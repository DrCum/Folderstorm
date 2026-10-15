package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/viewerapi"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"regexp"
	"strings"
	"unicode/utf8"
)

const contentTextLimit = 65536

var contentUUID = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$`)
var contentHash = regexp.MustCompile(`^[0-9a-f]{64}$`)

func validContentUUID(id string) bool {
	return contentUUID.MatchString(id) && id != "00000000-0000-0000-0000-000000000000"
}
func registerContent(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_content_get", Description: "Read complete UTF-8 source and a versioned edit snapshot from an exact native-readable agent inventory or Library notecard/script item. Resolve links first. Native COPY is required; scripts also require MODIFY except in Library. Returns complete/truncated, content_hash and asset_id. Requires Read and content_editing version 1.", Annotations: withTitle(readOnly, "Read inventory source")}, state.contentGet)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_content_preview", Description: "Preview a bounded source diff using expected_asset_id and content_hash from a complete inventory_content_get. Text only, at most 65536 UTF-8 bytes. Preserve embedded notecard markers. Scripts require compiler=mono. No file or URL inputs. Requires Read and content_editing version 1.", Annotations: withTitle(readOnly, "Preview source changes")}, state.contentPreview)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_update_notecard", Description: "Replace text in an existing owned agent-inventory notecard, preserving embedded inventory losslessly. Requires a complete inventory_content_get snapshot, expected_asset_id, content_hash, native COPY+MODIFY, Read and Content Edit. Close native editors and wait for uploads. Viewer owns Ask. Revision checks are best-effort; the server has no atomic expected-revision capability. No automatic retry or rollback on uncertain outcomes.", Annotations: withTitle(mutating, "Save notecard source")}, state.contentNotecard)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_content_operation_status", Description: "Read source-bound content operation details after a save or timeout. Includes confirmed versus unknown source save and compiler outcomes when known. Use operation_id returned by the same viewer connection. Requires Read and current native content read eligibility; no retry is performed.", Annotations: withTitle(readOnly, "Content operation status")}, state.contentStatus)
}

type contentGetArgs struct {
	ID            string `json:"id" jsonschema:"exact inventory item UUID; resolve links first"`
	ViewerSession string `json:"viewer_session,omitempty"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type contentEditArgs struct {
	ID              string `json:"id" jsonschema:"exact existing inventory item UUID"`
	ExpectedAssetID string `json:"expected_asset_id" jsonschema:"asset_id from a complete inventory_content_get"`
	ContentHash     string `json:"content_hash" jsonschema:"SHA-256 content_hash from the complete read"`
	Text            string `json:"text" jsonschema:"complete replacement UTF-8 text, at most 65536 bytes; preserve embedded item markers"`
	Compiler        string `json:"compiler,omitempty" jsonschema:"mono for raw LSL scripts; omit for notecards"`
	ViewerSession   string `json:"viewer_session,omitempty"`
	ViewerPID       int    `json:"viewer_pid,omitempty"`
}
type contentStatusArgs struct {
	OperationID   string `json:"operation_id" jsonschema:"source-bound operation UUID returned by a content save"`
	ViewerSession string `json:"viewer_session,omitempty"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}

func validContentEdit(args contentEditArgs) bool {
	return validContentUUID(args.ID) && validContentUUID(args.ExpectedAssetID) && contentHash.MatchString(args.ContentHash) && len(args.Text) <= contentTextLimit && utf8.ValidString(args.Text) && !strings.ContainsRune(args.Text, 0) && (args.Compiler == "" || args.Compiler == "mono")
}
func (s *Server) contentCall(ctx context.Context, pid int, op string, params map[string]any, write bool) (*mcp.CallToolResult, any, error) {
	classes := []string{viewerapi.ClassRead}
	if write {
		classes = append(classes, viewerapi.ClassContentEdit)
	}
	api, policy, errRes := s.openClasses(ctx, pid, classes)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present || policy.Capabilities["content_editing"] != 1 {
		return errorResult("unsupported_feature", "Viewer content_editing version 1 is required; no legacy fallback was attempted", nil)
	}
	// Policy caching is not proof that this listener is live on the selected login.
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
	if len(raw) > 65536 || json.Unmarshal(raw, &health) != nil || !health.Ready || health.Capabilities["content_editing"] != 1 || !health.APIs["FSContent"] {
		return errorResult("unsupported_feature", "Selected viewer does not have a ready FSContent version 1 service", nil)
	}
	if selected, ok := requestIdentity(ctx); ok && selected.ViewerSession != "" && (selected.ViewerSession != health.Session || selected.Account != health.Account || selected.Grid != health.Grid) {
		return errorResult("session_changed", "Viewer identity changed before content access", nil)
	}
	return apiResult(api.CallNamed(ctx, "FSContent", op, params))
}
func (s *Server) contentGet(ctx context.Context, _ *mcp.CallToolRequest, args contentGetArgs) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validContentUUID(args.ID) {
		return errorResult("invalid_args", "id must be an exact non-null inventory item UUID", nil)
	}
	return s.contentCall(ctx, args.ViewerPID, "get", map[string]any{"id": args.ID}, false)
}
func contentParams(args contentEditArgs) map[string]any {
	p := map[string]any{"id": args.ID, "expected_asset_id": args.ExpectedAssetID, "content_hash": args.ContentHash, "text": args.Text}
	if args.Compiler != "" {
		p["compiler"] = args.Compiler
	}
	return p
}
func (s *Server) contentPreview(ctx context.Context, _ *mcp.CallToolRequest, args contentEditArgs) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validContentEdit(args) {
		return errorResult("invalid_args", "A complete snapshot's exact id, expected_asset_id, SHA-256 content_hash and bounded UTF-8 text are required", nil)
	}
	return s.contentCall(ctx, args.ViewerPID, "preview", contentParams(args), false)
}
func (s *Server) contentNotecard(ctx context.Context, _ *mcp.CallToolRequest, args contentEditArgs) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validContentEdit(args) || args.Compiler != "" {
		return errorResult("invalid_args", "Notecard save requires an exact snapshot and bounded UTF-8 text; compiler is only for scripts", nil)
	}
	return s.contentCall(ctx, args.ViewerPID, "saveNotecard", contentParams(args), true)
}
func (s *Server) contentStatus(ctx context.Context, _ *mcp.CallToolRequest, args contentStatusArgs) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if !validContentUUID(args.OperationID) {
		return errorResult("invalid_args", "operation_id must be the content operation UUID from this viewer connection", nil)
	}
	return s.contentCall(ctx, args.ViewerPID, "get", map[string]any{"operation_id": args.OperationID}, false)
}
