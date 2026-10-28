package mcptools

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestObjectInteractionRegisteredDispatch(t *testing.T) {
	for _, tc := range []struct {
		name, op string
		p        map[string]any
	}{
		{"object_touch", "touch", map[string]any{"object_id": buildID, "client_request_id": contentOperation}},
		{"object_touch", "touch", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "face": 0, "uv": []float64{.5, .5}, "local_point": []float64{.1, 0, 0}}},
		{"object_dialog_list", "dialogList", map[string]any{"object_id": buildID}},
		{"object_dialog_reply", "dialogReply", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "notification_id": contentItem, "expected_dialog": strings.Repeat("a", 64), "button_index": 0}},
		{"object_dialog_reply", "dialogReply", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "notification_id": contentItem, "expected_dialog": strings.Repeat("a", 64), "text": ""}},
		{"object_channel_send", "channelSend", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "channel": -900123, "text": "start"}},
		{"script_test_status", "testStatus", map[string]any{"run_id": contentOperation}},
		{"script_test_stop", "testStop", map[string]any{"run_id": contentOperation}},
	} {
		client, api := objectsFixture(t)
		tc.p["viewer_session"] = "login-a"
		r := call(t, client, tc.name, tc.p)
		if r.IsError {
			t.Fatalf("%s: %s", tc.name, textOf(r))
		}
		if got := api.opsCopy(); len(got) != 3 || got[2] != "FSObjects."+tc.op {
			t.Fatal(got)
		}
		if tc.op == "dialogReply" {
			if value, ok := tc.p["text"]; ok && api.lastNamed["text"] != value {
				t.Fatal("blank textbox changed")
			}
		}
	}
}
func TestObjectInteractionShapeGuards(t *testing.T) {
	for _, tc := range []struct {
		name string
		p    map[string]any
	}{
		{"object_touch", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "face": 0}},
		{"object_touch", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "face": 64, "uv": []float64{0, 0}, "local_point": []float64{0, 0, 0}}},
		{"object_dialog_reply", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "notification_id": contentItem, "expected_dialog": strings.Repeat("a", 64), "button_index": 0, "text": "ambiguous"}},
		{"object_dialog_reply", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "notification_id": contentItem, "expected_dialog": strings.Repeat("a", 64), "button_index": -2}},
		{"object_channel_send", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "channel": 0, "text": "not public"}},
		{"object_channel_send", map[string]any{"object_id": buildID, "client_request_id": contentOperation, "channel": 2147483647, "text": "not debug"}},
	} {
		client, api := objectsFixture(t)
		tc.p["viewer_session"] = "login-a"
		if r := call(t, client, tc.name, tc.p); !r.IsError {
			t.Fatalf("accepted %s %#v", tc.name, tc.p)
		}
		if len(api.opsCopy()) != 0 {
			t.Fatal(api.opsCopy())
		}
	}
}
func TestScriptRecipesRegisteredAndScoped(t *testing.T) {
	for _, step := range []map[string]any{
		{"action": "touch", "object_id": buildID},
		{"action": "reset", "object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision},
		{"action": "running", "object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision, "running": false},
		{"action": "send_channel", "object_id": buildID, "channel": -900123, "text": "start"},
		{"action": "wait_output", "object_id": buildID, "text": "READY", "expected": true, "timeout_seconds": 10},
		{"action": "assert_running", "object_id": buildID, "item_id": contentItem, "expected": false, "timeout_seconds": 10},
		{"action": "assert_contents", "object_id": buildID, "item_id": contentItem, "expected": true, "timeout_seconds": 10},
		{"action": "delay", "seconds": 60},
	} {
		client, api := objectsFixture(t)
		p := map[string]any{"viewer_session": "login-a", "client_request_id": contentOperation, "object_ids": []string{buildID}, "steps": []any{step}, "timeout_seconds": 600}
		if r := call(t, client, "script_test_begin", p); r.IsError {
			t.Fatal(step, textOf(r))
		}
		if got := api.opsCopy(); len(got) != 3 || got[2] != "FSObjects.testBegin" {
			t.Fatal(got)
		}
	}
	for _, step := range []map[string]any{
		{"action": "touch", "object_id": contentItem},
		{"action": "touch", "object_id": buildID, "face": 0, "uv": []float64{2, 0}, "local_point": []float64{0, 0, 0}},
		{"action": "running", "object_id": buildID, "item_id": contentItem, "snapshot_id": buildRevision},
		{"action": "send_channel", "object_id": buildID, "channel": 0, "text": "forbidden"},
		{"action": "send_channel", "object_id": buildID, "channel": -1, "text": strings.Repeat("a", 1024)},
		{"action": "touch", "object_id": buildID, "text": "unrelated field"},
		{"action": "inventory_delete", "object_id": buildID},
	} {
		client, api := objectsFixture(t)
		p := map[string]any{"viewer_session": "login-a", "client_request_id": contentOperation, "object_ids": []string{buildID}, "steps": []any{step}, "timeout_seconds": 600}
		if r := call(t, client, "script_test_begin", p); !r.IsError {
			t.Fatal("accepted", step)
		}
		if len(api.opsCopy()) != 0 {
			t.Fatal(api.opsCopy())
		}
	}
}
func TestScriptStopDoesNotDependOnOtherPermissions(t *testing.T) {
	client, api := objectsFixture(t)
	api.status = json.RawMessage(`{"permissions":{"world_read":"deny","script_debug_read":"deny","script_test":"deny","object_interact":"deny"}}`)
	r := call(t, client, "script_test_stop", map[string]any{"viewer_session": "login-a", "run_id": contentOperation})
	if r.IsError {
		t.Fatal(textOf(r))
	}
	if got := api.opsCopy(); len(got) != 3 || got[2] != "FSObjects.testStop" {
		t.Fatal(got)
	}
}
