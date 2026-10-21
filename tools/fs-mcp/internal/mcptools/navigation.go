package mcptools

import (
	"context"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"math"
	"strings"
)

func registerNavigation(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "navigation_status", Description: "Read exact login location, native flight/pilot/teleport state and the last assistant-owned navigation outcome. Requires opt-in World read. No movement or character switching.", Annotations: withTitle(readOnly, "Navigation status")}, state.navigationStatus)
	mcp.AddTool(server, &mcp.Tool{Name: "navigation_move_to", Description: "Walk or fly to global coordinates in the current region within 64m. Explicit mode, stop_distance .25..4m and timeout_seconds 1..30. Native autopilot; arrival is not guaranteed. Refuses sitting or existing native pilot/teleport. Requires active character, World read and Ask-capable Navigation move. Manual input, revocation, switching, region changes and timeout cancel only owned movement. Returns operation_id; poll status.", Annotations: withTitle(mutating, "Move avatar")}, state.navigationMove)
	mcp.AddTool(server, &mcp.Tool{Name: "navigation_flying_set", Description: "Explicitly take off or land through native flight eligibility. Does not move to a destination or stand the avatar. Refuses existing movement. Requires active character, World read and Navigation move.", Annotations: withTitle(mutating, "Set avatar flight")}, state.navigationFlying)
	mcp.AddTool(server, &mcp.Tool{Name: "navigation_teleport", Description: "Submit one teleport to explicit global coordinates, respecting native restrictions and delayed maturity checks. Requires active character, World read and separate Ask-capable Navigation teleport. Never replay an uncertain teleport. Native cancellation after submission is best effort; status distinguishes submission from observed arrival.", Annotations: withTitle(mutating, "Teleport avatar")}, state.navigationTeleport)
	mcp.AddTool(server, &mcp.Tool{Name: "navigation_stop", Description: "Cancel only assistant-owned navigation matching this exact operation_id and viewer_session. Never stops a newer native pilot/follow/teleport. Requires active character and World read; stopping only the prior owned action needs no new movement approval. Teleport cancellation is best effort and cannot undo arrival.", Annotations: withTitle(mutating, "Stop owned navigation")}, state.navigationStop)
}

type navigationViewerArgs struct {
	ViewerSession string `json:"viewer_session"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type navigationMoveArgs struct {
	Position       []float64 `json:"position"`
	Mode           string    `json:"mode"`
	StopDistance   float64   `json:"stop_distance"`
	TimeoutSeconds int       `json:"timeout_seconds"`
	ViewerSession  string    `json:"viewer_session"`
	ViewerPID      int       `json:"viewer_pid,omitempty"`
}
type navigationFlyingArgs struct {
	Flying        bool   `json:"flying"`
	ViewerSession string `json:"viewer_session"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type navigationTeleportArgs struct {
	Position      []float64 `json:"position"`
	ViewerSession string    `json:"viewer_session"`
	ViewerPID     int       `json:"viewer_pid,omitempty"`
}
type navigationStopArgs struct {
	OperationID   string `json:"operation_id"`
	ViewerSession string `json:"viewer_session"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}

func navigationPosition(p []float64) bool {
	if len(p) != 3 {
		return false
	}
	for _, v := range p {
		if math.IsNaN(v) || math.IsInf(v, 0) || v < 0 || v > 4294967295 {
			return false
		}
	}
	return p[2] <= 4096
}
func (s *Server) navigationCall(ctx context.Context, session string, pid int, op string, params map[string]any) (*mcp.CallToolResult, any, error) {
	if strings.TrimSpace(session) == "" || pid < 0 {
		return errorResult("invalid_args", "Exact viewer_session is required", nil)
	}
	classes := []string{"world_read"}
	if op != "status" && op != "stop" {
		permission := "navigation_move"
		if op == "teleport" {
			permission = "navigation_teleport"
		}
		classes = append(classes, permission)
	}
	return s.callWorldFeature(ctx, session, pid, "FSNavigation", "navigation_tools", op, params, classes)
}
func (s *Server) navigationStatus(ctx context.Context, _ *mcp.CallToolRequest, args navigationViewerArgs) (*mcp.CallToolResult, any, error) {
	return s.navigationCall(ctx, args.ViewerSession, args.ViewerPID, "status", map[string]any{})
}
func (s *Server) navigationMove(ctx context.Context, _ *mcp.CallToolRequest, args navigationMoveArgs) (*mcp.CallToolResult, any, error) {
	if !navigationPosition(args.Position) || (args.Mode != "walk" && args.Mode != "fly") || math.IsNaN(args.StopDistance) || math.IsInf(args.StopDistance, 0) || args.StopDistance < .25 || args.StopDistance > 4 || args.TimeoutSeconds < 1 || args.TimeoutSeconds > 30 {
		return errorResult("invalid_args", "Global position, walk/fly, stop_distance .25..4 and timeout_seconds 1..30 required", nil)
	}
	return s.navigationCall(ctx, args.ViewerSession, args.ViewerPID, "moveTo", map[string]any{"position": args.Position, "mode": args.Mode, "stop_distance": args.StopDistance, "timeout_seconds": args.TimeoutSeconds})
}
func (s *Server) navigationFlying(ctx context.Context, _ *mcp.CallToolRequest, args navigationFlyingArgs) (*mcp.CallToolResult, any, error) {
	return s.navigationCall(ctx, args.ViewerSession, args.ViewerPID, "flyingSet", map[string]any{"flying": args.Flying})
}
func (s *Server) navigationTeleport(ctx context.Context, _ *mcp.CallToolRequest, args navigationTeleportArgs) (*mcp.CallToolResult, any, error) {
	if !navigationPosition(args.Position) {
		return errorResult("invalid_args", "Finite nonnegative global position with height 0..4096 required", nil)
	}
	return s.navigationCall(ctx, args.ViewerSession, args.ViewerPID, "teleport", map[string]any{"position": args.Position})
}
func (s *Server) navigationStop(ctx context.Context, _ *mcp.CallToolRequest, args navigationStopArgs) (*mcp.CallToolResult, any, error) {
	if strings.TrimSpace(args.OperationID) == "" || len(args.OperationID) > 64 {
		return errorResult("invalid_args", "Exact owned operation_id required", nil)
	}
	return s.navigationCall(ctx, args.ViewerSession, args.ViewerPID, "stop", map[string]any{"operation_id": args.OperationID})
}
