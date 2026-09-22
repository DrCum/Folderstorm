package mcptools

import (
	"context"

	"fs-mcp/internal/viewerapi"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func registerAppearance(s *mcp.Server, state *Server, readOnly, destructive *mcp.ToolAnnotations) {
	mcp.AddTool(s, &mcp.Tool{
		Name:        "appearance_outfits",
		Description: "List outfit folders under My Outfits.",
		Annotations: withTitle(readOnly, "List outfits"),
	}, state.appearanceOutfits)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "appearance_outfit_items",
		Description: "List items inside an outfit folder UUID.",
		Annotations: withTitle(readOnly, "Outfit items"),
	}, state.appearanceOutfitItems)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "appearance_worn",
		Description: "List the Current Outfit, including wearable type and attachment point. Poll this after wearing; baking is asynchronous.",
		Annotations: withTitle(readOnly, "Worn items"),
	}, state.appearanceWorn)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "appearance_wear_outfit",
		Description: "Wear an outfit by folder_id or folder_name. append adds it to the current outfit. Requires confirmation.",
		Annotations: withTitle(destructive, "Wear outfit"),
	}, state.appearanceWearOutfit)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "appearance_wear_items",
		Description: "Wear inventory items by UUID. replace removes conflicting worn items. Requires confirmation.",
		Annotations: withTitle(destructive, "Wear items"),
	}, state.appearanceWearItems)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "appearance_detach",
		Description: "Detach or remove worn items by UUID. Requires confirmation.",
		Annotations: withTitle(destructive, "Detach items"),
	}, state.appearanceDetach)
}

type viewerArgs struct {
	ViewerPID int `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type outfitItemsArgs struct {
	OutfitID  string `json:"outfit_id" jsonschema:"outfit folder UUID"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type wearOutfitArgs struct {
	FolderID        string `json:"folder_id,omitempty" jsonschema:"outfit folder UUID"`
	FolderName      string `json:"folder_name,omitempty" jsonschema:"outfit folder name"`
	Append          bool   `json:"append,omitempty" jsonschema:"add to the current outfit instead of replacing it"`
	SkipElicitation bool   `json:"skip_elicitation,omitempty" jsonschema:"return plan_id instead of prompting"`
	ViewerPID       int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type wearItemsArgs struct {
	ItemsID         any  `json:"items_id" jsonschema:"one item UUID or an array of item UUIDs"`
	Replace         bool `json:"replace,omitempty" jsonschema:"replace worn items of the same type"`
	SkipElicitation bool `json:"skip_elicitation,omitempty" jsonschema:"return plan_id instead of prompting"`
	ViewerPID       int  `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func (s *Server) appearanceOutfits(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	_, api, errRes := s.resolve(args.ViewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.AppearanceAPI, "getOutfitsList", map[string]any{}))
}

func (s *Server) appearanceOutfitItems(ctx context.Context, _ *mcp.CallToolRequest, args outfitItemsArgs) (*mcp.CallToolResult, any, error) {
	if args.OutfitID == "" {
		return errorResult("invalid_args", "outfit_id is required", nil)
	}
	_, api, errRes := s.resolve(args.ViewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.AppearanceAPI, "getOutfitItems", map[string]any{"outfit_id": args.OutfitID}))
}

func (s *Server) appearanceWorn(ctx context.Context, _ *mcp.CallToolRequest, args viewerArgs) (*mcp.CallToolResult, any, error) {
	_, api, errRes := s.resolve(args.ViewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.CallNamed(ctx, viewerapi.AppearanceAPI, "worn", map[string]any{}))
}

func (s *Server) appearanceWearOutfit(ctx context.Context, req *mcp.CallToolRequest, args wearOutfitArgs) (*mcp.CallToolResult, any, error) {
	if args.FolderID == "" && args.FolderName == "" {
		return errorResult("invalid_args", "folder_id or folder_name is required", nil)
	}
	params := map[string]any{"append": args.Append}
	if args.FolderID != "" {
		params["folder_id"] = args.FolderID
	}
	if args.FolderName != "" {
		params["folder_name"] = args.FolderName
	}
	message := "Wearing this outfit changes what other people see. Confirm to replace the current outfit."
	if args.Append {
		message = "Wearing this outfit adds it to what you are already wearing."
	}
	return s.gateConfirm(ctx, req, args.ViewerPID, viewerapi.AppearanceAPI, "wearOutfit", params, message, args.SkipElicitation)
}

func (s *Server) appearanceWearItems(ctx context.Context, req *mcp.CallToolRequest, args wearItemsArgs) (*mcp.CallToolResult, any, error) {
	if args.ItemsID == nil || args.ItemsID == "" {
		return errorResult("invalid_args", "items_id is required", nil)
	}
	params := map[string]any{"items_id": args.ItemsID, "replace": args.Replace}
	return s.gateConfirm(ctx, req, args.ViewerPID, viewerapi.AppearanceAPI, "wearItems", params,
		"Wearing these items changes what other people see.", args.SkipElicitation)
}

func (s *Server) appearanceDetach(ctx context.Context, req *mcp.CallToolRequest, args wearItemsArgs) (*mcp.CallToolResult, any, error) {
	if args.ItemsID == nil || args.ItemsID == "" {
		return errorResult("invalid_args", "items_id is required", nil)
	}
	return s.gateConfirm(ctx, req, args.ViewerPID, viewerapi.AppearanceAPI, "detachItems", map[string]any{"items_id": args.ItemsID},
		"Detaching these items changes what other people see.", args.SkipElicitation)
}
