package mcptools

import (
	"context"
	"fs-mcp/internal/viewerapi"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"strings"
)

func registerWorkspaces(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "workspace_list", Description: "List saved workspaces and viewport/bar layouts, their exact IDs and favorites. Requires Read and workspace_tools version 1.", Annotations: withTitle(readOnly, "Saved workspaces and layouts")}, state.workspaceList)
	mcp.AddTool(server, &mcp.Tool{Name: "workspace_status", Description: "Read active/modified workspace state, Previous availability and the latest restore report. Pending attachments are not confirmed success. Requires Read and workspace_tools version 1.", Annotations: withTitle(readOnly, "Workspace status")}, state.workspaceStatus)
	mcp.AddTool(server, &mcp.Tool{Name: "workspace_preview", Description: "Read fitted window geometry and restore groups for an existing workspace or layout without applying it. Use its exact ID from workspace_list. Requires Read and workspace_tools version 1.", Annotations: withTitle(readOnly, "Preview saved arrangement")}, state.workspacePreview)
	mcp.AddTool(server, &mcp.Tool{Name: "workspace_apply", Description: "Switch to an existing workspace or layout using kind and exact ID from workspace_list. The viewer enforces Workspace plus Camera/Wear permissions for attached effects and owns Ask dialogs. Does not create profiles. Finish Preferences first. Requires workspace_tools version 1; no legacy fallback.", Annotations: withTitle(mutating, "Switch saved arrangement")}, state.workspaceApply)
	mcp.AddTool(server, &mcp.Tool{Name: "workspace_previous", Description: "Return to the session-only Previous arrangement, including unsaved compatible positions and settings. Viewer-owned Workspace/Camera/Wear permissions apply. Finish Preferences first. Requires workspace_tools version 1.", Annotations: withTitle(mutating, "Previous arrangement")}, state.workspacePrevious)
}

type workspaceTargetArgs struct {
	Kind      string `json:"kind" jsonschema:"workspace or layout"`
	ID        string `json:"id" jsonschema:"exact saved ID from workspace_list"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func validWorkspaceTarget(args workspaceTargetArgs) bool {
	return (args.Kind == "workspace" || args.Kind == "layout") && args.ID != "" && len(args.ID) <= 128 && strings.TrimSpace(args.ID) == args.ID
}
func (s *Server) workspaceCall(ctx context.Context, pid int, op string, params map[string]any, read bool) (*mcp.CallToolResult, any, error) {
	class := viewerapi.ClassWorkspace
	if read {
		class = viewerapi.ClassRead
	}
	api, policy, errRes := s.openClass(ctx, pid, class)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present || policy.WorkspaceTools != viewerapi.WorkspaceToolsVersion {
		return errorResult("unsupported_feature", "This viewer does not support workspace tools version 1. Update viewer and sidecar together; no legacy switch was attempted.", nil)
	}
	return apiResult(api.CallNamed(ctx, viewerapi.WorkspaceAPI, op, params))
}
func (s *Server) workspaceList(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.workspaceCall(ctx, args.ViewerPID, "list", map[string]any{}, true)
}
func (s *Server) workspaceStatus(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.workspaceCall(ctx, args.ViewerPID, "status", map[string]any{}, true)
}
func (s *Server) workspacePreview(ctx context.Context, _ *mcp.CallToolRequest, args workspaceTargetArgs) (*mcp.CallToolResult, any, error) {
	if !validWorkspaceTarget(args) {
		return errorResult("invalid_args", "kind must be workspace or layout and id must be an exact saved identifier", nil)
	}
	return s.workspaceCall(ctx, args.ViewerPID, "preview", map[string]any{"kind": args.Kind, "id": args.ID}, true)
}
func (s *Server) workspaceApply(ctx context.Context, _ *mcp.CallToolRequest, args workspaceTargetArgs) (*mcp.CallToolResult, any, error) {
	if !validWorkspaceTarget(args) {
		return errorResult("invalid_args", "kind must be workspace or layout and id must be an exact saved identifier", nil)
	}
	return s.workspaceCall(ctx, args.ViewerPID, "apply", map[string]any{"kind": args.Kind, "id": args.ID}, false)
}
func (s *Server) workspacePrevious(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.workspaceCall(ctx, args.ViewerPID, "previous", map[string]any{}, false)
}
