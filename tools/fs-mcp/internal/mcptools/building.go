package mcptools

import (
	"context"
	"encoding/json"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"math"
	"strings"
)

func registerBuilding(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "build_object_get", Description: "Read a loaded root volume and its transform and faces. Returns a login/region-bound expected_revision valid for 120 seconds. Requires opt-in World read. Does not select objects.", Annotations: withTitle(readOnly, "build object get")}, state.buildingGet)
	mcp.AddTool(server, &mcp.Tool{Name: "build_object_transform", Description: "Move, rotate or resize an owned nonphysical unattached root at its exact expected_revision. Region coordinates; quaternion [x,y,z,w]. Linked roots move/rotate together; linked resizing is refused. Requires active character, World read and Ask-capable Build edit.", Annotations: withTitle(mutating, "build object transform")}, state.buildingTransform)
	mcp.AddTool(server, &mcp.Tool{Name: "build_object_surface", Description: "Edit one legacy face color RGBA, glow, fullbright and UV transform. Optional diffuse texture_item must be owned full-permission agent inventory. PBR overrides use native Build. Requires exact revision, active character and Build edit.", Annotations: withTitle(mutating, "build object surface")}, state.buildingSurface)
	mcp.AddTool(server, &mcp.Tool{Name: "build_object_material", Description: "Assign a full-permission owned inventory PBR material to one face, clearing existing overrides. Requires exact revision, active character, inventory Read and Build edit. Asynchronous guarded submission; inspect receipt and object before considering another operation.", Annotations: withTitle(mutating, "build object material")}, state.buildingMaterial)
	mcp.AddTool(server, &mcp.Tool{Name: "build_rez_primitive", Description: "Rez cube, sphere, cylinder or cone with explicit region position, scale and quaternion within 64m on current buildable parcel. Simulator-default permissions; native selection stays unchanged. Submitted does not confirm object creation or identify its UUID. Never retry an uncertain rez.", Annotations: withTitle(mutating, "build rez primitive")}, state.buildingPrimitive)
	mcp.AddTool(server, &mcp.Tool{Name: "build_rez_inventory", Description: "Rez one owned copyable agent-inventory object within 64m on current buildable parcel. Source retained; inventory permissions are packed natively. Requires inventory/World read, active character and Build rez. Never replay an uncertain rez.", Annotations: withTitle(mutating, "build rez inventory")}, state.buildingRezInventory)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_permissions_get", Description: "Read finished owned agent-inventory item permissions and an exact expected_revision. Excludes links, Library and Trash. Does not fetch task inventory.", Annotations: withTitle(readOnly, "inventory permissions get")}, state.buildingPermissions)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_next_owner_set", Description: "Set explicit next-owner Copy/Modify/Transfer at the exact expected_revision. Native Properties eligibility, base rights and fair-use apply. Other permissions and sale settings are preserved; object slam flag updated. Requires Read and Ask-capable Inventory permissions; no-copy/no-transfer is refused.", Annotations: withTitle(mutating, "inventory next owner set")}, state.buildingNextOwner)
}

type buildingTargetArgs struct {
	ID            string `json:"id"`
	ViewerSession string `json:"viewer_session"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type buildingTransformArgs struct {
	ID               string    `json:"id"`
	ExpectedRevision string    `json:"expected_revision"`
	Position         []float64 `json:"position,omitempty"`
	Rotation         []float64 `json:"rotation,omitempty"`
	Scale            []float64 `json:"scale,omitempty"`
	ViewerSession    string    `json:"viewer_session"`
	ViewerPID        int       `json:"viewer_pid,omitempty"`
}
type buildingSurfaceArgs struct {
	ID               string    `json:"id"`
	ExpectedRevision string    `json:"expected_revision"`
	Face             int       `json:"face"`
	Color            []float64 `json:"color,omitempty"`
	Glow             *float64  `json:"glow,omitempty"`
	Fullbright       *bool     `json:"fullbright,omitempty"`
	UVScale          []float64 `json:"uv_scale,omitempty"`
	UVOffset         []float64 `json:"uv_offset,omitempty"`
	UVRotation       *float64  `json:"uv_rotation,omitempty"`
	TextureItem      string    `json:"texture_item,omitempty"`
	ViewerSession    string    `json:"viewer_session"`
	ViewerPID        int       `json:"viewer_pid,omitempty"`
}
type buildingMaterialArgs struct {
	ID               string `json:"id"`
	ExpectedRevision string `json:"expected_revision"`
	Face             int    `json:"face"`
	MaterialItem     string `json:"material_item"`
	ViewerSession    string `json:"viewer_session"`
	ViewerPID        int    `json:"viewer_pid,omitempty"`
}
type buildingPrimitiveArgs struct {
	Shape         string    `json:"shape"`
	Position      []float64 `json:"position"`
	Rotation      []float64 `json:"rotation"`
	Scale         []float64 `json:"scale"`
	ViewerSession string    `json:"viewer_session"`
	ViewerPID     int       `json:"viewer_pid,omitempty"`
}
type buildingRezArgs struct {
	ID            string    `json:"id"`
	Position      []float64 `json:"position"`
	ViewerSession string    `json:"viewer_session"`
	ViewerPID     int       `json:"viewer_pid,omitempty"`
}
type buildingNextOwnerArgs struct {
	ID               string `json:"id"`
	ExpectedRevision string `json:"expected_revision"`
	Copy             bool   `json:"copy"`
	Modify           bool   `json:"modify"`
	Transfer         bool   `json:"transfer"`
	ViewerSession    string `json:"viewer_session"`
	ViewerPID        int    `json:"viewer_pid,omitempty"`
}

func boundedVector(raw any, count int, low, high float64) bool {
	values, ok := raw.([]any)
	if !ok || len(values) != count {
		return false
	}
	for _, v := range values {
		n, ok := v.(float64)
		if !ok || math.IsNaN(n) || math.IsInf(n, 0) || n < low || n > high {
			return false
		}
	}
	return true
}
func buildingParams(op string, args any) (map[string]any, bool) {
	raw, err := json.Marshal(args)
	if err != nil {
		return nil, false
	}
	var p map[string]any
	if json.Unmarshal(raw, &p) != nil {
		return nil, false
	}
	delete(p, "viewer_session")
	delete(p, "viewer_pid")
	id, _ := p["id"].(string)
	if op != "rezPrimitive" && !socialUUID(id) {
		return nil, false
	}
	if op != "objectGet" && op != "permissionsGet" && op != "rezPrimitive" && op != "rezInventory" {
		r, _ := p["expected_revision"].(string)
		if !socialUUID(r) {
			return nil, false
		}
	}
	for key, count := range map[string]int{"position": 3, "scale": 3, "rotation": 4, "color": 4, "uv_scale": 2, "uv_offset": 2} {
		if v, ok := p[key]; ok {
			low, high := -1e9, 1e9
			if key == "color" {
				low, high = 0, 1
			}
			if key == "scale" {
				low, high = .001, 256
			}
			if !boundedVector(v, count, low, high) {
				return nil, false
			}
		}
	}
	if v, ok := p["rotation"]; ok {
		sum := 0.
		for _, x := range v.([]any) {
			n := x.(float64)
			sum += n * n
		}
		if math.Abs(sum-1) > .001 {
			return nil, false
		}
	}
	if op == "transform" && p["position"] == nil && p["rotation"] == nil && p["scale"] == nil {
		return nil, false
	}
	if op == "surface" || op == "material" {
		face := p["face"].(float64)
		if face < 0 || face > 63 {
			return nil, false
		}
	}
	if op == "surface" {
		if len(p) == 3 {
			return nil, false
		}
		if v, ok := p["glow"]; ok && (v.(float64) < 0 || v.(float64) > 1) {
			return nil, false
		}
		if v, ok := p["uv_rotation"]; ok && math.Abs(v.(float64)) > 6.283186 {
			return nil, false
		}
	}
	for _, key := range []string{"texture_item", "material_item"} {
		if v, ok := p[key]; ok && !socialUUID(v.(string)) {
			return nil, false
		}
	}
	if op == "nextOwnerSet" && !p["copy"].(bool) && !p["transfer"].(bool) {
		return nil, false
	}
	if op == "rezPrimitive" {
		shape := p["shape"].(string)
		if shape != "cube" && shape != "sphere" && shape != "cylinder" && shape != "cone" {
			return nil, false
		}
	}
	return p, true
}
func (s *Server) buildingCall(ctx context.Context, session string, pid int, op string, args any) (*mcp.CallToolResult, any, error) {
	if strings.TrimSpace(session) == "" || pid < 0 {
		return errorResult("invalid_args", "Exact viewer_session is required", nil)
	}
	params, ok := buildingParams(op, args)
	if !ok {
		return errorResult("invalid_args", "Invalid building target, revision, vector, surface or next-owner rights", nil)
	}
	classes := []string{"world_read"}
	switch op {
	case "permissionsGet":
		classes = []string{"read"}
	case "nextOwnerSet":
		classes = []string{"read", "inventory_permissions"}
	case "rezPrimitive":
		classes = append(classes, "build_rez")
	case "rezInventory":
		classes = append(classes, "build_rez", "read")
	case "transform", "surface", "material":
		classes = append(classes, "build_edit")
	}
	if params["texture_item"] != nil || op == "material" {
		classes = append(classes, "read")
	}
	return s.callWorldFeature(ctx, session, pid, "FSBuild", "building_tools", op, params, classes)
}

// No generic native fallback. Query capability for this exact login every time.
func (s *Server) callWorldFeature(ctx context.Context, session string, pid int, apiName, capability, op string, params map[string]any, classes []string) (*mcp.CallToolResult, any, error) {
	api, policy, errRes := s.openClasses(withViewerSession(ctx, session), pid, classes)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present {
		return errorResult("unsupported_feature", "Explicit native feature permissions are required", nil)
	}
	health, err := api.CallNamed(ctx, "LocalAssistant", "health", map[string]any{"diagnostic": true})
	if err != nil {
		return apiError(err)
	}
	var h struct {
		Capabilities map[string]int `json:"capabilities"`
	}
	if len(health) > 65536 || json.Unmarshal(health, &h) != nil || h.Capabilities[capability] != 1 {
		return errorResult("unsupported_feature", "Update matching viewer and sidecar; required capability is unavailable", nil)
	}
	return apiResult(api.CallNamed(ctx, apiName, op, params))
}
func (s *Server) buildingGet(ctx context.Context, _ *mcp.CallToolRequest, args buildingTargetArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "objectGet", args)
}
func (s *Server) buildingPermissions(ctx context.Context, _ *mcp.CallToolRequest, args buildingTargetArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "permissionsGet", args)
}
func (s *Server) buildingTransform(ctx context.Context, _ *mcp.CallToolRequest, args buildingTransformArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "transform", args)
}
func (s *Server) buildingSurface(ctx context.Context, _ *mcp.CallToolRequest, args buildingSurfaceArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "surface", args)
}
func (s *Server) buildingMaterial(ctx context.Context, _ *mcp.CallToolRequest, args buildingMaterialArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "material", args)
}
func (s *Server) buildingPrimitive(ctx context.Context, _ *mcp.CallToolRequest, args buildingPrimitiveArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "rezPrimitive", args)
}
func (s *Server) buildingRezInventory(ctx context.Context, _ *mcp.CallToolRequest, args buildingRezArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "rezInventory", args)
}
func (s *Server) buildingNextOwner(ctx context.Context, _ *mcp.CallToolRequest, args buildingNextOwnerArgs) (*mcp.CallToolResult, any, error) {
	return s.buildingCall(ctx, args.ViewerSession, args.ViewerPID, "nextOwnerSet", args)
}
