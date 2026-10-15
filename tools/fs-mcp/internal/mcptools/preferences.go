package mcptools

import (
	"context"
	"encoding/json"
	"math"
	"strconv"
	"strings"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const preferencesAPI = "FSPreferences"
const preferencesReadClass = "preferences_read"
const preferencesWriteClass = "preferences_write"

type preferenceControl struct {
	kind     string
	min, max float64
	writable bool
}

// A closed public catalog. Native descriptors remain authoritative for dynamic
// availability, FOV limits, restrictions, native ownership and actual types.
var preferenceControls = map[string]preferenceControl{
	"letter_keys_move": {"boolean", 0, 1, true}, "arrow_keys_move": {"boolean", 0, 1, true},
	"tap_hold_run": {"boolean", 0, 1, true}, "quick_jump": {"boolean", 0, 1, true},
	"mouse_sensitivity": {"number", 0, 15, true}, "invert_mouse": {"boolean", 0, 1, true},
	"selection_beam": {"boolean", 0, 1, true}, "look_at_targets": {"integer", 0, 1, true},
	"ui_scale": {"number", .75, 2, true}, "camera_fov": {"number", .17, 2.97, true},
	"camera_offset_scale": {"number", .5, 3, true}, "draw_distance": {"number", 32, 1024, true},
	"graphics_quality": {"integer", 0, 6, false}, "master_volume": {"number", 0, 1, false},
	"effects_volume": {"number", 0, 1, false}, "mute_audio": {"boolean", 0, 1, false},
}

func registerPreferences(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "preferences_schema", Description: "Read the closed curated preference catalog, types, ranges, storage and current native availability for an exact viewer_session. Requires opt-in Preferences read. Arbitrary debug settings, permissions, credentials, paths and ownership controls are excluded.", Annotations: withTitle(readOnly, "Curated preference schema")}, state.preferencesSchema)
	mcp.AddTool(server, &mcp.Tool{Name: "preferences_get", Description: "Read effective, saved, default and unsaved layers plus an expected revision for up to 16 curated preference IDs. Omit ids for the whole catalog. Requires opt-in Preferences read and an exact viewer_session.", Annotations: withTitle(readOnly, "Curated preferences")}, state.preferencesGet)
	mcp.AddTool(server, &mcp.Tool{Name: "preferences_status", Description: "Read the exact login's current preference revision, active preview and ownership-safe cleanup outcome. Requires opt-in Preferences read. No preview values are available after read permission is revoked.", Annotations: withTitle(readOnly, "Preference preview status")}, state.preferencesStatus)
	mcp.AddTool(server, &mcp.Tool{Name: "preferences_preview", Description: "Temporarily preview 1..16 approved curated settings at an exact expected_revision. One preview owns the login for at most 300 seconds (default 120). Saved layers remain unchanged until explicit preferences_accept. Finish native Preferences and workspace operations first. UI scale, graphics and camera require the active character; native restrictions apply. Does not jump, move the avatar, acquire audio/voice/input ownership or change assistant permissions. Requires Preferences read and Ask-capable Preferences write.", Annotations: withTitle(mutating, "Preview curated preference changes"), InputSchema: preferencesPreviewSchema()}, state.preferencesPreview)
	mcp.AddTool(server, &mcp.Tool{Name: "preferences_accept", Description: "Explicitly persist only the unchanged draft identified by transaction_id and current expected_revision on this exact viewer_session. External edits, expiry, changed identity, native Preferences and restrictions can refuse acceptance. Persistence uses saved control layers and the normal viewer settings-file lifecycle. Requires Preferences read and Preferences write.", Annotations: withTitle(mutating, "Accept preference preview")}, state.preferencesAccept)
	mcp.AddTool(server, &mcp.Tool{Name: "preferences_cancel", Description: "Discard this exact preference preview using transaction_id and current expected_revision. Restores only still-owned runtime/saved layers and reports partial cleanup when native/manual edits or ownership changes prevent restoration. Requires Preferences read and Preferences write.", Annotations: withTitle(mutating, "Cancel preference preview")}, state.preferencesCancel)
}

type preferencesViewerArgs struct {
	ViewerSession string `json:"viewer_session" jsonschema:"exact opaque login reference from viewer_list"`
	ViewerPID     int    `json:"viewer_pid,omitempty" jsonschema:"optional matching Firestorm process id"`
}
type preferencesGetArgs struct {
	IDs           []string `json:"ids,omitempty" jsonschema:"1 to 16 unique public curated preference IDs; omit for the whole catalog"`
	ViewerSession string   `json:"viewer_session" jsonschema:"exact opaque login reference from viewer_list"`
	ViewerPID     int      `json:"viewer_pid,omitempty" jsonschema:"optional matching Firestorm process id"`
}
type preferencesPreviewArgs struct {
	Changes          map[string]any `json:"changes"`
	ExpectedRevision string         `json:"expected_revision"`
	TTLSeconds       *int           `json:"ttl_seconds,omitempty"`
	ViewerSession    string         `json:"viewer_session"`
	ViewerPID        int            `json:"viewer_pid,omitempty"`
}
type preferencesTransactionArgs struct {
	TransactionID    string `json:"transaction_id" jsonschema:"exact opaque ID returned by preferences_preview"`
	ExpectedRevision string `json:"expected_revision" jsonschema:"current exact revision from preferences_get or preferences_status"`
	ViewerSession    string `json:"viewer_session" jsonschema:"exact opaque login reference from viewer_list"`
	ViewerPID        int    `json:"viewer_pid,omitempty" jsonschema:"optional matching Firestorm process id"`
}

func preferencesPreviewSchema() map[string]any {
	properties := map[string]any{}
	for id, entry := range preferenceControls {
		if !entry.writable {
			continue
		}
		schema := map[string]any{"type": entry.kind}
		if entry.kind != "boolean" {
			schema["minimum"], schema["maximum"] = entry.min, entry.max
		}
		properties[id] = schema
	}
	return map[string]any{
		"type": "object", "additionalProperties": false,
		"required": []string{"changes", "expected_revision", "viewer_session"},
		"properties": map[string]any{
			"changes":           map[string]any{"type": "object", "minProperties": 1, "maxProperties": 16, "additionalProperties": false, "properties": properties},
			"expected_revision": map[string]any{"type": "string", "pattern": "^[1-9][0-9]{0,18}$"},
			"viewer_session":    map[string]any{"type": "string", "minLength": 1, "maxLength": 128},
			"viewer_pid":        map[string]any{"type": "integer", "minimum": 0},
			"ttl_seconds":       map[string]any{"type": "integer", "minimum": 1, "maximum": 300},
		},
	}
}

func preferencesRevision(value string) bool {
	if value == "" || len(value) > 19 || value[0] == '0' {
		return false
	}
	_, err := strconv.ParseUint(value, 10, 64)
	return err == nil && strings.IndexFunc(value, func(r rune) bool { return r < '0' || r > '9' }) < 0
}
func preferencesTarget(session string, pid int) bool {
	return session != "" && len(session) <= 128 && strings.TrimSpace(session) == session && pid >= 0
}
func preferencesChanges(changes map[string]any) bool {
	if len(changes) < 1 || len(changes) > 16 {
		return false
	}
	for id, raw := range changes {
		entry, exists := preferenceControls[id]
		if !exists || !entry.writable {
			return false
		}
		if entry.kind == "boolean" {
			if _, ok := raw.(bool); !ok {
				return false
			}
			continue
		}
		var number float64
		switch v := raw.(type) {
		case float64:
			number = v
		case int:
			number = float64(v)
		default:
			return false
		}
		if math.IsNaN(number) || math.IsInf(number, 0) || number < entry.min || number > entry.max || (entry.kind == "integer" && math.Trunc(number) != number) {
			return false
		}
	}
	return true
}
func preferencesOperation(op string) bool {
	return op == "schema" || op == "get" || op == "status" || op == "preview" || op == "accept" || op == "cancel"
}
func preferencesCapability(raw []byte) bool {
	var health struct {
		Capabilities map[string]json.RawMessage `json:"capabilities"`
	}
	if len(raw) > 65536 || json.Unmarshal(raw, &health) != nil {
		return false
	}
	var version int
	return json.Unmarshal(health.Capabilities["preferences_tools"], &version) == nil && version == 1
}
func (s *Server) preferencesCall(ctx context.Context, session string, pid int, op string, params map[string]any) (*mcp.CallToolResult, any, error) {
	if !preferencesTarget(session, pid) || !preferencesOperation(op) {
		return errorResult("invalid_args", "An exact viewer_session and a curated preferences operation are required", nil)
	}
	classes := []string{preferencesReadClass}
	if op == "preview" || op == "accept" || op == "cancel" {
		classes = append(classes, preferencesWriteClass)
	}
	api, policy, errRes := s.openClasses(withViewerSession(ctx, session), pid, classes)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present {
		return errorResult("unsupported_feature", "Curated preferences require explicit preferences_read/preferences_write support", nil)
	}
	health, err := api.CallNamed(ctx, "LocalAssistant", "health", map[string]any{"diagnostic": true})
	if err != nil {
		return apiError(err)
	}
	if !preferencesCapability(health) {
		return errorResult("unsupported_feature", "This viewer does not support preferences_tools version 1; no generic control API was called", nil)
	}
	return apiResult(api.CallNamed(ctx, preferencesAPI, op, params))
}
func (s *Server) preferencesSchema(ctx context.Context, _ *mcp.CallToolRequest, args preferencesViewerArgs) (*mcp.CallToolResult, any, error) {
	return s.preferencesCall(ctx, args.ViewerSession, args.ViewerPID, "schema", map[string]any{})
}
func (s *Server) preferencesStatus(ctx context.Context, _ *mcp.CallToolRequest, args preferencesViewerArgs) (*mcp.CallToolResult, any, error) {
	return s.preferencesCall(ctx, args.ViewerSession, args.ViewerPID, "status", map[string]any{})
}
func (s *Server) preferencesGet(ctx context.Context, _ *mcp.CallToolRequest, args preferencesGetArgs) (*mcp.CallToolResult, any, error) {
	params := map[string]any{}
	if args.IDs != nil {
		if len(args.IDs) < 1 || len(args.IDs) > 16 {
			return errorResult("invalid_args", "ids must contain 1..16 unique curated IDs", nil)
		}
		seen := map[string]bool{}
		for _, id := range args.IDs {
			if _, exists := preferenceControls[id]; !exists || seen[id] {
				return errorResult("invalid_args", "Unknown or duplicate curated preference ID", nil)
			}
			seen[id] = true
		}
		params["ids"] = args.IDs
	}
	return s.preferencesCall(ctx, args.ViewerSession, args.ViewerPID, "get", params)
}
func (s *Server) preferencesPreview(ctx context.Context, _ *mcp.CallToolRequest, args preferencesPreviewArgs) (*mcp.CallToolResult, any, error) {
	if !preferencesRevision(args.ExpectedRevision) || !preferencesChanges(args.Changes) || (args.TTLSeconds != nil && (*args.TTLSeconds < 1 || *args.TTLSeconds > 300)) {
		return errorResult("invalid_args", "Use an exact expected_revision, 1..16 typed curated changes and ttl_seconds 1..300", nil)
	}
	params := map[string]any{"changes": args.Changes, "expected_revision": args.ExpectedRevision}
	if args.TTLSeconds != nil {
		params["ttl_seconds"] = *args.TTLSeconds
	}
	return s.preferencesCall(ctx, args.ViewerSession, args.ViewerPID, "preview", params)
}
func (s *Server) preferencesTransaction(ctx context.Context, args preferencesTransactionArgs, op string) (*mcp.CallToolResult, any, error) {
	if !preferencesRevision(args.ExpectedRevision) || args.TransactionID == "" || len(args.TransactionID) > 64 || strings.TrimSpace(args.TransactionID) != args.TransactionID {
		return errorResult("invalid_args", "An exact transaction_id and current expected_revision are required", nil)
	}
	return s.preferencesCall(ctx, args.ViewerSession, args.ViewerPID, op, map[string]any{"transaction_id": args.TransactionID, "expected_revision": args.ExpectedRevision})
}
func (s *Server) preferencesAccept(ctx context.Context, _ *mcp.CallToolRequest, args preferencesTransactionArgs) (*mcp.CallToolResult, any, error) {
	return s.preferencesTransaction(ctx, args, "accept")
}
func (s *Server) preferencesCancel(ctx context.Context, _ *mcp.CallToolRequest, args preferencesTransactionArgs) (*mcp.CallToolResult, any, error) {
	return s.preferencesTransaction(ctx, args, "cancel")
}
