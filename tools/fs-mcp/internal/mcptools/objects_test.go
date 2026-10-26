package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"testing"
)

func objectsFixture(t *testing.T) (*mcp.ClientSession, *preferencesTestAPI) {
	t.Helper()
	api := &preferencesTestAPI{fakeAPI: fakeAPI{status: json.RawMessage(`{"policy_generation":1,"permissions":{"read":"allow","world_read":"allow","task_inventory_read":"allow","script_debug_read":"allow"}}`)}, health: json.RawMessage(`{"capabilities":{"task_inventory":1,"script_diagnostics":1}}`)}
	s := reviewServer(api)
	inst := testInstance(11)
	inst.ViewerSession = "login-a"
	s.Find = func() ([]discover.Instance, error) { return []discover.Instance{inst}, nil }
	server := mcp.NewServer(&mcp.Implementation{Name: "objects", Version: "1"}, nil)
	registerObjects(server, s, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
	ct, st := mcp.NewInMemoryTransports()
	ss, err := server.Connect(context.Background(), st, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = ss.Close() })
	client := mcp.NewClient(&mcp.Implementation{Name: "client", Version: "1"}, nil)
	cs, err := client.Connect(context.Background(), ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs, api
}
func TestObjectsRegisteredReads(t *testing.T) {
	for _, tc := range []struct {
		name, op string
		params   map[string]any
	}{
		{"object_contents_list", "contents", map[string]any{"object_id": buildID}},
		{"object_contents_list", "contents", map[string]any{"object_id": buildID, "snapshot_id": buildRevision, "offset": 50, "limit": 100}},
		{"object_script_get", "scriptGet", map[string]any{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision}},
		{"script_debug_poll", "debugPoll", map[string]any{"start": "now"}},
		{"script_debug_poll", "debugPoll", map[string]any{"cursor": buildRevision, "object_id": buildID, "limit": 100}},
	} {
		t.Run(tc.op, func(t *testing.T) {
			client, api := objectsFixture(t)
			tc.params["viewer_session"] = "login-a"
			result := call(t, client, tc.name, tc.params)
			if result.IsError {
				t.Fatal(textOf(result))
			}
			got := api.opsCopy()
			if len(got) != 3 || got[2] != "FSObjects."+tc.op {
				t.Fatal(got)
			}
			if _, ok := api.lastNamed["viewer_session"]; ok {
				t.Fatal("source routing leaked into tool params")
			}
		})
	}
}
func TestObjectsRejectMissingSourceAndStaleShapes(t *testing.T) {
	for _, tc := range []struct {
		name   string
		params map[string]any
	}{
		{"object_contents_list", map[string]any{"object_id": buildID}},
		{"object_contents_list", map[string]any{"viewer_session": "login-a", "object_id": buildID, "offset": 50}},
		{"object_contents_list", map[string]any{"viewer_session": "login-a", "object_id": buildID, "limit": 101}},
		{"object_script_get", map[string]any{"viewer_session": "login-a", "object_id": buildID, "item_id": contentItem, "snapshot_id": "not-a-snapshot"}},
		{"script_debug_poll", map[string]any{"viewer_session": "login-a"}},
		{"script_debug_poll", map[string]any{"viewer_session": "login-a", "start": "now", "cursor": buildRevision}},
	} {
		client, api := objectsFixture(t)
		result := call(t, client, tc.name, tc.params)
		if !result.IsError {
			t.Fatalf("accepted %s %#v", tc.name, tc.params)
		}
		if len(api.opsCopy()) != 0 {
			t.Fatal(api.opsCopy())
		}
	}
}
func TestObjectsPrivateConsentAndCapabilities(t *testing.T) {
	for _, mode := range []string{"deny", "missing-capability", "wrong-version"} {
		client, api := objectsFixture(t)
		if mode == "deny" {
			api.status = json.RawMessage(`{"permissions":{"script_debug_read":"deny"}}`)
		} else if mode == "wrong-version" {
			api.health = json.RawMessage(`{"capabilities":{"script_diagnostics":2}}`)
		} else {
			api.health = json.RawMessage(`{"capabilities":{}}`)
		}
		result := call(t, client, "script_debug_poll", map[string]any{"viewer_session": "login-a", "start": "now"})
		if !result.IsError {
			t.Fatal(mode)
		}
		for _, op := range api.opsCopy() {
			if op == "FSObjects.debugPoll" {
				t.Fatal("native access without consent/capability")
			}
		}
	}
}
