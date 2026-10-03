package mcptools

import (
	"context"
	"strings"

	"fs-mcp/internal/viewerapi"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func registerInventoryBulkReview(server *mcp.Server, state *Server, readOnly, destructive *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{
		Name:        "inventory_preview_batch_rename",
		Description: "Preview up to 50 inventory renames without changing inventory. Returns a viewer-owned expiring plan and exact eligible/skipped rows. Requires Read and bulk_inventory_review capability version 1. Preview grants no execution permission.",
		Annotations: withTitle(readOnly, "Preview inventory renames"),
	}, state.inventoryPreviewBatchRename)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "inventory_preview_batch_move",
		Description: "Preview moving up to 50 exact inventory UUIDs into one folder without changing inventory. Returns a viewer-owned expiring plan and eligible/skipped rows. Requires Read and bulk_inventory_review capability version 1.",
		Annotations: withTitle(readOnly, "Preview inventory moves"),
	}, state.inventoryPreviewBatchMove)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "inventory_execute_plan",
		Description: "Execute a plan from inventory_preview_batch_rename, inventory_preview_batch_move, or inventory_preview_undo. The viewer derives and rechecks Edit or Move permissions; Ask prompts in the viewer. A plan is consumed once, so repeated execution returns its existing operation instead of replaying writes. Submitted/unconfirmed rows are not confirmed success. Requires bulk_inventory_review version 1; Read is not required to execute, and names/paths are redacted when Read is Never.",
		Annotations: withTitle(destructive, "Execute reviewed inventory plan"),
	}, state.inventoryExecutePlan)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "inventory_history",
		Description: "Read bounded in-memory assistant inventory history for the selected viewer login session. Optional operation_id returns details; otherwise offset/limit paginate operations (limit 1-50, default 20). Submitted/unconfirmed outcomes cannot safely be retried or undone. Requires Read and bulk_inventory_review version 1.",
		Annotations: withTitle(readOnly, "Assistant inventory history"),
	}, state.inventoryHistory)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "inventory_preview_undo",
		Description: "Preview a limited inverse for server-confirmed eligible rename/move history rows. Optional ids selects an exact subset (1-50); omit it to review all eligible rows. No inventory changes until inventory_execute_plan. Current state and Edit/Move permissions are rechecked by the viewer; unavailable recovery is reported. Requires Read and bulk_inventory_review version 1.",
		Annotations: withTitle(readOnly, "Review inventory undo"),
	}, state.inventoryPreviewUndo)
}

type bulkPlanArgs struct {
	PlanID    string `json:"plan_id" jsonschema:"viewer-owned plan UUID from a bulk preview"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type bulkHistoryArgs struct {
	OperationID string `json:"operation_id,omitempty" jsonschema:"optional operation UUID for detailed outcomes"`
	Offset      int    `json:"offset,omitempty" jsonschema:"nonnegative operations offset"`
	Limit       int    `json:"limit,omitempty" jsonschema:"maximum operations to return, 1-50, default 20"`
	ViewerPID   int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type bulkUndoArgs struct {
	OperationID string   `json:"operation_id" jsonschema:"history operation UUID to review for undo"`
	IDs         []string `json:"ids,omitempty" jsonschema:"optional exact subset of 1-50 inventory UUIDs; omit for all eligible rows"`
	ViewerPID   int      `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func validBulkID(id string) bool {
	return id != "" && len(id) <= 128 && strings.TrimSpace(id) == id
}

func uniqueBulkIDs(ids []string) bool {
	if len(ids) < 1 || len(ids) > 50 {
		return false
	}
	seen := make(map[string]bool, len(ids))
	for _, id := range ids {
		if !validBulkID(id) || seen[id] {
			return false
		}
		seen[id] = true
	}
	return true
}

// bulkReview calls only the new protocol, never a legacy mutating fallback.
// Execute deliberately has no sidecar-supplied permission class: the viewer
// resolves it from its authenticated session-owned plan, and can execute with
// Read disabled without exporting stored display fields.
func (s *Server) bulkReview(ctx context.Context, viewerPID int, op string, params map[string]any, requireRead bool) (*mcp.CallToolResult, any, error) {
	inst, api, errRes := s.resolve(viewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	policy, err := s.loadPolicy(ctx, inst.PID, api)
	if err != nil {
		return apiError(err)
	}
	if !policy.Present || policy.BulkInventoryReview != viewerapi.BulkInventoryReviewVersion {
		return errorResult("unsupported_feature", "This viewer does not support bulk inventory review version 1. Update the viewer; no legacy mutation was attempted.", nil)
	}
	if requireRead && policy.Level(viewerapi.ClassRead) == "deny" {
		return errorResult("not_permitted", "Reading inventory previews and history is not allowed by the viewer permission settings", map[string]any{"class": viewerapi.ClassRead})
	}
	return apiResult(api.CallNamed(ctx, viewerapi.DefaultAPI, op, params))
}

func (s *Server) inventoryPreviewBatchRename(ctx context.Context, _ *mcp.CallToolRequest, args batchRenameArgs) (*mcp.CallToolResult, any, error) {
	if len(args.Items) < 1 || len(args.Items) > 50 {
		return errorResult("invalid_args", "items must contain between 1 and 50 entries", nil)
	}
	items := make([]map[string]any, 0, len(args.Items))
	ids := make([]string, 0, len(args.Items))
	for _, item := range args.Items {
		if strings.TrimSpace(item.Name) == "" {
			return errorResult("invalid_args", "each item needs a nonempty name", nil)
		}
		ids = append(ids, item.ID)
		items = append(items, map[string]any{"id": item.ID, "name": item.Name})
	}
	if !uniqueBulkIDs(ids) {
		return errorResult("invalid_args", "items must use distinct nonempty inventory UUIDs", nil)
	}
	return s.bulkReview(ctx, args.ViewerPID, "previewBatchRename", map[string]any{"items": items}, true)
}

func (s *Server) inventoryPreviewBatchMove(ctx context.Context, _ *mcp.CallToolRequest, args batchMoveArgs) (*mcp.CallToolResult, any, error) {
	if !uniqueBulkIDs(args.IDs) || !validBulkID(args.ParentID) {
		return errorResult("invalid_args", "ids must contain 1-50 distinct UUIDs and parent_id is required", nil)
	}
	return s.bulkReview(ctx, args.ViewerPID, "previewBatchMove", map[string]any{"ids": args.IDs, "parent_id": args.ParentID}, true)
}

func (s *Server) inventoryExecutePlan(ctx context.Context, _ *mcp.CallToolRequest, args bulkPlanArgs) (*mcp.CallToolResult, any, error) {
	if !validBulkID(args.PlanID) {
		return errorResult("invalid_args", "plan_id is required", nil)
	}
	return s.bulkReview(ctx, args.ViewerPID, "executeBulkPlan", map[string]any{"plan_id": args.PlanID}, false)
}

func (s *Server) inventoryHistory(ctx context.Context, _ *mcp.CallToolRequest, args bulkHistoryArgs) (*mcp.CallToolResult, any, error) {
	if args.Offset < 0 || args.Limit < 0 || args.Limit > 50 || (args.OperationID != "" && !validBulkID(args.OperationID)) {
		return errorResult("invalid_args", "offset must be nonnegative, limit 1-50, and operation_id a nonempty UUID when supplied", nil)
	}
	if args.Limit == 0 {
		args.Limit = 20
	}
	params := map[string]any{"offset": args.Offset, "limit": args.Limit}
	if args.OperationID != "" {
		params["operation_id"] = args.OperationID
	}
	return s.bulkReview(ctx, args.ViewerPID, "bulkHistory", params, true)
}

func (s *Server) inventoryPreviewUndo(ctx context.Context, _ *mcp.CallToolRequest, args bulkUndoArgs) (*mcp.CallToolResult, any, error) {
	if !validBulkID(args.OperationID) || (args.IDs != nil && !uniqueBulkIDs(args.IDs)) {
		return errorResult("invalid_args", "operation_id is required; supplied ids must contain 1-50 distinct UUIDs", nil)
	}
	params := map[string]any{"operation_id": args.OperationID}
	if args.IDs != nil {
		params["ids"] = args.IDs
	}
	return s.bulkReview(ctx, args.ViewerPID, "previewBulkUndo", params, true)
}
