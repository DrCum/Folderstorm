package mcptools

import (
	"context"
	"encoding/json"
	"math"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const (
	worldAPI                = "FSWorld"
	worldReadClass          = "world_read"
	worldObservationVersion = 1
)

func registerWorld(server *mcp.Server, state *Server, readOnly *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "world_location", Description: "Read this login's observed region, position and orientation. Requires opt-in World observation permission and world_observation version 1; native visibility restrictions apply.", Annotations: withTitle(readOnly, "Current location")}, state.worldLocation)
	mcp.AddTool(server, &mcp.Tool{Name: "world_parcel", Description: "Read the currently loaded agent parcel and a small set of read-only flags. Does not select or edit land. Requires opt-in World observation permission.", Annotations: withTitle(readOnly, "Current parcel")}, state.worldParcel)
	mcp.AddTool(server, &mcp.Tool{Name: "world_nearby_avatars", Description: "Read a bounded snapshot of loaded nearby avatars. Loaded legacy names may be available; unresolved names stay unknown. Not a region census. Native name/location/nearby restrictions can refuse the whole observation.", Annotations: withTitle(readOnly, "Observed nearby avatars")}, state.worldAvatars)
	mcp.AddTool(server, &mcp.Tool{Name: "world_nearby_objects", Description: "Read loaded nearby root volumes, IDs and positions. Does not fetch properties, inspect object contents or edit objects. Empty results do not prove absence.", Annotations: withTitle(readOnly, "Observed nearby objects")}, state.worldObjects)
	mcp.AddTool(server, &mcp.Tool{Name: "world_audio_sources", Description: "Read bounded sound-history observations since this service first observed the current login/region. An observed source differs from its owner; historical position and a playing flag do not prove current audibility. Does not play, stop or fetch sounds.", Annotations: withTitle(readOnly, "Observed sound sources")}, state.worldAudio)
	mcp.AddTool(server, &mcp.Tool{Name: "world_media_sources", Description: "Read parcel source-configuration flags, native streaming state and loaded object/face media associations. URLs are omitted. Missing or unloaded data remains unknown. Does not navigate, start, stop or download media.", Annotations: withTitle(readOnly, "Observed media sources")}, state.worldMedia)
}

type worldNearbyArgs struct {
	Radius        *float64 `json:"radius,omitempty" jsonschema:"observation radius in meters, 1 to 256, default 64"`
	Limit         *int     `json:"limit,omitempty" jsonschema:"maximum rows, 1 to 100, default 25; live snapshots do not support pagination"`
	ViewerSession string   `json:"viewer_session,omitempty" jsonschema:"exact login reference from viewer_list; required for managed characters"`
	ViewerPID     int      `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func worldNearbyParams(args worldNearbyArgs) (map[string]any, bool) {
	radius, limit := 64., 25
	if args.Radius != nil {
		radius = *args.Radius
	}
	if args.Limit != nil {
		limit = *args.Limit
	}
	if math.IsNaN(radius) || math.IsInf(radius, 0) || radius < 1 || radius > 256 || limit < 1 || limit > 100 {
		return nil, false
	}
	return map[string]any{"radius": radius, "limit": limit}, true
}

func worldOperation(op string) bool {
	switch op {
	case "location", "parcel", "avatars", "objects", "audio", "media":
		return true
	}
	return false
}

// Query each request, never cache a capability across logins or silently use
// legacy LLAgent reads. Authentication/session pinning belongs to resolve.
func callWorldObservation(ctx context.Context, api API, op string, params map[string]any) (*mcp.CallToolResult, any, error) {
	if !worldOperation(op) {
		return errorResult("invalid_args", "Unknown world observation", nil)
	}
	health, err := api.CallNamed(ctx, "LocalAssistant", "health", map[string]any{"diagnostic": true})
	if err != nil {
		return apiError(err)
	}
	if !worldCapability(health) {
		return errorResult("unsupported_feature", "This viewer does not support world_observation version 1. Update viewer and sidecar together; no legacy world API was called.", nil)
	}
	return apiResult(api.CallNamed(ctx, worldAPI, op, params))
}

func worldCapability(raw []byte) bool {
	var health struct {
		Capabilities map[string]json.RawMessage `json:"capabilities"`
	}
	if len(raw) > 65536 || json.Unmarshal(raw, &health) != nil {
		return false
	}
	var version int
	return json.Unmarshal(health.Capabilities["world_observation"], &version) == nil && version == worldObservationVersion
}

func (s *Server) worldCall(ctx context.Context, pid int, op string, params map[string]any) (*mcp.CallToolResult, any, error) {
	api, policy, errRes := s.openClass(ctx, pid, worldReadClass)
	if errRes != nil {
		return errRes, nil, nil
	}
	// Older viewers must not inherit legacy Read or bypass opt-in permission.
	if !policy.Present {
		return errorResult("unsupported_feature", "World observations require explicit world_read permission support", nil)
	}
	return callWorldObservation(ctx, api, op, params)
}

func (s *Server) worldLocation(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.worldCall(withViewerSession(ctx, args.ViewerSession), args.ViewerPID, "location", map[string]any{})
}
func (s *Server) worldParcel(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.worldCall(withViewerSession(ctx, args.ViewerSession), args.ViewerPID, "parcel", map[string]any{})
}
func (s *Server) worldNearby(ctx context.Context, args worldNearbyArgs, op string) (*mcp.CallToolResult, any, error) {
	params, valid := worldNearbyParams(args)
	if !valid {
		return errorResult("invalid_args", "radius must be finite and 1..256; limit must be 1..100", nil)
	}
	return s.worldCall(withViewerSession(ctx, args.ViewerSession), args.ViewerPID, op, params)
}
func (s *Server) worldAvatars(ctx context.Context, _ *mcp.CallToolRequest, args worldNearbyArgs) (*mcp.CallToolResult, any, error) {
	return s.worldNearby(ctx, args, "avatars")
}
func (s *Server) worldObjects(ctx context.Context, _ *mcp.CallToolRequest, args worldNearbyArgs) (*mcp.CallToolResult, any, error) {
	return s.worldNearby(ctx, args, "objects")
}
func (s *Server) worldAudio(ctx context.Context, _ *mcp.CallToolRequest, args worldNearbyArgs) (*mcp.CallToolResult, any, error) {
	return s.worldNearby(ctx, args, "audio")
}
func (s *Server) worldMedia(ctx context.Context, _ *mcp.CallToolRequest, args worldNearbyArgs) (*mcp.CallToolResult, any, error) {
	return s.worldNearby(ctx, args, "media")
}
