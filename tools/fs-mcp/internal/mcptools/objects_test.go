package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"strings"
	"testing"
)

func objectsFixture(t *testing.T) (*mcp.ClientSession, *preferencesTestAPI) {
	t.Helper()
	api := &preferencesTestAPI{fakeAPI: fakeAPI{status: json.RawMessage(`{"policy_generation":1,"permissions":{"read":"allow","world_read":"allow","task_inventory_read":"allow","script_debug_read":"allow","task_inventory_write":"ask","task_inventory_remove":"ask","script_run":"ask","content_edit":"ask","object_take":"ask","build_rez":"ask","object_interact":"ask","script_test":"ask","notifications_read":"allow","chat_send_nearby":"ask"}}`)}, health: json.RawMessage(`{"capabilities":{"task_inventory":1,"script_diagnostics":1,"task_scripts":1,"object_lifecycle":1,"object_interaction":1,"test_sessions":1}}`)}
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
func TestObjectsRegisteredMutations(t *testing.T) {
	for _, tc := range []struct {
		name, op string
		args     map[string]any
	}{
		{"object_contents_put", "put", map[string]any{"object_id": buildID, "snapshot_id": buildRevision, "source_item": contentItem, "client_request_id": contentOperation}},
		{"object_contents_rename", "rename", map[string]any{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation, "name": "game controller"}},
		{"object_contents_remove", "remove", map[string]any{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation}},
		{"object_script_reset", "scriptReset", map[string]any{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation}},
		{"object_script_set_running", "scriptRunning", map[string]any{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation, "running": false}},
		{"object_script_status", "scriptStatus", map[string]any{"object_id": buildID, "item_id": contentItem}},
		{"object_operation_status", "operationStatus", map[string]any{"operation_id": contentOperation}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			client, api := objectsFixture(t)
			tc.args["viewer_session"] = "login-a"
			r := call(t, client, tc.name, tc.args)
			if r.IsError {
				t.Fatal(textOf(r))
			}
			got := api.opsCopy()
			if len(got) != 3 || got[2] != "FSObjects."+tc.op {
				t.Fatal(got)
			}
			if tc.op == "scriptRunning" && api.lastNamed["running"] != false {
				t.Fatal("false running flag lost")
			}
		})
	}
	for _, write := range []bool{false, true} {
		client, api := objectsFixture(t)
		name := "object_script_preview"
		op := "scriptPreview"
		p := map[string]any{"viewer_session": "login-a", "object_id": buildID, "item_id": contentItem, "source_snapshot": buildRevision, "expected_asset_id": contentAsset, "content_hash": strings.Repeat("a", 64), "text": "default { state_entry() { llOwnerSay(\"hello\"); } }", "compiler": "mono"}
		if write {
			name = "object_script_save_compile"
			op = "scriptSave"
			p["running_mode"] = "stopped"
			p["client_request_id"] = contentOperation
		}
		r := call(t, client, name, p)
		if r.IsError {
			t.Fatal(textOf(r))
		}
		if got := api.opsCopy(); len(got) != 3 || got[2] != "FSObjects."+op {
			t.Fatal(got)
		}
	}
}
func TestObjectsLifecycleDispatch(t *testing.T) {
	for _, tc := range []struct {
		name, op string
		args     map[string]any
	}{
		{"object_take", "take", map[string]any{"object_id": buildID, "snapshot_id": buildRevision, "destination_folder": contentItem, "client_request_id": contentOperation}},
		{"object_take_copy", "takeCopy", map[string]any{"object_id": buildID, "snapshot_id": buildRevision, "destination_folder": contentItem, "client_request_id": contentOperation}},
		{"object_rez_identify", "rezIdentify", map[string]any{"object_id": buildID, "operation_id": contentOperation, "client_request_id": buildRevision}},
	} {
		client, api := objectsFixture(t)
		tc.args["viewer_session"] = "login-a"
		r := call(t, client, tc.name, tc.args)
		if r.IsError {
			t.Fatal(textOf(r))
		}
		if got := api.opsCopy(); len(got) != 3 || got[2] != "FSObjects."+tc.op {
			t.Fatal(got)
		}
	}
	client, api := objectsFixture(t)
	api.health = json.RawMessage(`{"capabilities":{"building_tools":1}}`)
	// A missing lifecycle capability must fail before any native acquisition.
	r := call(t, client, "object_take_copy", map[string]any{"viewer_session": "login-a", "object_id": buildID, "snapshot_id": buildRevision, "destination_folder": contentItem, "client_request_id": contentOperation})
	if !r.IsError {
		t.Fatal("missing lifecycle capability accepted")
	}
}

func TestObjectsMutationShapeAndPermissionGuards(t *testing.T) {
	for _, p := range []map[string]any{
		{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation},
		{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": "reused text", "running": true},
		{"object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation, "running": true, "name": "wrong extra field"},
	} {
		client, api := objectsFixture(t)
		p["viewer_session"] = "login-a"
		if r := call(t, client, "object_script_set_running", p); !r.IsError {
			t.Fatal("accepted bad runtime write", p)
		}
		if len(api.opsCopy()) != 0 {
			t.Fatal(api.opsCopy())
		}
	}
	client, api := objectsFixture(t)
	api.status = json.RawMessage(`{"permissions":{"task_inventory_read":"allow","world_read":"allow","task_inventory_remove":"deny"}}`)
	r := call(t, client, "object_contents_remove", map[string]any{"viewer_session": "login-a", "object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "client_request_id": contentOperation})
	if !r.IsError {
		t.Fatal("irreversible removal bypassed its own permission")
	}
	for _, op := range api.opsCopy() {
		if op == "FSObjects.remove" {
			t.Fatal("removal sent despite denial")
		}
	}
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
