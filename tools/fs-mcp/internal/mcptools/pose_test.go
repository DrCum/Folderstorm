package mcptools

import (
	"context"
	"encoding/json"
	"fmt"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"math"
	"strings"
	"testing"
)

const poseTarget = "11111111-1111-1111-1111-111111111111"
const poseSession = "22222222-2222-2222-2222-222222222222"

type poseTestAPI struct {
	fakeAPI
	health json.RawMessage
}

func (a *poseTestAPI) CallNamed(ctx context.Context, api, op string, params map[string]any) (json.RawMessage, error) {
	if api == "LocalAssistant" && op == "health" {
		a.record(api + "." + op)
		if a.health != nil {
			return a.health, nil
		}
		return json.RawMessage(`{"bridge_ready":true,"account":"11111111-1111-1111-1111-111111111111","apis_ready":{"FSPose":true},"capabilities":{"pose_tools":1}}`), nil
	}
	return a.fakeAPI.CallNamed(ctx, api, op, params)
}
func posePolicy(read, edit string, version int) json.RawMessage {
	return json.RawMessage(fmt.Sprintf(`{"permissions":{"pose_read":%q,"pose_edit":%q,"pose_save":%q},"capabilities":{"pose_tools":%d}}`, read, edit, edit, version))
}
func poseClient(t *testing.T, s *Server) *mcp.ClientSession {
	t.Helper()
	server := mcp.NewServer(&mcp.Implementation{Name: "pose-test", Version: "1"}, nil)
	registerPose(server, s, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
	ct, st := mcp.NewInMemoryTransports()
	if _, err := server.Connect(context.Background(), st, nil); err != nil {
		t.Fatal(err)
	}
	client := mcp.NewClient(&mcp.Implementation{Name: "pose-client", Version: "1"}, nil)
	session, err := client.Connect(context.Background(), ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = session.Close() })
	return session
}
func poseMutationInput() map[string]any {
	return map[string]any{"target": poseTarget, "pose_session": poseSession, "expected_revision": 0}
}
func TestPoseRegisteredDispatch(t *testing.T) {
	for _, op := range []string{"status", "joints", "presets", "start", "update", "undo", "reset", "stop", "load", "save"} {
		t.Run(op, func(t *testing.T) {
			args := poseMutationInput()
			if op == "status" || op == "joints" || op == "presets" {
				args = map[string]any{"target": poseTarget}
			}
			if op == "update" {
				args["rotations"] = map[string]any{"mHead": []float64{0, 0, 0, 1}}
			}
			if op == "load" {
				args["preset_id"] = "preset-" + strings.Repeat("a", 64)
				args["preset_revision"] = strings.Repeat("b", 64)
			}
			if op == "save" {
				args["name"] = "my_pose"
				args["expected_preset_revision"] = "absent"
			}
			api := &poseTestAPI{fakeAPI: fakeAPI{status: posePolicy("allow", "ask", 1)}}
			result := call(t, poseClient(t, reviewServer(api)), "pose_"+op, args)
			if result.IsError || !contains(api.opsCopy(), "FSPose."+op) || !contains(api.opsCopy(), "LocalAssistant.health") {
				t.Fatalf("registered pose dispatch failed: %s", textOf(result))
			}
			if api.lastNamed["target"] != poseTarget {
				t.Fatal("target changed")
			}
		})
	}
}
func TestPosePermissionAndCapabilityGates(t *testing.T) {
	for _, tt := range []struct {
		read, edit  string
		version     int
		write, deny bool
	}{{"allow", "ask", 1, true, false}, {"deny", "allow", 1, true, true}, {"allow", "deny", 1, true, true}, {"allow", "deny", 1, false, false}, {"deny", "allow", 1, false, true}, {"allow", "allow", 0, true, true}, {"allow", "allow", 2, false, true}} {
		api := &poseTestAPI{fakeAPI: fakeAPI{status: posePolicy(tt.read, tt.edit, tt.version)}}
		result, _, _ := reviewServer(api).poseCall(context.Background(), 11, "status", map[string]any{"target": poseTarget}, tt.write)
		if result.IsError != tt.deny || contains(api.opsCopy(), "FSPose.status") == tt.deny {
			t.Fatalf("pose gate mismatch %+v", tt)
		}
	}
}
func TestPoseNoRetargetingOrStaleHealth(t *testing.T) {
	for _, health := range []string{`{}`, `{"bridge_ready":false}`, `{"bridge_ready":true,"apis_ready":{"FSPose":true},"capabilities":{"pose_tools":2}}`, `{"bridge_ready":true,"account":"33333333-3333-3333-3333-333333333333","apis_ready":{"FSPose":true},"capabilities":{"pose_tools":1}}`} {
		api := &poseTestAPI{fakeAPI: fakeAPI{status: posePolicy("allow", "ask", 1)}, health: json.RawMessage(health)}
		result, _, _ := reviewServer(api).poseCall(context.Background(), 11, "start", poseMutationInput(), true)
		if !result.IsError || contains(api.opsCopy(), "FSPose.start") {
			t.Fatal("stale health or other account accepted")
		}
	}
}
func TestPoseRotationAndPathBounds(t *testing.T) {
	if !validPoseRotations(map[string][]float64{"mHead": {0, 0, 0, 1}}) {
		t.Fatal("valid unit quaternion refused")
	}
	for _, rotations := range []map[string][]float64{{}, {"mPelvis": {0, 0, 0, 1}}, {"mRoot": {0, 0, 0, 1}}, {"mHead": {0, 0, 0, 0}}, {"mHead": {math.NaN(), 0, 0, 1}}, {"mHead": {1, 1, 1, 1}}, {"/tmp/joint": {0, 0, 0, 1}}} {
		if validPoseRotations(rotations) {
			t.Fatalf("unsafe rotations accepted: %+v", rotations)
		}
	}
	for _, name := range []string{"../outside", "/tmp/file", "https://example.test", "pose.xml"} {
		api := &poseTestAPI{fakeAPI: fakeAPI{status: posePolicy("allow", "allow", 1)}}
		res, _, _ := reviewServer(api).poseSave(context.Background(), nil, poseMutationArgs{Target: poseTarget, PoseSession: poseSession, Name: name, ExpectedPresetRevision: "absent", ViewerPID: 11})
		if !res.IsError || contains(api.opsCopy(), "FSPose.save") {
			t.Fatalf("unsafe preset path accepted %s", name)
		}
	}
}
func TestPoseSourceReceiptUsesDedicatedRead(t *testing.T) {
	api := &poseTestAPI{fakeAPI: fakeAPI{status: posePolicy("deny", "allow", 1)}}
	result, _, _ := reviewServer(api).poseStatus(context.Background(), nil, poseReadArgs{Target: poseTarget, OperationID: poseSession, ViewerPID: 11})
	if !result.IsError || contains(api.opsCopy(), "FSPose.status") {
		t.Fatal("private receipt bypassed Pose Read")
	}
}
