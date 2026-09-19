package mcptools

import (
	"context"
	"encoding/json"
	"strings"
	"sync"
	"testing"

	"fs-mcp/internal/discover"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type fakeAPI struct {
	mu       sync.Mutex
	ops      []string
	status   json.RawMessage
	get      json.RawMessage
	copy     json.RawMessage
	confirm  json.RawMessage
	lastCopy map[string]any
}

func (f *fakeAPI) record(op string) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.ops = append(f.ops, op)
}

func (f *fakeAPI) Status(context.Context) (json.RawMessage, error) {
	f.record("status")
	if f.status != nil {
		return f.status, nil
	}
	return json.RawMessage(`{"logged_in":true}`), nil
}
func (f *fakeAPI) Get(context.Context, string) (json.RawMessage, error) {
	f.record("get")
	if f.get != nil {
		return f.get, nil
	}
	return json.RawMessage(`{"id":"item-1","name":"Shirt","type":"item"}`), nil
}
func (f *fakeAPI) List(context.Context, map[string]any) (json.RawMessage, error) {
	f.record("list")
	return json.RawMessage(`{"items":[],"categories":[]}`), nil
}
func (f *fakeAPI) Search(context.Context, map[string]any) (json.RawMessage, error) {
	f.record("search")
	return json.RawMessage(`{"items":[]}`), nil
}
func (f *fakeAPI) SystemFolder(context.Context, string) (json.RawMessage, error) {
	f.record("systemFolder")
	return json.RawMessage(`{"id":"sys-tex"}`), nil
}
func (f *fakeAPI) CreateFolder(context.Context, map[string]any) (json.RawMessage, error) {
	f.record("createFolder")
	return json.RawMessage(`{"id":"new-folder"}`), nil
}
func (f *fakeAPI) Move(context.Context, map[string]any) (json.RawMessage, error) {
	f.record("move")
	return json.RawMessage(`{"ok":true}`), nil
}
func (f *fakeAPI) Rename(context.Context, map[string]any) (json.RawMessage, error) {
	f.record("rename")
	return json.RawMessage(`{"ok":true}`), nil
}
func (f *fakeAPI) Copy(_ context.Context, params map[string]any) (json.RawMessage, error) {
	f.record("copy")
	f.mu.Lock()
	f.lastCopy = params
	f.mu.Unlock()
	if f.copy != nil {
		return f.copy, nil
	}
	return json.RawMessage(`{"status":"ok"}`), nil
}
func (f *fakeAPI) ConfirmCopy(context.Context, map[string]any) (json.RawMessage, error) {
	f.record("confirmCopy")
	if f.confirm != nil {
		return f.confirm, nil
	}
	return json.RawMessage(`{"status":"ok"}`), nil
}

func (f *fakeAPI) opsCopy() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	out := make([]string, len(f.ops))
	copy(out, f.ops)
	return out
}

func testInstance(pid int) discover.Instance {
	return discover.Instance{PID: pid, Port: 10000 + pid, Host: "127.0.0.1", Token: "secret", Alive: true, Source: "test"}
}

func connect(t *testing.T, state *Server, clientOpts *mcp.ClientOptions) *mcp.ClientSession {
	t.Helper()
	ctx := context.Background()
	srv := NewMCPServer(state)
	ct, st := mcp.NewInMemoryTransports()
	if _, err := srv.Connect(ctx, st, nil); err != nil {
		t.Fatal(err)
	}
	cl := mcp.NewClient(&mcp.Implementation{Name: "test-client", Version: "v0"}, clientOpts)
	cs, err := cl.Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs
}

func call(t *testing.T, cs *mcp.ClientSession, name string, args map[string]any) *mcp.CallToolResult {
	t.Helper()
	res, err := cs.CallTool(context.Background(), &mcp.CallToolParams{Name: name, Arguments: args})
	if err != nil {
		t.Fatalf("%s: %v", name, err)
	}
	return res
}

func TestViewerStatusAutoSelectsSingle(t *testing.T) {
	api := &fakeAPI{status: json.RawMessage(`{"logged_in":true,"agent":"Test Resident"}`)}
	state := &Server{
		Find: func() ([]discover.Instance, error) {
			return []discover.Instance{testInstance(11)}, nil
		},
		NewClient: func(discover.Instance) API { return api },
	}
	cs := connect(t, state, nil)
	res := call(t, cs, "viewer_status", map[string]any{})
	if res.IsError {
		t.Fatalf("unexpected error: %s", textOf(res))
	}
	text := textOf(res)
	if strings.Contains(text, "secret") {
		t.Fatalf("token leaked: %s", text)
	}
	if !strings.Contains(text, `"pid":11`) {
		t.Fatalf("missing pid: %s", text)
	}
	if !strings.Contains(text, "Test Resident") {
		t.Fatalf("missing status: %s", text)
	}
}

func TestMultipleViewersRequireSelect(t *testing.T) {
	api := &fakeAPI{}
	state := &Server{
		Find: func() ([]discover.Instance, error) {
			return []discover.Instance{testInstance(1), testInstance(2)}, nil
		},
		NewClient: func(discover.Instance) API { return api },
	}
	cs := connect(t, state, nil)
	res := call(t, cs, "inventory_get", map[string]any{"id": "abc"})
	if !res.IsError {
		t.Fatal("expected multiple_viewers error")
	}
	if !strings.Contains(textOf(res), "multiple_viewers") {
		t.Fatalf("got %s", textOf(res))
	}
	sel := call(t, cs, "viewer_select", map[string]any{"pid": 2})
	if sel.IsError {
		t.Fatalf("select: %s", textOf(sel))
	}
	got := call(t, cs, "inventory_get", map[string]any{"id": "abc"})
	if got.IsError {
		t.Fatalf("get after select: %s", textOf(got))
	}
	if !strings.Contains(textOf(got), "item-1") {
		t.Fatalf("get = %s", textOf(got))
	}
}

func TestInventoryGetInvalidArgs(t *testing.T) {
	state := &Server{
		Find:      func() ([]discover.Instance, error) { return []discover.Instance{testInstance(1)}, nil },
		NewClient: func(discover.Instance) API { return &fakeAPI{} },
	}
	cs := connect(t, state, nil)
	res := call(t, cs, "inventory_get", map[string]any{"id": ""})
	if !res.IsError || !strings.Contains(textOf(res), "invalid_args") {
		t.Fatalf("got %s", textOf(res))
	}
}

func TestCopyPlanIDFallbackWithoutElicitation(t *testing.T) {
	api := &fakeAPI{copy: json.RawMessage(`{
		"status":"confirmation_required",
		"plan_id":"plan-99",
		"proposed_moves":[{"source_id":"no-copy-1","destination_parent_id":"dst"}]
	}`)}
	state := &Server{
		Find:      func() ([]discover.Instance, error) { return []discover.Instance{testInstance(1)}, nil },
		NewClient: func(discover.Instance) API { return api },
	}
	cs := connect(t, state, nil)
	res := call(t, cs, "inventory_copy", map[string]any{"id": "src", "parent_id": "dst"})
	if res.IsError {
		t.Fatalf("copy error: %s", textOf(res))
	}
	if !strings.Contains(textOf(res), "plan-99") {
		t.Fatalf("expected plan_id fallback, got %s", textOf(res))
	}
	if !strings.Contains(textOf(res), "inventory_confirm_copy") {
		t.Fatalf("expected confirm fallback, got %s", textOf(res))
	}
	if contains(api.opsCopy(), "confirmCopy") {
		t.Fatal("confirmCopy should not run without approval")
	}
	conf := call(t, cs, "inventory_confirm_copy", map[string]any{"plan_id": "plan-99"})
	if conf.IsError {
		t.Fatalf("confirm: %s", textOf(conf))
	}
	if !contains(api.opsCopy(), "confirmCopy") {
		t.Fatalf("ops = %v", api.opsCopy())
	}
}

func TestCopyElicitationAcceptsAndConfirms(t *testing.T) {
	api := &fakeAPI{
		copy:    json.RawMessage(`{"status":"confirmation_required","plan_id":"plan-elicit","proposed_moves":[{"source_id":"u1"}]}`),
		confirm: json.RawMessage(`{"status":"ok","moved":["u1"]}`),
	}
	state := &Server{
		Find:      func() ([]discover.Instance, error) { return []discover.Instance{testInstance(1)}, nil },
		NewClient: func(discover.Instance) API { return api },
	}
	cs := connect(t, state, &mcp.ClientOptions{
		ElicitationHandler: func(context.Context, *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
			return &mcp.ElicitResult{Action: "accept", Content: map[string]any{"confirm": true}}, nil
		},
	})
	res := call(t, cs, "inventory_copy", map[string]any{"id": "src", "parent_id": "dst"})
	if res.IsError {
		t.Fatalf("copy: %s", textOf(res))
	}
	if !strings.Contains(textOf(res), "u1") {
		t.Fatalf("confirm result = %s", textOf(res))
	}
	ops := api.opsCopy()
	if !contains(ops, "copy") || !contains(ops, "confirmCopy") {
		t.Fatalf("ops = %v", ops)
	}
}

func TestCopyElicitationDeclineKeepsPlan(t *testing.T) {
	api := &fakeAPI{
		copy: json.RawMessage(`{"status":"confirmation_required","plan_id":"plan-no","proposed_moves":[{"source_id":"u1"}]}`),
	}
	state := &Server{
		Find:      func() ([]discover.Instance, error) { return []discover.Instance{testInstance(1)}, nil },
		NewClient: func(discover.Instance) API { return api },
	}
	cs := connect(t, state, &mcp.ClientOptions{
		ElicitationHandler: func(context.Context, *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
			return &mcp.ElicitResult{Action: "decline"}, nil
		},
	})
	res := call(t, cs, "inventory_copy", map[string]any{"id": "src", "parent_id": "dst"})
	if res.IsError {
		t.Fatalf("copy: %s", textOf(res))
	}
	if !strings.Contains(textOf(res), "plan-no") || !strings.Contains(textOf(res), "declined") {
		t.Fatalf("got %s", textOf(res))
	}
	if contains(api.opsCopy(), "confirmCopy") {
		t.Fatal("confirmCopy on decline")
	}
}

func TestToolAnnotations(t *testing.T) {
	state := New()
	state.Find = func() ([]discover.Instance, error) { return nil, nil }
	cs := connect(t, state, nil)
	var tools []*mcp.Tool
	for tool, err := range cs.Tools(context.Background(), nil) {
		if err != nil {
			t.Fatal(err)
		}
		tools = append(tools, tool)
	}
	byName := map[string]*mcp.Tool{}
	for _, tool := range tools {
		byName[tool.Name] = tool
	}
	for _, name := range []string{
		"viewer_status", "viewer_list", "viewer_select",
		"inventory_get", "inventory_list", "inventory_search", "inventory_system_folder",
		"inventory_create_folder", "inventory_move", "inventory_rename",
		"inventory_copy", "inventory_confirm_copy",
	} {
		if byName[name] == nil {
			t.Fatalf("missing tool %s", name)
		}
	}
	if !byName["inventory_get"].Annotations.ReadOnlyHint {
		t.Fatal("inventory_get should be read-only")
	}
	if byName["inventory_confirm_copy"].Annotations.DestructiveHint == nil || !*byName["inventory_confirm_copy"].Annotations.DestructiveHint {
		t.Fatal("confirm_copy should be destructive")
	}
}

func textOf(res *mcp.CallToolResult) string {
	var b strings.Builder
	for _, c := range res.Content {
		if tc, ok := c.(*mcp.TextContent); ok {
			b.WriteString(tc.Text)
		}
	}
	return b.String()
}

func contains(list []string, want string) bool {
	for _, v := range list {
		if v == want {
			return true
		}
	}
	return false
}
