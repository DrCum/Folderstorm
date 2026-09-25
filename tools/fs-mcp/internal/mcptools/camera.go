package mcptools

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"

	"fs-mcp/internal/viewerapi"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const maxSnapshotBytes = 8 << 20

func registerCamera(s *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(s, &mcp.Tool{
		Name:        "camera_get",
		Description: "Return the current camera position, focus, and distance.",
		Annotations: withTitle(readOnly, "Camera pose"),
	}, state.cameraGet)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "camera_set_pose",
		Description: "Move the camera to a preset relative to the avatar: portrait, full_body, front, back, left, or right.",
		Annotations: withTitle(mutating, "Camera preset"),
	}, state.cameraSetPose)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "camera_set",
		Description: "Move the camera to an explicit region position and focus, each an [x, y, z] array.",
		Annotations: withTitle(mutating, "Set camera"),
	}, state.cameraSet)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "camera_reset",
		Description: "Return the camera to the default third-person pose.",
		Annotations: withTitle(mutating, "Reset camera"),
	}, state.cameraReset)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "camera_snapshot",
		Description: "Capture a JPEG of the current view. Hides UI and HUD by default and writes a temp JPEG. This follows the Camera permission and is not a read.",
		Annotations: withTitle(mutating, "Snapshot"),
	}, state.cameraSnapshot)
}

type cameraPoseArgs struct {
	Preset    string `json:"preset" jsonschema:"portrait, full_body, front, back, left, or right"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type cameraSetArgs struct {
	Position  []float64 `json:"position" jsonschema:"region position [x, y, z]"`
	Focus     []float64 `json:"focus" jsonschema:"region focus [x, y, z]"`
	ViewerPID int       `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type snapshotArgs struct {
	MaxEdge   int   `json:"max_edge,omitempty" jsonschema:"cap the long edge in pixels, default 1024"`
	ShowUI    *bool `json:"show_ui,omitempty" jsonschema:"include viewer UI, default false"`
	ShowHUD   *bool `json:"show_hud,omitempty" jsonschema:"include HUD, default false"`
	ViewerPID int   `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func (s *Server) cameraGet(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassRead)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.CameraAPI, "get", map[string]any{}))
}

func (s *Server) cameraSetPose(ctx context.Context, _ *mcp.CallToolRequest, args cameraPoseArgs) (*mcp.CallToolResult, any, error) {
	if strings.TrimSpace(args.Preset) == "" {
		return errorResult("invalid_args", "preset is required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassCamera)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.CameraAPI, "setPose", map[string]any{"preset": args.Preset}))
}

func (s *Server) cameraSet(ctx context.Context, _ *mcp.CallToolRequest, args cameraSetArgs) (*mcp.CallToolResult, any, error) {
	if len(args.Position) != 3 || len(args.Focus) != 3 {
		return errorResult("invalid_args", "position and focus must each be [x, y, z]", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassCamera)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.CameraAPI, "set", map[string]any{
		"position": args.Position,
		"focus":    args.Focus,
	}))
}

func (s *Server) cameraReset(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassCamera)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.CameraAPI, "reset", map[string]any{}))
}

func (s *Server) cameraSnapshot(ctx context.Context, _ *mcp.CallToolRequest, args snapshotArgs) (*mcp.CallToolResult, any, error) {
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassCamera)
	if errRes != nil {
		return errRes, nil, nil
	}
	params := map[string]any{}
	if args.MaxEdge > 0 {
		params["max_edge"] = args.MaxEdge
	}
	if args.ShowUI != nil {
		params["show_ui"] = *args.ShowUI
	}
	if args.ShowHUD != nil {
		params["show_hud"] = *args.ShowHUD
	}
	raw, err := api.CallNamed(ctx, viewerapi.CameraAPI, "snapshot", params)
	if err != nil {
		return apiError(err)
	}
	var payload map[string]any
	if unmarshalErr := json.Unmarshal(raw, &payload); unmarshalErr != nil {
		return errorResult("encode", unmarshalErr.Error(), nil)
	}
	path, _ := payload["path"].(string)
	if path == "" {
		return errorResult("viewer_error", "snapshot did not return a file path", payload)
	}
	if !filepath.IsAbs(path) {
		return errorResult("viewer_error", "snapshot path must be absolute", nil)
	}
	info, statErr := os.Stat(path)
	if statErr != nil {
		return errorResult("viewer_error", statErr.Error(), nil)
	}
	if info.Size() <= 0 || info.Size() > maxSnapshotBytes {
		return errorResult("too_large", "snapshot file is empty or larger than 8MiB", map[string]any{"bytes": info.Size()})
	}
	data, readErr := os.ReadFile(path)
	_ = os.Remove(path)
	if readErr != nil {
		return errorResult("read", readErr.Error(), nil)
	}
	delete(payload, "path")
	meta, _ := json.Marshal(payload)
	return &mcp.CallToolResult{
		Content: []mcp.Content{
			&mcp.ImageContent{Data: data, MIMEType: "image/jpeg"},
			&mcp.TextContent{Text: string(meta)},
		},
		StructuredContent: payload,
	}, payload, nil
}
