package mcptools

import (
	"context"
	"encoding/json"
	"math"
	"reflect"
	"strings"
	"testing"

	"fs-mcp/internal/discover"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type preferencesTestAPI struct {
	fakeAPI
	health json.RawMessage
	result json.RawMessage
}

func (a *preferencesTestAPI) CallNamed(ctx context.Context, api, op string, params map[string]any) (json.RawMessage, error) {
	_, _ = a.fakeAPI.CallNamed(ctx, api, op, params)
	if api == "LocalAssistant" && op == "health" {
		return a.health, nil
	}
	if a.result != nil {
		return a.result, nil
	}
	return json.RawMessage(`{"status":"observed","revision":"7"}`), nil
}
func preferencesFixture() (*Server, *preferencesTestAPI) {
	api := &preferencesTestAPI{
		fakeAPI: fakeAPI{status: json.RawMessage(`{"policy_generation":1,"permissions":{"read":"deny","preferences_read":"allow","preferences_write":"ask"}}`)},
		health:  json.RawMessage(`{"capabilities":{"preferences_tools":1}}`),
	}
	state := reviewServer(api)
	inst := testInstance(11)
	inst.ViewerSession = "opaque-login-a"
	state.Find = func() ([]discover.Instance, error) { return []discover.Instance{inst}, nil }
	return state, api
}
func preferencesClient(t *testing.T, state *Server) *mcp.ClientSession {
	t.Helper()
	server := mcp.NewServer(&mcp.Implementation{Name: "preferences-test", Version: "1"}, nil)
	registerPreferences(server, state, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
	ctx := context.Background()
	ct, st := mcp.NewInMemoryTransports()
	ss, err := server.Connect(ctx, st, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = ss.Close() })
	client := mcp.NewClient(&mcp.Implementation{Name: "preferences-client", Version: "1"}, nil)
	cs, err := client.Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs
}

func TestPreferencesRegisteredToolsDispatchExactGuardedSurface(t *testing.T) {
	for _, test := range []struct {
		name, op string
		args     map[string]any
	}{
		{"preferences_schema", "schema", map[string]any{}},
		{"preferences_get", "get", map[string]any{"ids": []string{"mouse_sensitivity", "master_volume"}}},
		{"preferences_status", "status", map[string]any{}},
		{"preferences_preview", "preview", map[string]any{"changes": map[string]any{"invert_mouse": true, "ui_scale": 1.25}, "expected_revision": "7", "ttl_seconds": 300}},
		{"preferences_accept", "accept", map[string]any{"transaction_id": "transaction-a", "expected_revision": "8"}},
		{"preferences_cancel", "cancel", map[string]any{"transaction_id": "transaction-a", "expected_revision": "8"}},
	} {
		t.Run(test.name, func(t *testing.T) {
			state, api := preferencesFixture()
			cs := preferencesClient(t, state)
			test.args["viewer_session"] = "opaque-login-a"
			res := call(t, cs, test.name, test.args)
			if res.IsError {
				t.Fatal(textOf(res))
			}
			if !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health", "FSPreferences." + test.op}) {
				t.Fatal(api.opsCopy())
			}
			want := map[string]any{}
			for key, val := range test.args {
				if key != "viewer_session" {
					want[key] = val
				}
			}
			gotRaw, _ := json.Marshal(api.lastNamed)
			wantRaw, _ := json.Marshal(want)
			if string(gotRaw) != string(wantRaw) {
				t.Fatalf("material input changed: got=%s want=%s", gotRaw, wantRaw)
			}
		})
	}
}

func TestPreferencesRegisteredSchemasAreClosedTypedAndExactSessionRequired(t *testing.T) {
	state, _ := preferencesFixture()
	cs := preferencesClient(t, state)
	list, err := cs.ListTools(context.Background(), nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(list.Tools) != 6 {
		t.Fatal(len(list.Tools))
	}
	for _, tool := range list.Tools {
		read := tool.Name == "preferences_schema" || tool.Name == "preferences_get" || tool.Name == "preferences_status"
		if tool.Annotations == nil || tool.Annotations.ReadOnlyHint != read {
			t.Fatal("incorrect read/write annotation", tool.Name)
		}
		var schema map[string]any
		raw, _ := json.Marshal(tool.InputSchema)
		_ = json.Unmarshal(raw, &schema)
		required := schema["required"].([]any)
		sessionRequired := false
		for _, key := range required {
			sessionRequired = sessionRequired || key == "viewer_session"
		}
		if !sessionRequired {
			t.Fatal("missing exact session", tool.Name)
		}
		if tool.Name == "preferences_preview" {
			changes := schema["properties"].(map[string]any)["changes"].(map[string]any)
			if changes["additionalProperties"] != false || changes["maxProperties"] != float64(16) {
				t.Fatal(changes)
			}
			props := changes["properties"].(map[string]any)
			if len(props) != 12 {
				t.Fatal("unexpected writable catalog", props)
			}
			for id, entry := range preferenceControls {
				_, exists := props[id]
				if exists != entry.writable {
					t.Fatal(id)
				}
			}
		}
	}
}

func TestPreferencesInvalidInputNeverReadsPolicyOrDispatches(t *testing.T) {
	for _, test := range []struct {
		name string
		args map[string]any
	}{
		{"preferences_schema", map[string]any{}},
		{"preferences_get", map[string]any{"viewer_session": "opaque-login-a", "ids": []string{"LocalAssistantPermissions"}}},
		{"preferences_get", map[string]any{"viewer_session": "opaque-login-a", "ids": []string{"ui_scale", "ui_scale"}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"LocalAssistantEnabled": true}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"graphics_quality": 3}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"master_volume": .5}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"mute_audio": true}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"invert_mouse": 1}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"ui_scale": 3}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"look_at_targets": .5}}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"quick_jump": true}, "ttl_seconds": 301}},
		{"preferences_preview", map[string]any{"viewer_session": "opaque-login-a", "expected_revision": "1", "changes": map[string]any{"quick_jump": true}, "key": "arbitrary"}},
		{"preferences_accept", map[string]any{"viewer_session": "opaque-login-a", "transaction_id": "transaction-a", "expected_revision": "0"}},
		{"preferences_cancel", map[string]any{"viewer_session": "opaque-login-a", "transaction_id": "", "expected_revision": "7"}},
	} {
		t.Run(test.name, func(t *testing.T) {
			state, api := preferencesFixture()
			cs := preferencesClient(t, state)
			res := call(t, cs, test.name, test.args)
			if !res.IsError || len(api.opsCopy()) != 0 {
				t.Fatalf("invalid request reached viewer: %s %v", textOf(res), api.opsCopy())
			}
		})
	}
	for _, bad := range []map[string]any{{"ui_scale": math.NaN()}, {"draw_distance": math.Inf(1)}, {"look_at_targets": true}, {"invert_mouse": "true"}, {"effects_volume": .5}, {}} {
		if preferencesChanges(bad) {
			t.Fatal("accepted invalid draft", bad)
		}
	}
}

func TestPreferencesCapabilityNeverCachedOrFallsBackToControls(t *testing.T) {
	for _, health := range []string{`{}`, `null`, `{"capabilities":{"preferences_tools":true}}`, `{"capabilities":{"preferences_tools":"1"}}`, `{"capabilities":{"preferences_tools":2}}`, `{"capabilities":{"preferences_tools":1.5}}`, `{"capabilities":{"preferences_tools":1}}` + strings.Repeat(" ", 65536)} {
		state, api := preferencesFixture()
		api.health = json.RawMessage(health)
		res, _, _ := state.preferencesSchema(context.Background(), nil, preferencesViewerArgs{ViewerSession: "opaque-login-a"})
		if !res.IsError || !strings.Contains(textOf(res), "unsupported_feature") || !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health"}) {
			t.Fatalf("unsupported feature called legacy API: %s %v", textOf(res), api.opsCopy())
		}
	}
	state, api := preferencesFixture()
	res, _, _ := state.preferencesSchema(context.Background(), nil, preferencesViewerArgs{ViewerSession: "opaque-login-a"})
	if res.IsError {
		t.Fatal(textOf(res))
	}
	api.health = json.RawMessage(`{"capabilities":{"preferences_tools":0}}`)
	res, _, _ = state.preferencesSchema(context.Background(), nil, preferencesViewerArgs{ViewerSession: "opaque-login-a"})
	if !res.IsError || count(api.opsCopy(), "FSPreferences.schema") != 1 {
		t.Fatal("cached a previous capability", api.opsCopy())
	}
	for _, op := range []string{"set", "toggle", "vars", "groups", "apply", "saveSettings"} {
		if preferencesOperation(op) {
			t.Fatal("generic operation exposed", op)
		}
	}
}

func TestPreferencesReadAndWritePermissionsAreSeparateAndRevocationIsFresh(t *testing.T) {
	for _, status := range []string{`{}`, `{"permissions":{"read":"allow"}}`, `{"permissions":{"preferences_read":"deny","preferences_write":"allow"}}`, `{"permissions":{"preferences_read":"allow","preferences_write":"deny"}}`} {
		state, api := preferencesFixture()
		api.status = json.RawMessage(status)
		res, _, _ := state.preferencesPreview(context.Background(), nil, preferencesPreviewArgs{ViewerSession: "opaque-login-a", ExpectedRevision: "1", Changes: map[string]any{"quick_jump": true}})
		if !res.IsError || contains(api.opsCopy(), "FSPreferences.preview") {
			t.Fatalf("preference opt-in bypassed: %s %v", textOf(res), api.opsCopy())
		}
	}
	state, api := preferencesFixture()
	api.status = json.RawMessage(`{"policy_generation":1,"permissions":{"preferences_read":"allow","preferences_write":"deny"}}`)
	res, _, _ := state.preferencesGet(context.Background(), nil, preferencesGetArgs{ViewerSession: "opaque-login-a"})
	if res.IsError || !contains(api.opsCopy(), "FSPreferences.get") {
		t.Fatal("read required write permission", textOf(res))
	}
	api.status = json.RawMessage(`{"policy_generation":2,"permissions":{"preferences_read":"deny","preferences_write":"ask"}}`)
	res, _, _ = state.preferencesStatus(context.Background(), nil, preferencesViewerArgs{ViewerSession: "opaque-login-a"})
	if !res.IsError || contains(api.opsCopy(), "FSPreferences.status") {
		t.Fatal("revoked read leaked draft", textOf(res), api.opsCopy())
	}
}

func TestPreferencesWrongLoginAndMismatchedPIDNeverReachViewer(t *testing.T) {
	for _, args := range []preferencesViewerArgs{{ViewerSession: "expired-login"}, {ViewerSession: "opaque-login-a", ViewerPID: 12}, {ViewerPID: 11}} {
		state, api := preferencesFixture()
		res, _, _ := state.preferencesSchema(context.Background(), nil, args)
		if !res.IsError || len(api.opsCopy()) != 0 {
			t.Fatal("wrong login reached preferences", textOf(res), api.opsCopy())
		}
	}
}

func TestPreferencesNativeConflictAndPartialCleanupRemainHonest(t *testing.T) {
	for _, result := range []string{`{"status":"partially_restored","last_cleanup":{"skipped_count":1}}`, `{"status":"conflict","revision":"9"}`, `{"status":"previewed","revision":"8","changes":[{"id":"ui_scale","before":{"saved":1},"after":{"effective":1.25,"saved":1,"unsaved":true}}]}`} {
		state, api := preferencesFixture()
		api.result = json.RawMessage(result)
		res, data, err := state.preferencesStatus(context.Background(), nil, preferencesViewerArgs{ViewerSession: "opaque-login-a"})
		if err != nil || res.IsError {
			t.Fatal(textOf(res), err)
		}
		got, _ := json.Marshal(data)
		var expected any
		_ = json.Unmarshal([]byte(result), &expected)
		want, _ := json.Marshal(expected)
		if string(got) != string(want) {
			t.Fatalf("native outcome changed: got=%s want=%s", got, want)
		}
	}
}
