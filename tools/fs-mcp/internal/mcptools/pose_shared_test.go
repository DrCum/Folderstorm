package mcptools

import (
	"context"
	"encoding/json"
	"fmt"
	"fs-mcp/internal/discover"
	"testing"
)

const sharedTarget = "33333333-3333-3333-3333-333333333333"

type sharedPoseTestAPI struct {
	fakeAPI
	health  json.RawMessage
	failure error
	payload json.RawMessage
}

func (a *sharedPoseTestAPI) CallNamed(ctx context.Context, api, op string, params map[string]any) (json.RawMessage, error) {
	if api == "LocalAssistant" && op == "health" {
		a.record(api + "." + op)
		if a.health != nil {
			return a.health, nil
		}
		return json.RawMessage(`{"bridge_ready":true,"account":"11111111-1111-1111-1111-111111111111","apis_ready":{"FSPose":true},"capabilities":{"shared_pose_tools":1}}`), nil
	}
	if api == "FSPose" && a.failure != nil {
		a.record(api + "." + op)
		return nil, a.failure
	}
	if api == "FSPose" && a.payload != nil {
		a.record(api + "." + op)
		a.lastNamed = params
		return a.payload, nil
	}
	return a.fakeAPI.CallNamed(ctx, api, op, params)
}
func sharedPosePolicy(read, share string, version int) json.RawMessage {
	return json.RawMessage(fmt.Sprintf(`{"permissions":{"pose_read":%q,"pose_share":%q,"pose_edit":"deny","pose_save":"deny"},"capabilities":{"shared_pose_tools":%d}}`, read, share, version))
}
func sharedInput() map[string]any {
	return map[string]any{"target": sharedTarget, "session_generation": poseSession}
}
func TestPoseSharedRegisteredDispatch(t *testing.T) {
	cases := []struct{ name, op string }{
		{"sessions", "sharedSessions"}, {"grants", "sharedGrants"}, {"joints", "sharedSessions"}, {"update", "sharedEdit"}, {"cancel", "sharedEdit"}, {"operation_status", "sharedSessions"},
	}
	for _, tt := range cases {
		t.Run(tt.name, func(t *testing.T) {
			args := sharedInput()
			switch tt.name {
			case "sessions", "grants":
				args = map[string]any{}
			case "joints":
				args = map[string]any{"target": sharedTarget}
			case "update":
				args["rotations"] = map[string]any{"mHead": []float64{0, 0, 1, 0}}
				args["expected_joint_revisions"] = map[string]any{"mHead": 3}
			case "cancel":
				args["operation_id"] = poseTarget
			case "operation_status":
				args = map[string]any{"operation_id": poseTarget}
			}
			api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("allow", "ask", 1)}}
			result := call(t, poseClient(t, reviewServer(api)), "pose_shared_"+tt.name, args)
			if result.IsError || !contains(api.opsCopy(), "FSPose."+tt.op) {
				t.Fatalf("shared dispatch failed: %s", textOf(result))
			}
			if tt.name == "cancel" && api.lastNamed["cancel"] != true {
				t.Fatal("cancel was not operation-specific")
			}
		})
	}
	for _, op := range []string{"start", "stop"} {
		t.Run(op+"_sharing", func(t *testing.T) {
			api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("allow", "ask", 1)}}
			args := sharedInput()
			args["target"] = poseTarget
			result := call(t, poseClient(t, reviewServer(api)), "pose_"+op+"_sharing", args)
			native := op + "Sharing"
			if result.IsError || !contains(api.opsCopy(), "FSPose."+native) {
				t.Fatalf("publish dispatch failed: %s", textOf(result))
			}
		})
	}
}
func TestPoseSharedDedicatedGates(t *testing.T) {
	for _, tt := range []struct {
		read, share   string
		version       int
		write, denied bool
	}{
		{"allow", "ask", 1, true, false}, {"deny", "allow", 1, true, true}, {"allow", "deny", 1, true, true}, {"allow", "deny", 1, false, false}, {"deny", "allow", 1, false, true}, {"allow", "allow", 0, true, true}, {"allow", "allow", 2, false, true},
	} {
		api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy(tt.read, tt.share, tt.version)}}
		result, _, _ := reviewServer(api).poseFeatureCall(context.Background(), 11, "sharedSessions", map[string]any{}, tt.write, true)
		if result.IsError != tt.denied || contains(api.opsCopy(), "FSPose.sharedSessions") == tt.denied {
			t.Fatalf("wrong native share gate: %+v", tt)
		}
	}
	// Own pose capability cannot be used as a fallback for native sharing.
	api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: posePolicy("allow", "allow", 1)}}
	result, _, _ := reviewServer(api).poseFeatureCall(context.Background(), 11, "sharedSessions", map[string]any{}, false, true)
	if !result.IsError || contains(api.opsCopy(), "FSPose.sharedSessions") {
		t.Fatal("shared feature fell back to own posing")
	}
}
func TestPoseSharedTargetAndRevisionBounds(t *testing.T) {
	for _, args := range []poseSharedMutationArgs{
		{Target: sharedTarget, SessionGeneration: poseSession, Rotations: map[string][]float64{"mHead": {0, 0, 0, 1}}, ExpectedJointRevisions: map[string]int{"mNeck": 0}},
		{Target: sharedTarget, SessionGeneration: poseSession, Rotations: map[string][]float64{"mHead": {0, 0, 0, 1}}, ExpectedJointRevisions: map[string]int{"mHead": -1}},
		{Target: sharedTarget, SessionGeneration: poseSession, Rotations: map[string][]float64{"mPelvis": {0, 0, 0, 1}}, ExpectedJointRevisions: map[string]int{"mPelvis": 0}},
		{Target: sharedTarget, SessionGeneration: "bad"},
	} {
		api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("allow", "allow", 1)}}
		result, _, _ := reviewServer(api).poseSharedUpdate(context.Background(), nil, args)
		if !result.IsError || contains(api.opsCopy(), "FSPose.sharedEdit") {
			t.Fatal("invalid effect set dispatched")
		}
	}
	api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("allow", "allow", 1)}}
	result, _, _ := reviewServer(api).poseStartSharing(context.Background(), nil, poseSharedMutationArgs{Target: sharedTarget, SessionGeneration: poseSession})
	if !result.IsError || contains(api.opsCopy(), "FSPose.startSharing") {
		t.Fatal("publishing retargeted another avatar")
	}
}
func TestPoseSharedNoReplayAndHonestReceipts(t *testing.T) {
	for _, tt := range []struct {
		payload json.RawMessage
		failure error
	}{
		{payload: json.RawMessage(`{"state":"submitted_unknown","uncertain":true,"owner_accepted":false}`)},
		{payload: json.RawMessage(`{"state":"reserved","owner_accepted":false}`)},
		{failure: fmt.Errorf("transport uncertain after submit")},
	} {
		api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("allow", "ask", 1)}, payload: tt.payload, failure: tt.failure}
		result, _, _ := reviewServer(api).poseSharedUpdate(context.Background(), nil, poseSharedMutationArgs{Target: sharedTarget, SessionGeneration: poseSession, Rotations: map[string][]float64{"mHead": {0, 0, 0, 1}}, ExpectedJointRevisions: map[string]int{"mHead": 0}})
		count := 0
		for _, op := range api.opsCopy() {
			if op == "FSPose.sharedEdit" {
				count++
			}
		}
		if count != 1 {
			t.Fatalf("uncertain operation replayed: %v", api.opsCopy())
		}
		if tt.failure == nil && (result.IsError || textOf(result) == "") {
			t.Fatal("receipt was lost")
		}
	}
}
func TestPoseSharedReceiptRequiresPrivateRead(t *testing.T) {
	api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("deny", "allow", 1)}}
	result, _, _ := reviewServer(api).poseSharedOperationStatus(context.Background(), nil, poseSharedReadArgs{OperationID: poseSession})
	if !result.IsError || contains(api.opsCopy(), "FSPose.sharedSessions") {
		t.Fatal("operation receipt bypassed Pose Read")
	}
}

func TestPoseSharedFreshHealthIdentityBinding(t *testing.T) {
	for _, field := range []string{"viewer_session", "account", "grid"} {
		identity := map[string]any{"viewer_session": "viewer-one", "account": poseTarget, "grid": "grid-one", "bridge_ready": true, "apis_ready": map[string]bool{"FSPose": true}, "capabilities": map[string]int{"shared_pose_tools": 1}}
		identity[field] = "replacement"
		raw, _ := json.Marshal(identity)
		api := &sharedPoseTestAPI{fakeAPI: fakeAPI{status: sharedPosePolicy("allow", "ask", 1)}, health: raw}
		state := reviewServer(api)
		state.Find = func() ([]discover.Instance, error) {
			return []discover.Instance{{PID: 11, ViewerSession: "viewer-one", Account: poseTarget, Grid: "grid-one", Token: "bound"}}, nil
		}
		result, _, _ := state.poseSharedSessions(context.Background(), nil, poseSharedReadArgs{ViewerSession: "viewer-one"})
		if !result.IsError || contains(api.opsCopy(), "FSPose.sharedSessions") {
			t.Fatalf("fresh %s mismatch adopted", field)
		}
	}
}
