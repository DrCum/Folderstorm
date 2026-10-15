package mcptools

import (
	"context"
	"encoding/json"
	"fmt"
	"fs-mcp/internal/discover"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"strings"
	"testing"
)

const contentItem = "11111111-1111-1111-1111-111111111111"
const contentAsset = "22222222-2222-2222-2222-222222222222"
const contentOperation = "33333333-3333-3333-3333-333333333333"

func contentTestPolicy(read, write string, version int) json.RawMessage {
	return json.RawMessage(fmt.Sprintf(`{"policy_generation":1,"permissions":{"read":%q,"content_edit":%q},"capabilities":{"content_editing":%d}}`, read, write, version))
}

type contentTestAPI struct {
	fakeAPI
	health json.RawMessage
	result json.RawMessage
}

func (a *contentTestAPI) CallNamed(ctx context.Context, api, op string, params map[string]any) (json.RawMessage, error) {
	if api == "LocalAssistant" && op == "health" {
		a.record(api + "." + op)
		if a.health != nil {
			return a.health, nil
		}
		return json.RawMessage(`{"bridge_ready":true,"apis_ready":{"FSContent":true},"capabilities":{"content_editing":1}}`), nil
	}
	raw, err := a.fakeAPI.CallNamed(ctx, api, op, params)
	if api == "FSContent" && a.result != nil {
		return a.result, nil
	}
	return raw, err
}
func contentRegisteredClient(t *testing.T, state *Server) *mcp.ClientSession {
	t.Helper()
	srv := mcp.NewServer(&mcp.Implementation{Name: "content-test", Version: "v1"}, nil)
	registerContent(srv, state, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
	ct, st := mcp.NewInMemoryTransports()
	if _, err := srv.Connect(context.Background(), st, nil); err != nil {
		t.Fatal(err)
	}
	cl := mcp.NewClient(&mcp.Implementation{Name: "test-client", Version: "v1"}, nil)
	cs, err := cl.Connect(context.Background(), ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs
}
func contentEditInput() map[string]any {
	return map[string]any{"id": contentItem, "expected_asset_id": contentAsset, "content_hash": strings.Repeat("a", 64), "text": "changed 😀\n"}
}
func TestContentRegisteredDispatch(t *testing.T) {
	tests := []struct {
		name, op string
		args     map[string]any
	}{
		{"inventory_content_get", "get", map[string]any{"id": contentItem}},
		{"inventory_content_preview", "preview", contentEditInput()},
		{"inventory_update_notecard", "saveNotecard", contentEditInput()},
		{"inventory_content_operation_status", "get", map[string]any{"operation_id": contentOperation}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("allow", "ask", 1)}}
			result := call(t, contentRegisteredClient(t, reviewServer(api)), tt.name, tt.args)
			if result.IsError || !contains(api.opsCopy(), "FSContent."+tt.op) || !contains(api.opsCopy(), "LocalAssistant.health") {
				t.Fatalf("registered tool did not dispatch: %s", textOf(result))
			}
			for key, value := range tt.args {
				if api.lastNamed[key] != value {
					t.Fatalf("changed %s: %#v", key, api.lastNamed)
				}
			}
		})
	}
}
func TestContentPermissionAndVersionFailClosed(t *testing.T) {
	for _, tt := range []struct {
		read, write  string
		version      int
		save, denied bool
	}{
		{"allow", "ask", 1, true, false}, {"allow", "deny", 1, true, true}, {"deny", "allow", 1, true, true},
		{"allow", "deny", 1, false, false}, {"deny", "allow", 1, false, true}, {"allow", "allow", 0, true, true}, {"allow", "allow", 2, false, true},
	} {
		api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy(tt.read, tt.write, tt.version)}}
		op := "get"
		params := map[string]any{"id": contentItem}
		if tt.save {
			op = "saveNotecard"
			params = contentEditInput()
		}
		result, _, err := reviewServer(api).contentCall(context.Background(), 11, op, params, tt.save)
		if err != nil || result.IsError != tt.denied || contains(api.opsCopy(), "FSContent."+op) == tt.denied {
			t.Fatalf("incorrect gate %+v: %v %v", tt, err, result)
		}
	}
}
func TestContentHealthIsFreshAndRequired(t *testing.T) {
	for _, health := range []string{`{}`, `not json`, `{"bridge_ready":true,"capabilities":{"content_editing":1}}`, `{"bridge_ready":false,"apis_ready":{"FSContent":true},"capabilities":{"content_editing":1}}`, `{"bridge_ready":true,"apis_ready":{"FSContent":true},"capabilities":{"content_editing":2}}`} {
		api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("allow", "allow", 1)}, health: json.RawMessage(health)}
		result, _, _ := reviewServer(api).contentCall(context.Background(), 11, "saveNotecard", contentEditInput(), true)
		if !result.IsError || contains(api.opsCopy(), "FSContent.saveNotecard") {
			t.Fatalf("stale health accepted %s", health)
		}
	}
	api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("allow", "allow", 1)}}
	s := reviewServer(api)
	good, _, _ := s.contentCall(context.Background(), 11, "get", map[string]any{"id": contentItem}, false)
	if good.IsError {
		t.Fatal("initial health failed")
	}
	api.health = json.RawMessage(`{"bridge_ready":false}`)
	bad, _, _ := s.contentCall(context.Background(), 11, "get", map[string]any{"id": contentItem}, false)
	count := 0
	for _, op := range api.opsCopy() {
		if op == "FSContent.get" {
			count++
		}
	}
	if !bad.IsError || count != 1 {
		t.Fatal("cached feature authority bypassed fresh health")
	}
}
func TestContentInputLimitsAndRawText(t *testing.T) {
	base := contentEditArgs{ID: contentItem, ExpectedAssetID: contentAsset, ContentHash: strings.Repeat("a", 64), Text: ""}
	if !validContentEdit(base) {
		t.Fatal("empty complete source must be allowed")
	}
	for _, change := range []func(*contentEditArgs){
		func(a *contentEditArgs) { a.ID = "/tmp/script.lsl" }, func(a *contentEditArgs) { a.ExpectedAssetID = "https://example.test/script" },
		func(a *contentEditArgs) { a.Text = strings.Repeat("x", contentTextLimit+1) }, func(a *contentEditArgs) { a.Text = "a\x00b" },
		func(a *contentEditArgs) { a.Text = "\xff" }, func(a *contentEditArgs) { a.Compiler = "preprocess" }, func(a *contentEditArgs) { a.ContentHash = "partial" },
	} {
		a := base
		change(&a)
		if validContentEdit(a) {
			t.Fatalf("invalid text request accepted: %+v", a)
		}
	}
}
func TestContentStatusRequiresReadAndExactOperation(t *testing.T) {
	api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("deny", "allow", 1)}}
	res, _, _ := reviewServer(api).contentStatus(context.Background(), nil, contentStatusArgs{OperationID: contentOperation, ViewerPID: 11})
	if !res.IsError || contains(api.opsCopy(), "FSContent.get") {
		t.Fatal("private receipt bypassed Read")
	}
	api = &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("allow", "deny", 1)}}
	res, _, _ = reviewServer(api).contentStatus(context.Background(), nil, contentStatusArgs{OperationID: "other-source", ViewerPID: 11})
	if !res.IsError || contains(api.opsCopy(), "FSContent.get") {
		t.Fatal("invalid operation forwarded")
	}
}
func TestContentSelectedSessionCannotChange(t *testing.T) {
	instance := testInstance(11)
	instance.Managed = true
	instance.ViewerSession = "source-epoch"
	instance.Account = "account-one"
	instance.Grid = "grid-one"
	api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("allow", "ask", 1)}, health: json.RawMessage(`{"bridge_ready":true,"viewer_session":"different-epoch","account":"account-one","grid":"grid-one","apis_ready":{"FSContent":true},"capabilities":{"managed_sessions":1,"content_editing":1}}`)}
	s := reviewServer(api)
	s.Find = func() ([]discover.Instance, error) { return []discover.Instance{instance}, nil }
	res, _, _ := s.contentStatus(context.Background(), nil, contentStatusArgs{OperationID: contentOperation, ViewerSession: "source-epoch", ViewerPID: 11})
	if !res.IsError || contains(api.opsCopy(), "FSContent.get") {
		t.Fatal("receipt crossed viewer source epoch")
	}
}
func TestContentConfirmedReceiptIsPreserved(t *testing.T) {
	api := &contentTestAPI{fakeAPI: fakeAPI{status: contentTestPolicy("allow", "ask", 1)}, result: json.RawMessage(`{"operation_id":"33333333-3333-3333-3333-333333333333","source_saved":true,"source_saved_known":true,"asset_id":"22222222-2222-2222-2222-222222222222","state":"server_confirmed"}`)}
	result, _, _ := reviewServer(api).contentStatus(context.Background(), nil, contentStatusArgs{OperationID: contentOperation, ViewerPID: 11})
	if result.IsError || !strings.Contains(textOf(result), `"source_saved":true`) {
		t.Fatalf("known saved identity lost: %s", textOf(result))
	}
}

var _ API = (*contentTestAPI)(nil)
