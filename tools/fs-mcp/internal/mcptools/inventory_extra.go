package mcptools

import (
	"context"

	"fs-mcp/internal/viewerapi"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func registerInventoryExtra(s *mcp.Server, state *Server, readOnly, mutating, destructive *mcp.ToolAnnotations) {
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_types", Description: "Return the folder type and asset type names the viewer accepts.", Annotations: withTitle(readOnly, "Inventory types")}, state.inventoryTypes)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_get_many", Description: "Fetch up to 50 inventory objects by UUID.", Annotations: withTitle(readOnly, "Get inventory objects")}, state.inventoryGetMany)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_resolve_path", Description: "Resolve an inventory path to every matching UUID. Names are not unique, so the result can be ambiguous.", Annotations: withTitle(readOnly, "Resolve inventory path")}, state.inventoryResolvePath)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_protected_folders", Description: "List protected folders. Does not change protection.", Annotations: withTitle(readOnly, "Protected folders")}, state.inventoryProtectedFolders)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_changes", Description: "Return inventory changes since a generation cursor from viewer_status.", Annotations: withTitle(readOnly, "Inventory changes")}, state.inventoryChanges)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_read_notecard", Description: "Read a notecard. The response is size-capped and includes asset_id and stale.", Annotations: withTitle(readOnly, "Read notecard")}, state.inventoryReadNotecard)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_read_script", Description: "Read an LSL script. The response is size-capped and includes asset_id and stale.", Annotations: withTitle(readOnly, "Read script")}, state.inventoryReadScript)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_landmark", Description: "Read a landmark's region and global position.", Annotations: withTitle(readOnly, "Read landmark")}, state.inventoryLandmark)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_set_description", Description: "Set the description of an item or folder.", Annotations: withTitle(mutating, "Set description")}, state.inventorySetDescription)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_set_thumbnail", Description: "Set or clear a thumbnail using a texture UUID.", Annotations: withTitle(mutating, "Set thumbnail")}, state.inventorySetThumbnail)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_set_favorite", Description: "Set or clear the favorite flag.", Annotations: withTitle(mutating, "Set favorite")}, state.inventorySetFavorite)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_link", Description: "Create a link to an item or folder in a destination folder.", Annotations: withTitle(mutating, "Create link")}, state.inventoryLink)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_create_item", Description: "Create a notecard, script, gesture, material, settings item, clothing, body part, or a landmark of the current location.", Annotations: withTitle(mutating, "Create inventory item")}, state.inventoryCreateItem)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_batch_move", Description: "Move up to 50 objects into one folder. Returns a per-item result.", Annotations: withTitle(destructive, "Batch move")}, state.inventoryBatchMove)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_batch_rename", Description: "Rename up to 50 objects. Returns a per-item result.", Annotations: withTitle(mutating, "Batch rename")}, state.inventoryBatchRename)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_batch_copy", Description: "Copy up to 50 items. No-copy items return a plan_id for inventory_confirm_copy. Folders use inventory_copy.", Annotations: withTitle(destructive, "Batch copy")}, state.inventoryBatchCopy)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_trash", Description: "Move an object into Trash. This can be restored.", Annotations: withTitle(destructive, "Move to trash")}, state.inventoryTrash)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_restore", Description: "Move an object out of Trash into its type folder.", Annotations: withTitle(mutating, "Restore from trash")}, state.inventoryRestore)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_replace_links", Description: "Point every link that targets source_id at target_id, and move the old links to Trash. Requires confirmation.", Annotations: withTitle(destructive, "Replace links")}, state.inventoryReplaceLinks)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_empty_trash", Description: "Permanently empty Trash. Requires confirmation.", Annotations: withTitle(destructive, "Empty trash")}, state.inventoryEmptyTrash)
	mcp.AddTool(s, &mcp.Tool{Name: "inventory_purge", Description: "Permanently delete one object. Requires confirmation.", Annotations: withTitle(destructive, "Purge inventory object")}, state.inventoryPurge)
}

type idsArgs struct {
	IDs       []string `json:"ids" jsonschema:"inventory UUIDs, at most 50"`
	ViewerPID int      `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type pathArgs struct {
	Path      string `json:"path" jsonschema:"inventory path such as Clothing/Outfits/Formal"`
	FolderID  string `json:"folder_id,omitempty" jsonschema:"folder UUID to start from instead of inventory root"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type sinceArgs struct {
	Since     int `json:"since,omitempty" jsonschema:"return changes with a generation greater than this"`
	ViewerPID int `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type descArgs struct {
	ID        string `json:"id" jsonschema:"item or folder UUID"`
	Desc      string `json:"desc" jsonschema:"new description"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type thumbArgs struct {
	ID          string `json:"id" jsonschema:"item or folder UUID"`
	ThumbnailID string `json:"thumbnail_id,omitempty" jsonschema:"texture UUID, empty to clear"`
	ViewerPID   int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type favoriteArgs struct {
	ID        string `json:"id" jsonschema:"item or folder UUID"`
	Favorite  bool   `json:"favorite" jsonschema:"true to mark favorite"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type linkArgs struct {
	ID        string `json:"id" jsonschema:"item or folder UUID to link"`
	ParentID  string `json:"parent_id" jsonschema:"destination folder UUID"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type createItemArgs struct {
	Type      string `json:"type" jsonschema:"notecard, lsl, gesture, material, landmark, sky, water, daycycle, or a wearable type such as shirt"`
	ParentID  string `json:"parent_id,omitempty" jsonschema:"destination folder UUID"`
	Name      string `json:"name,omitempty" jsonschema:"landmark name; other types use the viewer default"`
	Desc      string `json:"desc,omitempty" jsonschema:"landmark description"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type batchMoveArgs struct {
	IDs       []string `json:"ids" jsonschema:"objects to move"`
	ParentID  string   `json:"parent_id" jsonschema:"destination folder UUID"`
	ViewerPID int      `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type renameEntry struct {
	ID   string `json:"id" jsonschema:"object UUID"`
	Name string `json:"name" jsonschema:"new name"`
}

type batchRenameArgs struct {
	Items     []renameEntry `json:"items" jsonschema:"objects to rename"`
	ViewerPID int           `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type batchCopyArgs struct {
	IDs       []string `json:"ids" jsonschema:"items to copy"`
	ParentID  string   `json:"parent_id" jsonschema:"destination folder UUID"`
	Policy    string   `json:"policy,omitempty" jsonschema:"default, strict, or copyable_only"`
	ViewerPID int      `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type replaceLinksArgs struct {
	SourceID        string `json:"source_id" jsonschema:"UUID the existing links point at"`
	TargetID        string `json:"target_id" jsonschema:"UUID the new links should point at"`
	SkipElicitation bool   `json:"skip_elicitation,omitempty" jsonschema:"return plan_id instead of prompting"`
	ViewerPID       int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type purgeArgs struct {
	ID              string `json:"id" jsonschema:"object UUID to delete permanently"`
	SkipElicitation bool   `json:"skip_elicitation,omitempty" jsonschema:"return plan_id instead of prompting"`
	ViewerPID       int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func (s *Server) inventoryTypes(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "types", map[string]any{})
}

func (s *Server) inventoryGetMany(ctx context.Context, _ *mcp.CallToolRequest, args idsArgs) (*mcp.CallToolResult, any, error) {
	if len(args.IDs) == 0 || len(args.IDs) > 50 {
		return errorResult("invalid_args", "ids must contain between 1 and 50 UUIDs", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "getMany", map[string]any{"ids": args.IDs})
}

func (s *Server) inventoryResolvePath(ctx context.Context, _ *mcp.CallToolRequest, args pathArgs) (*mcp.CallToolResult, any, error) {
	if args.Path == "" {
		return errorResult("invalid_args", "path is required", nil)
	}
	params := map[string]any{"path": args.Path}
	if args.FolderID != "" {
		params["folder_id"] = args.FolderID
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "resolvePath", params)
}

func (s *Server) inventoryProtectedFolders(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "protectedFolders", map[string]any{})
}

func (s *Server) inventoryChanges(ctx context.Context, _ *mcp.CallToolRequest, args sinceArgs) (*mcp.CallToolResult, any, error) {
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "changes", map[string]any{"since": args.Since})
}

func (s *Server) inventoryReadNotecard(ctx context.Context, _ *mcp.CallToolRequest, args getArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "readNotecard", map[string]any{"id": args.ID})
}

func (s *Server) inventoryReadScript(ctx context.Context, _ *mcp.CallToolRequest, args getArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "readScript", map[string]any{"id": args.ID})
}

func (s *Server) inventoryLandmark(ctx context.Context, _ *mcp.CallToolRequest, args getArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "landmark", map[string]any{"id": args.ID})
}

func (s *Server) inventorySetDescription(ctx context.Context, _ *mcp.CallToolRequest, args descArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "setDescription", map[string]any{"id": args.ID, "desc": args.Desc})
}

func (s *Server) inventorySetThumbnail(ctx context.Context, _ *mcp.CallToolRequest, args thumbArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "setThumbnail", map[string]any{"id": args.ID, "thumbnail_id": args.ThumbnailID})
}

func (s *Server) inventorySetFavorite(ctx context.Context, _ *mcp.CallToolRequest, args favoriteArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "setFavorite", map[string]any{"id": args.ID, "favorite": args.Favorite})
}

func (s *Server) inventoryLink(ctx context.Context, _ *mcp.CallToolRequest, args linkArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" || args.ParentID == "" {
		return errorResult("invalid_args", "id and parent_id are required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "link", map[string]any{"id": args.ID, "parent_id": args.ParentID})
}

func (s *Server) inventoryCreateItem(ctx context.Context, _ *mcp.CallToolRequest, args createItemArgs) (*mcp.CallToolResult, any, error) {
	if args.Type == "" {
		return errorResult("invalid_args", "type is required", nil)
	}
	params := map[string]any{"type": args.Type}
	if args.ParentID != "" {
		params["parent_id"] = args.ParentID
	}
	if args.Name != "" {
		params["name"] = args.Name
	}
	if args.Desc != "" {
		params["desc"] = args.Desc
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "createItem", params)
}

func (s *Server) inventoryBatchMove(ctx context.Context, _ *mcp.CallToolRequest, args batchMoveArgs) (*mcp.CallToolResult, any, error) {
	if len(args.IDs) == 0 || len(args.IDs) > 50 || args.ParentID == "" {
		return errorResult("invalid_args", "ids (1-50) and parent_id are required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "batchMove", map[string]any{"ids": args.IDs, "parent_id": args.ParentID})
}

func (s *Server) inventoryBatchRename(ctx context.Context, _ *mcp.CallToolRequest, args batchRenameArgs) (*mcp.CallToolResult, any, error) {
	if len(args.Items) == 0 || len(args.Items) > 50 {
		return errorResult("invalid_args", "items must contain between 1 and 50 entries", nil)
	}
	items := make([]map[string]any, 0, len(args.Items))
	for _, item := range args.Items {
		if item.ID == "" || item.Name == "" {
			return errorResult("invalid_args", "each item needs id and name", nil)
		}
		items = append(items, map[string]any{"id": item.ID, "name": item.Name})
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "batchRename", map[string]any{"items": items})
}

func (s *Server) inventoryBatchCopy(ctx context.Context, _ *mcp.CallToolRequest, args batchCopyArgs) (*mcp.CallToolResult, any, error) {
	if len(args.IDs) == 0 || len(args.IDs) > 50 || args.ParentID == "" {
		return errorResult("invalid_args", "ids (1-50) and parent_id are required", nil)
	}
	params := map[string]any{"ids": args.IDs, "parent_id": args.ParentID}
	if args.Policy != "" {
		params["policy"] = args.Policy
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "batchCopy", params)
}

func (s *Server) inventoryTrash(ctx context.Context, _ *mcp.CallToolRequest, args getArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "trash", map[string]any{"id": args.ID})
}

func (s *Server) inventoryRestore(ctx context.Context, _ *mcp.CallToolRequest, args getArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.named(ctx, args.ViewerPID, viewerapi.DefaultAPI, "restore", map[string]any{"id": args.ID})
}

func (s *Server) inventoryReplaceLinks(ctx context.Context, req *mcp.CallToolRequest, args replaceLinksArgs) (*mcp.CallToolResult, any, error) {
	if args.SourceID == "" || args.TargetID == "" {
		return errorResult("invalid_args", "source_id and target_id are required", nil)
	}
	return s.gateConfirm(ctx, req, args.ViewerPID, viewerapi.DefaultAPI, "replaceLinks",
		map[string]any{"source_id": args.SourceID, "target_id": args.TargetID},
		"Replacing links creates new links to the target and moves the old links into Trash.",
		args.SkipElicitation)
}

func (s *Server) inventoryEmptyTrash(ctx context.Context, req *mcp.CallToolRequest, args wearOutfitArgs) (*mcp.CallToolResult, any, error) {
	return s.gateConfirm(ctx, req, args.ViewerPID, viewerapi.DefaultAPI, "emptyTrash", map[string]any{},
		"Emptying Trash permanently deletes everything in it.", args.SkipElicitation)
}

func (s *Server) inventoryPurge(ctx context.Context, req *mcp.CallToolRequest, args purgeArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	return s.gateConfirm(ctx, req, args.ViewerPID, viewerapi.DefaultAPI, "purge",
		map[string]any{"id": args.ID},
		"Purging permanently deletes this inventory object.",
		args.SkipElicitation)
}

func (s *Server) named(ctx context.Context, viewerPID int, apiName, op string, params map[string]any) (*mcp.CallToolResult, any, error) {
	_, api, errRes := s.resolve(viewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, apiName, op, params))
}
