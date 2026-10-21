package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/viewerapi"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"math"
	"regexp"
)

var poseUUID = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`)
var poseJoint = regexp.MustCompile(`^m[A-Za-z0-9_]{1,63}$`)
var poseName = regexp.MustCompile(`^[A-Za-z0-9_-]{1,64}$`)
var poseHash = regexp.MustCompile(`^[a-f0-9]{64}$`)

func validPoseUUID(id string) bool {
	return poseUUID.MatchString(id) && id != "00000000-0000-0000-0000-000000000000"
}
func registerPose(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	registerSharedPose(server, state, readOnly, mutating)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_status", Description: "Read a self-targeted local rotation transaction or a source-bound operation receipt. Requires explicit current-account avatar target and opted-in Pose Read. Never returns simulator login/session secrets.", Annotations: withTitle(readOnly, "Pose transaction status")}, state.poseStatus)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_joints", Description: "Read supported non-root local joint names and quaternion rotations for the exact current-account avatar. Pose Read is separate from inventory Read. No animesh, control avatars or world/root transforms.", Annotations: withTitle(readOnly, "Read local pose joints")}, state.poseJoints)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_presets", Description: "List bounded native pose-store preset IDs, exact file revisions and rotation-only compatibility. Returns opaque IDs rather than file paths. Wider native motion/position/scale presets are refused by the audited loader.", Annotations: withTitle(readOnly, "List saved pose presets")}, state.posePresets)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_start", Description: "Start a private rotation-only transaction for the exact current-account avatar using pose_session and expected_revision from pose_status. Requires active character, Pose Read and viewer-owned Pose Edit approval. Refuses existing native/manual/shared ownership. Does not stop fidgets, change default animations or touch native manual undo history. Effects are local to this viewer.", Annotations: withTitle(mutating, "Start local pose transaction")}, state.poseStart)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_update", Description: "Apply 1-64 named local unit quaternion rotations [x,y,z,w] to this source's existing self-pose transaction, with exact pose_session and expected_revision. Native approval freezes cumulative affected joints. Captured authority is checked before each timed rotation; background/pause/login/region/manual ownership changes stop only the owned motion. No position, scale, root, mirror or locked-descendant expansion.", Annotations: withTitle(mutating, "Update local joint rotations")}, state.poseUpdate)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_undo", Description: "Undo this transaction's latest rotation change with exact pose_session/revision. Never consumes native manual Poser undo or resets another editor's work. Requires active Pose Edit and Pose Read.", Annotations: withTitle(mutating, "Undo owned pose change")}, state.poseUndo)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_reset", Description: "Release this source's owned joint rotation overlay, retaining its private undo history. Requires exact session/revision and active Pose Edit; no position/scale restoration or manual joint reset.", Annotations: withTitle(mutating, "Reset owned rotation overlay")}, state.poseReset)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_stop", Description: "Stop only this source's private rotation motion with exact pose_session/revision. Native manual and shared Poser motions remain independently owned. Requires active Pose Edit and Pose Read.", Annotations: withTitle(mutating, "Stop owned pose transaction")}, state.poseStop)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_load", Description: "Load a listed native rotation-only preset using exact preset_id/preset_revision and current pose_session/expected_revision. Freezes bounded file bytes before viewer approval. Never invokes the native general pose loader, restores broader motion state, or accesses arbitrary paths. Requires active Pose Edit and Pose Read.", Annotations: withTitle(mutating, "Load saved local rotations")}, state.poseLoad)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_save", Description: "Save the owned rotation overlay as a new native version 8 pose file with confined name and expected_preset_revision=absent. Refuses collisions and arbitrary paths; never overwrites files. Requires active Pose Save and Pose Read. No publishing occurs.", Annotations: withTitle(mutating, "Save new rotation preset")}, state.poseSave)
}

type poseReadArgs struct {
	Target        string `json:"target" jsonschema:"exact current-account avatar UUID; no implicit self target"`
	OperationID   string `json:"operation_id,omitempty"`
	ViewerSession string `json:"viewer_session,omitempty"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type poseMutationArgs struct {
	Target                 string               `json:"target"`
	PoseSession            string               `json:"pose_session" jsonschema:"opaque pose_session from pose_status; not a simulator session UUID"`
	ExpectedRevision       int                  `json:"expected_revision" jsonschema:"exact revision from pose_status"`
	Rotations              map[string][]float64 `json:"rotations,omitempty"`
	PresetID               string               `json:"preset_id,omitempty"`
	PresetRevision         string               `json:"preset_revision,omitempty"`
	Name                   string               `json:"name,omitempty"`
	ExpectedPresetRevision string               `json:"expected_preset_revision,omitempty"`
	ViewerSession          string               `json:"viewer_session,omitempty"`
	ViewerPID              int                  `json:"viewer_pid,omitempty"`
}

func (s *Server) poseCall(ctx context.Context, pid int, op string, params map[string]any, write bool) (*mcp.CallToolResult, any, error) {
	return s.poseFeatureCall(ctx, pid, op, params, write, false)
}
func (s *Server) poseFeatureCall(ctx context.Context, pid int, op string, params map[string]any, write, shared bool) (*mcp.CallToolResult, any, error) {
	capability := "pose_tools"
	if shared {
		capability = "shared_pose_tools"
	}
	classes := []string{viewerapi.ClassPoseRead}
	if write {
		if shared {
			classes = append(classes, viewerapi.ClassPoseShare)
		} else if op == "save" {
			classes = append(classes, viewerapi.ClassPoseSave)
		} else {
			classes = append(classes, viewerapi.ClassPoseEdit)
		}
	}
	api, policy, errRes := s.openClasses(ctx, pid, classes)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present || policy.Capabilities[capability] != 1 {
		return errorResult("unsupported_feature", "Selected viewer does not support "+capability+" version 1; no fallback was attempted", nil)
	}
	raw, err := api.CallNamed(ctx, "LocalAssistant", "health", map[string]any{"diagnostic": true})
	if err != nil {
		return apiError(err)
	}
	var health struct {
		Ready        bool            `json:"bridge_ready"`
		Session      string          `json:"viewer_session"`
		Account      string          `json:"account"`
		Grid         string          `json:"grid"`
		Capabilities map[string]int  `json:"capabilities"`
		APIs         map[string]bool `json:"apis_ready"`
	}
	if len(raw) > 65536 || json.Unmarshal(raw, &health) != nil || !health.Ready || !health.APIs["FSPose"] || health.Capabilities[capability] != 1 {
		return errorResult("unsupported_feature", "Fresh viewer health does not expose a ready FSPose service", nil)
	}
	if selected, ok := requestIdentity(ctx); ok && selected.ViewerSession != "" && (selected.ViewerSession != health.Session || selected.Account != health.Account || selected.Grid != health.Grid) {
		return errorResult("session_changed", "Selected account/grid/viewer connection changed before posing", nil)
	}
	if target, ok := params["target"].(string); ok && (!shared || op == "startSharing" || op == "stopSharing") && health.Account != "" && target != health.Account {
		return errorResult("invalid_target", "Own posing requires the current account avatar target", nil)
	}
	return apiResult(api.CallNamed(ctx, "FSPose", op, params))
}
func (s *Server) poseRead(ctx context.Context, args poseReadArgs, op string) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validPoseUUID(args.Target) || (args.OperationID != "" && (op != "status" || !validPoseUUID(args.OperationID))) {
		return errorResult("invalid_args", "An exact avatar target and valid source-bound operation UUID are required", nil)
	}
	params := map[string]any{"target": args.Target}
	if args.OperationID != "" {
		params["operation_id"] = args.OperationID
	}
	return s.poseCall(ctx, args.ViewerPID, op, params, false)
}
func (s *Server) poseStatus(ctx context.Context, _ *mcp.CallToolRequest, args poseReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseRead(ctx, args, "status")
}
func (s *Server) poseJoints(ctx context.Context, _ *mcp.CallToolRequest, args poseReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseRead(ctx, args, "joints")
}
func (s *Server) posePresets(ctx context.Context, _ *mcp.CallToolRequest, args poseReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseRead(ctx, args, "presets")
}
func validPoseRotations(rotations map[string][]float64) bool {
	if len(rotations) < 1 || len(rotations) > 64 {
		return false
	}
	for name, q := range rotations {
		if !poseJoint.MatchString(name) || name == "mPelvis" || name == "mRoot" || name == "mAvatarRoot" || len(q) != 4 {
			return false
		}
		norm := 0.
		for _, v := range q {
			if math.IsNaN(v) || math.IsInf(v, 0) || math.Abs(v) > 1.001 {
				return false
			}
			norm += v * v
		}
		if math.Abs(norm-1) > .002 {
			return false
		}
	}
	return true
}
func (s *Server) poseMutate(ctx context.Context, args poseMutationArgs, op string) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validPoseUUID(args.Target) || !validPoseUUID(args.PoseSession) || args.ExpectedRevision < 0 {
		return errorResult("invalid_args", "An explicit self avatar target, exact pose_session and nonnegative expected_revision are required", nil)
	}
	params := map[string]any{"target": args.Target, "pose_session": args.PoseSession, "expected_revision": args.ExpectedRevision}
	switch op {
	case "update":
		if !validPoseRotations(args.Rotations) {
			return errorResult("invalid_args", "rotations must contain 1-64 known non-root joint unit quaternions [x,y,z,w]", nil)
		}
		params["rotations"] = args.Rotations
	case "load":
		if len(args.PresetID) != 71 || args.PresetID[:7] != "preset-" || !poseHash.MatchString(args.PresetID[7:]) || !poseHash.MatchString(args.PresetRevision) {
			return errorResult("invalid_args", "Use exact preset_id and preset_revision from pose_presets", nil)
		}
		params["preset_id"] = args.PresetID
		params["preset_revision"] = args.PresetRevision
	case "save":
		if !poseName.MatchString(args.Name) || args.ExpectedPresetRevision != "absent" {
			return errorResult("invalid_args", "Saving requires a new confined name and expected_preset_revision=absent", nil)
		}
		params["name"] = args.Name
		params["expected_preset_revision"] = "absent"
	}
	return s.poseCall(ctx, args.ViewerPID, op, params, true)
}
func (s *Server) poseStart(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "start")
}
func (s *Server) poseUpdate(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "update")
}
func (s *Server) poseUndo(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "undo")
}
func (s *Server) poseReset(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "reset")
}
func (s *Server) poseStop(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "stop")
}
func (s *Server) poseLoad(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "load")
}
func (s *Server) poseSave(ctx context.Context, _ *mcp.CallToolRequest, args poseMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseMutate(ctx, args, "save")
}
