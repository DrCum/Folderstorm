package mcptools

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"reflect"
	"strings"
	"sync"
	"testing"

	"fs-mcp/internal/discover"
	"fs-mcp/internal/viewerapi"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const gestureItem = "11111111-1111-4111-8111-111111111111"
const gestureAsset = "22222222-2222-4222-8222-222222222222"
const gesturePlayback = "33333333-3333-4333-8333-333333333333"
const gestureHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

type gestureFixture struct {
	mu          sync.Mutex
	requests    []map[string]any
	read, play  string
	health      string
	legacy      bool
	result      string
	wrongSource bool
}

func newGestureFixture(t *testing.T) (*gestureFixture, *Server) {
	t.Helper()
	fixture := &gestureFixture{read: "allow", play: "ask", health: `{"capabilities":{"gesture_tools":1}}`, result: `{"state":"submitted","cleanup_complete":true,"sound_delivery":"submitted_only_not_audibility"}`}
	httpServer := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		fixture.mu.Lock()
		defer fixture.mu.Unlock()
		if r.Method != http.MethodPost || r.Header.Get("Authorization") != "Bearer fixture-secret" {
			t.Error("typed request lost native authenticated transport")
			w.WriteHeader(http.StatusUnauthorized)
			return
		}
		var request map[string]any
		if err := json.NewDecoder(r.Body).Decode(&request); err != nil {
			t.Error(err)
			w.WriteHeader(http.StatusBadRequest)
			return
		}
		fixture.requests = append(fixture.requests, request)
		if request["viewer_session"] != "gesture-login" {
			fixture.wrongSource = true
		}
		w.Header().Set("Content-Type", "application/json")
		switch request["api"].(string) + "." + request["op"].(string) {
		case "LLInventory.status":
			if fixture.legacy {
				_, _ = io.WriteString(w, `{"logged_in":true}`)
			} else {
				_ = json.NewEncoder(w).Encode(map[string]any{"policy_generation": 1, "permissions": map[string]string{"read": "deny", "gesture_read": fixture.read, "gesture_play": fixture.play, "chat_send_nearby": "deny"}})
			}
		case "LocalAssistant.health":
			_, _ = io.WriteString(w, fixture.health)
		case "FSGestures.list", "FSGestures.status", "FSGestures.play", "FSGestures.stop":
			_, _ = io.WriteString(w, fixture.result)
		default:
			t.Error("unexpected legacy/control operation", request)
			w.WriteHeader(http.StatusBadRequest)
		}
	}))
	t.Cleanup(httpServer.Close)
	inst := testInstance(11)
	inst.URL, inst.ViewerSession, inst.Token = httpServer.URL, "gesture-login", "fixture-secret"
	state := &Server{Find: func() ([]discover.Instance, error) { return []discover.Instance{inst}, nil }, NewClient: func(in discover.Instance) API { return viewerapi.New(in) }}
	return fixture, state
}
func (f *gestureFixture) operations() []string {
	f.mu.Lock()
	defer f.mu.Unlock()
	result := make([]string, 0, len(f.requests))
	for _, request := range f.requests {
		result = append(result, request["api"].(string)+"."+request["op"].(string))
	}
	return result
}

func TestGestureCapabilityIsExactAndBounded(t *testing.T) {
	for _, raw := range []string{`{}`, `null`, `{"capabilities":{"gesture_tools":true}}`, `{"capabilities":{"gesture_tools":"1"}}`, `{"capabilities":{"gesture_tools":0}}`, `{"capabilities":{"gesture_tools":2}}`, `{"capabilities":{"gesture_tools":1.1}}`, `{"capabilities":{"gesture_tools":1}}` + strings.Repeat(" ", 65536)} {
		if gestureCapability([]byte(raw)) {
			t.Fatalf("accepted unsupported gesture service: %.90s", raw)
		}
	}
	if !gestureCapability([]byte(`{"capabilities":{"gesture_tools":1}}`)) {
		t.Fatal("exact supported capability refused")
	}
}
func TestGesturesRegisteredSDKToolsAndNativeHTTPBoundaries(t *testing.T) {
	fixture, state := newGestureFixture(t)
	server := mcp.NewServer(&mcp.Implementation{Name: "gesture-test", Version: "1"}, nil)
	registerGestures(server, state)
	ctx := context.Background()
	ct, st := mcp.NewInMemoryTransports()
	ss, err := server.Connect(ctx, st, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer ss.Close()
	client := mcp.NewClient(&mcp.Implementation{Name: "gesture-client", Version: "1"}, nil)
	cs, err := client.Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer cs.Close()
	tools, err := cs.ListTools(ctx, nil)
	if err != nil || len(tools.Tools) != 4 {
		t.Fatalf("wrong registered gesture surface: %v %v", tools, err)
	}
	for _, tool := range tools.Tools {
		read := tool.Name == "gesture_list" || tool.Name == "gesture_status"
		if tool.Annotations == nil || tool.Annotations.ReadOnlyHint != read || tool.Annotations.OpenWorldHint == nil || !*tool.Annotations.OpenWorldHint {
			t.Fatal("incorrect world-effect annotation", tool.Name)
		}
		raw, _ := json.Marshal(tool.InputSchema)
		var schema struct {
			Required []string `json:"required"`
		}
		if json.Unmarshal(raw, &schema) != nil || !contains(schema.Required, "viewer_session") {
			t.Fatal("exact source missing from registered schema", tool.Name, string(raw))
		}
	}
	for _, test := range []struct {
		name string
		args map[string]any
	}{
		{"gesture_list", map[string]any{"limit": 3}},
		{"gesture_status", map[string]any{"item_id": gestureItem}},
		{"gesture_play", map[string]any{"item_id": gestureItem, "asset_id": gestureAsset, "asset_hash": gestureHash, "client_request_id": "explicit-1"}},
		{"gesture_stop", map[string]any{"item_id": gestureItem, "playback_id": gesturePlayback}},
	} {
		test.args["viewer_session"] = "gesture-login"
		result := call(t, cs, test.name, test.args)
		if result.IsError {
			t.Fatalf("typed registered gesture failed: %s", textOf(result))
		}
	}
	want := []string{}
	for _, op := range []string{"list", "status", "play", "stop"} {
		want = append(want, "LLInventory.status", "LocalAssistant.health", "FSGestures."+op)
	}
	if !reflect.DeepEqual(fixture.operations(), want) || fixture.wrongSource {
		t.Fatal("request crossed native/source boundary", fixture.operations())
	}
	fixture.mu.Lock()
	play := fixture.requests[8]
	fixture.mu.Unlock()
	for key, value := range map[string]string{"item_id": gestureItem, "asset_id": gestureAsset, "asset_hash": gestureHash, "client_request_id": "explicit-1"} {
		if play[key] != value {
			t.Fatal("frozen play material changed", key, play)
		}
	}
}
func TestGesturesRejectWrongSourceAndBoundsBeforeDiscovery(t *testing.T) {
	state := &Server{Find: func() ([]discover.Instance, error) { t.Fatal("invalid args reached discovery"); return nil, nil }}
	for _, args := range []gesturePlayArgs{
		{ViewerSession: "gesture-login", ItemID: "gesture by name"},
		{ViewerSession: "gesture-login", ItemID: gestureItem, AssetID: gestureAsset, AssetHash: "stale", ClientRequestID: "1"},
		{ViewerSession: "gesture-login", ItemID: gestureItem, AssetID: gestureAsset, AssetHash: strings.ToUpper(gestureHash), ClientRequestID: "1"},
		{ViewerSession: "gesture-login", ItemID: gestureItem, AssetID: gestureAsset, AssetHash: gestureHash, ClientRequestID: strings.Repeat("x", 129)},
	} {
		result, _, _ := state.gesturePlay(context.Background(), nil, args)
		if !result.IsError {
			t.Fatal("invalid material accepted")
		}
	}
	for _, limit := range []int{-1, 101} {
		result, _, _ := state.gestureList(context.Background(), nil, gestureListArgs{ViewerSession: "gesture-login", Limit: limit})
		if !result.IsError {
			t.Fatal("unbounded read accepted")
		}
	}
	for _, session := range []string{"", " gesture-login"} {
		result, _, _ := state.gestureStatus(context.Background(), nil, gestureItemArgs{ViewerSession: session, ItemID: gestureItem})
		if !result.IsError {
			t.Fatal("missing or ambiguous source accepted")
		}
	}
	fixture, valid := newGestureFixture(t)
	result, _, _ := valid.gestureStatus(context.Background(), nil, gestureItemArgs{ViewerSession: "different-login", ItemID: gestureItem})
	if !result.IsError || len(fixture.operations()) != 0 {
		t.Fatal("wrong login contacted a viewer")
	}
}
func TestGesturesOptInCompoundPermissionAndHealthRechecks(t *testing.T) {
	for _, test := range []struct {
		read, play, health string
		legacy             bool
	}{
		{"deny", "allow", `{"capabilities":{"gesture_tools":1}}`, false},
		{"allow", "deny", `{"capabilities":{"gesture_tools":1}}`, false},
		{"allow", "ask", `{"capabilities":{"gesture_tools":2}}`, false},
		{"allow", "ask", `{}`, false},
		{"allow", "ask", `{"capabilities":{"gesture_tools":1}}`, true},
	} {
		fixture, state := newGestureFixture(t)
		fixture.read, fixture.play, fixture.health, fixture.legacy = test.read, test.play, test.health, test.legacy
		result, _, _ := state.gesturePlay(context.Background(), nil, gesturePlayArgs{ViewerSession: "gesture-login", ItemID: gestureItem, AssetID: gestureAsset, AssetHash: gestureHash, ClientRequestID: "intent"})
		if !result.IsError || contains(fixture.operations(), "FSGestures.play") {
			t.Fatal("unsupported or denied play reached viewer", test, fixture.operations())
		}
	}
	fixture, state := newGestureFixture(t)
	args := gestureItemArgs{ViewerSession: "gesture-login", ItemID: gestureItem}
	if result, _, _ := state.gestureStatus(context.Background(), nil, args); result.IsError {
		t.Fatal("first read failed")
	}
	fixture.mu.Lock()
	fixture.health = `{"capabilities":{"gesture_tools":2}}`
	fixture.mu.Unlock()
	result, _, _ := state.gestureStatus(context.Background(), nil, args)
	if !result.IsError || count(fixture.operations(), "FSGestures.status") != 1 || count(fixture.operations(), "LocalAssistant.health") != 2 {
		t.Fatal("stale health version survived across calls", fixture.operations())
	}
}
func TestGesturesPreserveNativePartialAndUnsupportedEvidence(t *testing.T) {
	for _, native := range []string{
		`{"state":"partially_submitted","cleanup_complete":false,"reason":"animation_ownership_superseded","sound_triggers_submitted":1}`,
		`{"status":"unavailable","reason":"gesture_asset_download_failed"}`,
		`{"state":"pending","authority_duration":"bridge_request_deadline_may_cancel_before_120_seconds"}`,
		`{"status":"inspected","animation_assets":[{"asset_id":"22222222-2222-4222-8222-222222222222","asset_hash":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size_bytes":41,"header_version":"1.0","emote_name":""}]}`,
	} {
		fixture, state := newGestureFixture(t)
		fixture.result = native
		result, data, err := state.gestureStatus(context.Background(), nil, gestureItemArgs{ViewerSession: "gesture-login", ItemID: gestureItem})
		if err != nil || result.IsError {
			t.Fatal("native evidence turned into transport failure", err, result)
		}
		got, _ := json.Marshal(data)
		var expected any
		_ = json.Unmarshal([]byte(native), &expected)
		want, _ := json.Marshal(expected)
		if string(got) != string(want) {
			t.Fatal("native uncertainty evidence changed", string(got), string(want))
		}
	}
}
