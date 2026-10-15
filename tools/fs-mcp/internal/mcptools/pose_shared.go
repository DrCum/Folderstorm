package mcptools

import (
	"context"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// This typed adapter cannot request, accept or synthesize native owner grants.
// An MCP Ask approves only the selected source's exact proposed rotations.
func registerSharedPose(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "pose_shared_sessions", Description: "List at most 64 nearby native shared-pose sessions and opaque local generations. Requires Pose Read and shared_pose_tools version 1. Returns no private native consent tokens or simulator session UUIDs. Does not invite anyone, enable sharing, or open a Poser.", Annotations: withTitle(readOnly, "List native shared poses")}, state.poseSharedSessions)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_shared_grants", Description: "List existing native owner-granted shared-pose editing sessions. Native owner consent comes only from the ordinary viewer request/notification flow; an MCP confirmation cannot supply it. Requires Pose Read.", Annotations: withTitle(readOnly, "Read native pose grants")}, state.poseSharedGrants)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_shared_joints", Description: "Read the exact nearby target's observed native shared-pose local rotations, per-joint revisions and editability. Remote reservations are confirmed only by an owner reservation request. Returns opaque session/grant generations rather than secrets. Requires Pose Read.", Annotations: withTitle(readOnly, "Read shared joint revisions")}, state.poseSharedJoints)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_shared_update", Description: "Reserve 1-64 exact joints under the target owner's existing native grant and send one final local quaternion edit with exact observed joint revisions. Requires active Pose Share and Pose Read; native confirmation freezes the effect set. Rejects mirrors, world locks and locked-descendant expansion. Preserves position, scale, priority, enable and native per-joint undo. Operation receipt distinguishes pending/reserved/submitted from owner_accepted. No uncertain retry or automatic rollback.", Annotations: withTitle(mutating, "Request owner-authorized joint rotations")}, state.poseSharedUpdate)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_shared_cancel", Description: "Cancel only this source's exact shared-pose operation, using current target/session generation. Releases only its own unsubmitted native lease when possible; a submitted edit may remain uncertain. Does not stop or reset the owner's Poser, change mouse capture, undo manual work or replay an edit. Requires active Pose Share and Pose Read.", Annotations: withTitle(mutating, "Cancel owned shared-pose operation")}, state.poseSharedCancel)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_shared_operation_status", Description: "Read a private source-bound native shared-pose receipt. owner_accepted requires an exact native owner acknowledgement; a bridge transport acknowledgement alone is insufficient. Reconnect/source changes invalidate access. Requires Pose Read.", Annotations: withTitle(readOnly, "Read shared-pose operation receipt")}, state.poseSharedOperationStatus)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_start_sharing", Description: "Publish the current account's already active ordinary native Poser session to native shared-pose peers, including its existing native position/scale state. Requires explicit self target/session generation and active Pose Share plus Pose Read. Never starts a native Poser or publishes a private assistant transaction implicitly. Does not approve invitations or enable the sharing preference. Native owner consent remains separate.", Annotations: withTitle(mutating, "Publish existing native pose")}, state.poseStartSharing)
	mcp.AddTool(server, &mcp.Tool{Name: "pose_stop_sharing", Description: "End the exact current self native publishing/grant session, preserving the owner's native Poser motion and manual state. Requires explicit self target/session generation and active Pose Share plus Pose Read. Remote synchronization is unconfirmed until native transport/peer timeout; no global reset or retry.", Annotations: withTitle(mutating, "Stop exact native pose publication")}, state.poseStopSharing)
}

type poseSharedReadArgs struct {
	Target        string `json:"target,omitempty"`
	OperationID   string `json:"operation_id,omitempty"`
	ViewerSession string `json:"viewer_session,omitempty"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type poseSharedMutationArgs struct {
	Target                 string               `json:"target" jsonschema:"exact ordinary nearby avatar UUID; publishing requires current account avatar"`
	SessionGeneration      string               `json:"session_generation" jsonschema:"opaque local generation from pose_shared_sessions or pose_shared_joints, never a simulator session UUID"`
	Rotations              map[string][]float64 `json:"rotations,omitempty"`
	ExpectedJointRevisions map[string]int       `json:"expected_joint_revisions,omitempty"`
	OperationID            string               `json:"operation_id,omitempty"`
	ViewerSession          string               `json:"viewer_session,omitempty"`
	ViewerPID              int                  `json:"viewer_pid,omitempty"`
}

func (s *Server) poseSharedRead(ctx context.Context, args poseSharedReadArgs, op string, exact, receipt bool) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if (exact || args.Target != "") && !validPoseUUID(args.Target) || receipt && !validPoseUUID(args.OperationID) || !receipt && args.OperationID != "" {
		return errorResult("invalid_args", "Use an exact target UUID or a source-bound operation UUID for the selected read tool", nil)
	}
	params := map[string]any{}
	if args.Target != "" {
		params["target"] = args.Target
	}
	if receipt {
		params["operation_id"] = args.OperationID
	}
	return s.poseFeatureCall(ctx, args.ViewerPID, op, params, false, true)
}
func (s *Server) poseSharedSessions(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedRead(ctx, args, "sharedSessions", false, false)
}
func (s *Server) poseSharedGrants(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedRead(ctx, args, "sharedGrants", false, false)
}
func (s *Server) poseSharedJoints(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedRead(ctx, args, "sharedSessions", true, false)
}
func (s *Server) poseSharedOperationStatus(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedReadArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedRead(ctx, args, "sharedSessions", false, true)
}
func (s *Server) poseSharedMutate(ctx context.Context, args poseSharedMutationArgs, op string, cancel bool) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validPoseUUID(args.Target) || !validPoseUUID(args.SessionGeneration) {
		return errorResult("invalid_args", "Explicit target and exact opaque native session_generation are required", nil)
	}
	params := map[string]any{"target": args.Target, "session_generation": args.SessionGeneration}
	if cancel {
		if !validPoseUUID(args.OperationID) {
			return errorResult("invalid_args", "Cancel requires an exact source-owned operation_id", nil)
		}
		params["cancel"] = true
		params["operation_id"] = args.OperationID
	} else if op == "sharedEdit" {
		if !validPoseRotations(args.Rotations) || len(args.ExpectedJointRevisions) != len(args.Rotations) {
			return errorResult("invalid_args", "Use 1-64 local joint rotations with the exact same expected_joint_revisions keys", nil)
		}
		for joint := range args.Rotations {
			revision, ok := args.ExpectedJointRevisions[joint]
			if !ok || revision < 0 || int64(revision) > 2147483647 {
				return errorResult("invalid_args", "Joint revisions must be exact nonnegative native integers", nil)
			}
		}
		params["rotations"] = args.Rotations
		params["expected_joint_revisions"] = args.ExpectedJointRevisions
	}
	return s.poseFeatureCall(ctx, args.ViewerPID, op, params, true, true)
}
func (s *Server) poseSharedUpdate(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedMutate(ctx, args, "sharedEdit", false)
}
func (s *Server) poseSharedCancel(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedMutate(ctx, args, "sharedEdit", true)
}
func (s *Server) poseStartSharing(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedMutate(ctx, args, "startSharing", false)
}
func (s *Server) poseStopSharing(ctx context.Context, _ *mcp.CallToolRequest, args poseSharedMutationArgs) (*mcp.CallToolResult, any, error) {
	return s.poseSharedMutate(ctx, args, "stopSharing", false)
}
