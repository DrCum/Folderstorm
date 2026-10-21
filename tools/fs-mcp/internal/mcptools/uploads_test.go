package mcptools

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func uploadClient(t *testing.T, state *Server) *mcp.ClientSession {
	t.Helper()
	server := mcp.NewServer(&mcp.Implementation{Name: "upload-test", Version: "1"}, nil)
	registerUploads(server, state, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
	ctx := context.Background()
	ct, st := mcp.NewInMemoryTransports()
	ss, err := server.Connect(ctx, st, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = ss.Close() })
	cs, err := mcp.NewClient(&mcp.Implementation{Name: "client", Version: "1"}, nil).Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs
}
func uploadFixture() (*Server, *preferencesTestAPI) {
	s, api := buildingFixture()
	api.status = json.RawMessage(`{"permissions":{"read":"allow","create":"allow","upload":"allow"}}`)
	api.health = json.RawMessage(`{"capabilities":{"file_uploads":1}}`)
	return s, api
}
func writeUpload(t *testing.T, path string) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(path, []byte("native-test-input"), 0600); err != nil {
		t.Fatal(err)
	}
}
func TestUploadManifestHierarchyAndHash(t *testing.T) {
	root := t.TempDir()
	writeUpload(t, filepath.Join(root, "Z.wav"))
	writeUpload(t, filepath.Join(root, "Sub", "a.png"))
	files, err := uploadInputs(uploadPreviewArgs{Directory: root})
	if err != nil {
		t.Fatal(err)
	}
	sum := sha256.Sum256([]byte("native-test-input"))
	if len(files) != 2 || files[0].Folder != "Sub" || files[0].Name != "a" || files[1].Folder != "" || files[1].Name != "Z" || files[0].SHA256 != hex.EncodeToString(sum[:]) || files[0].Bytes != 17 {
		t.Fatalf("%+v", files)
	}
}
func TestUploadRejectsUnsupportedDuplicateSpecialAndOversized(t *testing.T) {
	root := t.TempDir()
	file := filepath.Join(root, "a.wav")
	writeUpload(t, file)
	for _, args := range []uploadPreviewArgs{{}, {Files: []string{file}, Directory: root}, {Files: []string{file, file}}, {Files: []string{"relative.wav"}}} {
		if _, err := uploadInputs(args); err == nil {
			t.Fatal(args)
		}
	}
	writeUpload(t, filepath.Join(root, "README.txt"))
	if _, err := uploadInputs(uploadPreviewArgs{Directory: root}); err == nil {
		t.Fatal("unsupported entry silently skipped")
	}
	large := filepath.Join(t.TempDir(), "large.png")
	f, err := os.Create(large)
	if err != nil {
		t.Fatal(err)
	}
	err = f.Truncate(uploadFileLimit + 1)
	_ = f.Close()
	if err != nil {
		t.Fatal(err)
	}
	if _, err = uploadInputs(uploadPreviewArgs{Files: []string{large}}); err == nil {
		t.Fatal("oversized source")
	}
	link := filepath.Join(t.TempDir(), "link.wav")
	if os.Symlink(file, link) == nil {
		if _, err = uploadInputs(uploadPreviewArgs{Files: []string{link}}); err == nil {
			t.Fatal("linked source")
		}
	}
	linkedDir := filepath.Join(t.TempDir(), "alias")
	if os.Symlink(root, linkedDir) == nil {
		if _, err = uploadInputs(uploadPreviewArgs{Files: []string{filepath.Join(linkedDir, "a.wav")}}); err == nil {
			t.Fatal("linked parent")
		}
	}
}
func TestUploadCountAndDepthLimits(t *testing.T) {
	root := t.TempDir()
	for i := 0; i < 33; i++ {
		writeUpload(t, filepath.Join(root, string(rune('a'+i))+".wav"))
	}
	if _, err := uploadInputs(uploadPreviewArgs{Directory: root}); err == nil {
		t.Fatal("count limit")
	}
	root = t.TempDir()
	writeUpload(t, filepath.Join(root, strings.Repeat("d/", 8), "a.wav"))
	if _, err := uploadInputs(uploadPreviewArgs{Directory: root}); err != nil {
		t.Fatal("eight folder levels", err)
	}
	root = t.TempDir()
	writeUpload(t, filepath.Join(root, strings.Repeat("d/", 9), "a.wav"))
	if _, err := uploadInputs(uploadPreviewArgs{Directory: root}); err == nil {
		t.Fatal("depth limit")
	}
}
func TestUploadRegisteredDispatchAndNoClientPriceBypass(t *testing.T) {
	for _, test := range []struct {
		name, op string
		params   map[string]any
	}{
		{"inventory_upload_execute", "execute", map[string]any{"plan_id": buildID}},
		{"inventory_upload_status", "status", map[string]any{"plan_id": buildID}},
		{"inventory_upload_cancel", "cancel", map[string]any{"plan_id": buildID}},
	} {
		t.Run(test.op, func(t *testing.T) {
			s, api := uploadFixture()
			cs := uploadClient(t, s)
			test.params["viewer_session"] = "login-a"
			r := call(t, cs, test.name, test.params)
			if r.IsError {
				t.Fatal(textOf(r))
			}
			if !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health", "FSUpload." + test.op}) {
				t.Fatal(api.opsCopy())
			}
			if _, ok := api.lastNamed["confirm"]; ok {
				t.Fatal("client confirmation bypass")
			}
			if _, ok := api.lastNamed["cost"]; ok {
				t.Fatal("client price")
			}
		})
	}
	s, api := uploadFixture()
	cs := uploadClient(t, s)
	file := filepath.Join(t.TempDir(), "a.png")
	writeUpload(t, file)
	r := call(t, cs, "inventory_upload_preview", map[string]any{"files": []string{file}, "folder_id": buildID, "viewer_session": "login-a"})
	if r.IsError {
		t.Fatal(textOf(r))
	}
	raw, _ := json.Marshal(api.lastNamed)
	var args struct {
		Files     []uploadFile `json:"files"`
		Dimension int          `json:"max_texture_dimension"`
	}
	if json.Unmarshal(raw, &args) != nil || len(args.Files) != 1 || args.Dimension != 1024 || len(args.Files[0].SHA256) != 64 {
		t.Fatal(string(raw))
	}
}
func TestUploadRefusesStaleDeniedAndUnsupportedViewer(t *testing.T) {
	s, api := uploadFixture()
	ctx := context.Background()
	for _, session := range []string{"", "stale"} {
		r, _, _ := s.uploadExecute(ctx, nil, uploadPlanArgs{PlanID: buildID, ViewerSession: session})
		if !r.IsError {
			t.Fatal(session)
		}
	}
	api.status = json.RawMessage(`{"permissions":{"read":"allow","create":"allow","upload":"deny"}}`)
	r, _, _ := s.uploadExecute(ctx, nil, uploadPlanArgs{PlanID: buildID, ViewerSession: "login-a"})
	if !r.IsError {
		t.Fatal("denied")
	}
	api.status = json.RawMessage(`{"permissions":{"read":"allow","create":"allow","upload":"allow"}}`)
	api.health = json.RawMessage(`{"capabilities":{}}`)
	r, _, _ = s.uploadExecute(ctx, nil, uploadPlanArgs{PlanID: buildID, ViewerSession: "login-a"})
	if !r.IsError {
		t.Fatal("old viewer")
	}
	for _, op := range api.opsCopy() {
		if strings.HasPrefix(op, "FSUpload.") {
			t.Fatal("unavailable source dispatched", op)
		}
	}
}
func TestUploadAllToolsRequireExactSession(t *testing.T) {
	s, _ := uploadFixture()
	cs := uploadClient(t, s)
	list, err := cs.ListTools(context.Background(), nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(list.Tools) != 5 {
		t.Fatal(len(list.Tools))
	}
	for _, tool := range list.Tools {
		raw, _ := json.Marshal(tool.InputSchema)
		var schema struct {
			Required   []string       `json:"required"`
			Properties map[string]any `json:"properties"`
		}
		_ = json.Unmarshal(raw, &schema)
		found := false
		for _, name := range schema.Required {
			found = found || name == "viewer_session"
		}
		if !found {
			t.Fatal(tool.Name)
		}
		if schema.Properties["confirm"] != nil || schema.Properties["cost"] != nil {
			t.Fatal("price supplied by agent")
		}
	}
}
