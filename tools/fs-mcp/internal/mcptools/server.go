package mcptools

import (
	"context"
	"encoding/json"
	"fmt"
	"strings"
	"sync"

	"fs-mcp/internal/discover"
	"fs-mcp/internal/viewerapi"

	"github.com/google/jsonschema-go/jsonschema"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const (
	Name    = "fs-mcp"
	Version = "0.1.0"
)

// API is the viewer Event API surface the sidecar needs.
type API interface {
	Status(ctx context.Context) (json.RawMessage, error)
	Get(ctx context.Context, id string) (json.RawMessage, error)
	List(ctx context.Context, params map[string]any) (json.RawMessage, error)
	Search(ctx context.Context, params map[string]any) (json.RawMessage, error)
	SystemFolder(ctx context.Context, ftName string) (json.RawMessage, error)
	CreateFolder(ctx context.Context, params map[string]any) (json.RawMessage, error)
	Move(ctx context.Context, params map[string]any) (json.RawMessage, error)
	Rename(ctx context.Context, params map[string]any) (json.RawMessage, error)
	Copy(ctx context.Context, params map[string]any) (json.RawMessage, error)
	ConfirmCopy(ctx context.Context, params map[string]any) (json.RawMessage, error)
	CallNamed(ctx context.Context, apiName, op string, params map[string]any) (json.RawMessage, error)
}

// Finder locates live viewer discovery files.
type Finder func() ([]discover.Instance, error)

// ClientFactory builds an Event API client for one instance.
type ClientFactory func(discover.Instance) API

// Server holds MCP tool state, including multi-viewer selection.
type Server struct {
	Find      Finder
	NewClient ClientFactory

	mu          sync.Mutex
	selectedPID int
	plans       map[string]storedPlan
	policies    map[int]viewerapi.StatusPolicy
}

// New returns a sidecar tool server with production discovery defaults.
func New() *Server {
	return &Server{
		Find: discover.Find,
		NewClient: func(inst discover.Instance) API {
			return viewerapi.New(inst)
		},
	}
}

// NewMCPServer registers Firestorm inventory tools on an MCP server.
func NewMCPServer(state *Server) *mcp.Server {
	if state == nil {
		state = New()
	}
	s := mcp.NewServer(&mcp.Implementation{Name: Name, Version: Version}, &mcp.ServerOptions{
		Instructions: "Local Firestorm sidecar for inventory, appearance, and camera. Talks only to a loopback Event API bridge. Address inventory by UUID. Wear, detach, replace-links, and moving no-copy items during a copy ask in the viewer unless that permission is Allow. Trash is its own permission and defaults to Allow. Permanent delete (purge and empty trash) is not available. Task inventory and Marketplace folders are not available.",
	})

	readOnly := &mcp.ToolAnnotations{ReadOnlyHint: true, OpenWorldHint: boolPtr(false), Title: ""}
	mutating := &mcp.ToolAnnotations{ReadOnlyHint: false, DestructiveHint: boolPtr(false), OpenWorldHint: boolPtr(false)}
	destructive := &mcp.ToolAnnotations{ReadOnlyHint: false, DestructiveHint: boolPtr(true), OpenWorldHint: boolPtr(false)}

	mcp.AddTool(s, &mcp.Tool{
		Name:        "viewer_status",
		Description: "Report the selected Firestorm viewer, discovery metadata (token redacted), and Event API status.",
		Annotations: withTitle(readOnly, "Viewer status"),
	}, state.viewerStatus)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "viewer_list",
		Description: "List live Firestorm viewers discovered from per-PID JSON files.",
		Annotations: withTitle(readOnly, "List viewers"),
	}, state.viewerList)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "viewer_select",
		Description: "Select which Firestorm viewer to use when more than one is running.",
		Annotations: withTitle(mutating, "Select viewer"),
	}, state.viewerSelect)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_get",
		Description: "Fetch one inventory item or folder by UUID, waiting for incomplete data when the viewer supports it.",
		Annotations: withTitle(readOnly, "Get inventory object"),
	}, state.inventoryGet)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_list",
		Description: "List direct descendants of an inventory folder UUID.",
		Annotations: withTitle(readOnly, "List inventory folder"),
	}, state.inventoryList)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_search",
		Description: "Search inventory by name/description/type under an optional folder UUID.",
		Annotations: withTitle(readOnly, "Search inventory"),
	}, state.inventorySearch)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_system_folder",
		Description: "Return the UUID of a system folder (Textures, Sounds, Objects, ...).",
		Annotations: withTitle(readOnly, "System folder"),
	}, state.inventorySystemFolder)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_create_folder",
		Description: "Create a folder under parent_id.",
		Annotations: withTitle(mutating, "Create inventory folder"),
	}, state.inventoryCreateFolder)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_move",
		Description: "Move an inventory object to a destination folder. Does not copy; no-copy items are moved.",
		Annotations: withTitle(destructive, "Move inventory object"),
	}, state.inventoryMove)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_rename",
		Description: "Rename an inventory item or folder by UUID.",
		Annotations: withTitle(mutating, "Rename inventory object"),
	}, state.inventoryRename)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_copy",
		Description: "Copy an item or folder. Copyable items follow Move and copy. Unique no-copy items follow Move no-copy items during a copy. On a current viewer, Ask is a viewer dialog. Policies: default, strict, copyable_only.",
		Annotations: withTitle(destructive, "Copy inventory"),
	}, state.inventoryCopy)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "inventory_confirm_copy",
		Description: "Approve or decline a pending no-copy copy plan on an older viewer. On a current viewer, Ask is a dialog in the viewer and this tool is not how that question is asked. Approval moves unique no-copy items out of the source into the destination.",
		Annotations: withTitle(destructive, "Confirm no-copy copy"),
	}, state.inventoryConfirmCopy)
	registerInventoryExtra(s, state, readOnly, mutating, destructive)
	registerInventoryBulkReview(s, state, readOnly, destructive)
	registerAppearance(s, state, readOnly, destructive)
	registerCamera(s, state, readOnly, mutating)
	mcp.AddTool(s, &mcp.Tool{
		Name:        "confirm_action",
		Description: "Resume a wear, detach, or link-replacement plan stored for an older viewer. A current viewer asks in its own dialog instead. Purge and empty-trash plans are denied and are not sent.",
		Annotations: withTitle(destructive, "Confirm action"),
	}, state.confirmAction)
	return s
}

func withTitle(base *mcp.ToolAnnotations, title string) *mcp.ToolAnnotations {
	out := *base
	out.Title = title
	return &out
}

func boolPtr(v bool) *bool { return &v }

type viewerPIDArgs struct {
	ViewerPID int `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id when several viewers are running"`
}

type selectArgs struct {
	PID int `json:"pid" jsonschema:"Firestorm process id from viewer_list"`
}

type getArgs struct {
	ID        string `json:"id" jsonschema:"inventory item or folder UUID"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type listArgs struct {
	FolderID  string `json:"folder_id" jsonschema:"folder UUID to list"`
	Limit     int    `json:"limit,omitempty" jsonschema:"maximum number of children to return"`
	Offset    int    `json:"offset,omitempty" jsonschema:"number of children to skip"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type searchArgs struct {
	Query         string `json:"query,omitempty" jsonschema:"name substring to match"`
	Name          string `json:"name,omitempty" jsonschema:"alias of query"`
	Desc          string `json:"desc,omitempty" jsonschema:"description substring"`
	Type          string `json:"type,omitempty" jsonschema:"asset type name"`
	FolderID      string `json:"folder_id,omitempty" jsonschema:"folder UUID to search under"`
	FilterLinks   string `json:"filter_links,omitempty" jsonschema:"INCLUDE_LINKS, EXCLUDE_LINKS, or ONLY_LINKS"`
	IncludeTrash  *bool  `json:"include_trash,omitempty" jsonschema:"when true, search Trash as well"`
	CreatorID     string `json:"creator_id,omitempty" jsonschema:"creator UUID"`
	CreatorName   string `json:"creator_name,omitempty" jsonschema:"creator name substring when the viewer name cache already has it"`
	InvType       string `json:"inv_type,omitempty" jsonschema:"inventory type name"`
	LinkedID      string `json:"linked_id,omitempty" jsonschema:"return links that point at this UUID"`
	Worn          *bool  `json:"worn,omitempty" jsonschema:"filter by worn state"`
	Copyable      *bool  `json:"copyable,omitempty" jsonschema:"filter by copy permission"`
	Modifiable    *bool  `json:"modifiable,omitempty" jsonschema:"filter by modify permission"`
	Favorite      *bool  `json:"favorite,omitempty" jsonschema:"filter by favorite flag"`
	CreatedAfter  int    `json:"created_after,omitempty" jsonschema:"unix timestamp lower bound"`
	CreatedBefore int    `json:"created_before,omitempty" jsonschema:"unix timestamp upper bound"`
	Limit         int    `json:"limit,omitempty" jsonschema:"maximum number of results"`
	ViewerPID     int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type systemFolderArgs struct {
	Type      string `json:"type,omitempty" jsonschema:"system folder type name, for example Textures"`
	FtName    string `json:"ft_name,omitempty" jsonschema:"alias of type"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type createFolderArgs struct {
	ParentID  string `json:"parent_id" jsonschema:"destination parent folder UUID"`
	Name      string `json:"name" jsonschema:"new folder name"`
	Type      string `json:"type,omitempty" jsonschema:"optional preferred folder type"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type moveArgs struct {
	ID        string `json:"id" jsonschema:"item or folder UUID to move"`
	ParentID  string `json:"parent_id" jsonschema:"destination folder UUID"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type renameArgs struct {
	ID        string `json:"id" jsonschema:"item or folder UUID"`
	Name      string `json:"name" jsonschema:"new name"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type copyArgs struct {
	ID              string `json:"id" jsonschema:"item or folder UUID to copy"`
	ParentID        string `json:"parent_id" jsonschema:"destination folder UUID"`
	Policy          string `json:"policy,omitempty" jsonschema:"default, strict, or copyable_only"`
	SkipElicitation bool   `json:"skip_elicitation,omitempty" jsonschema:"if true, return plan_id instead of prompting for no-copy confirmation"`
	ViewerPID       int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type confirmCopyArgs struct {
	PlanID    string `json:"plan_id" jsonschema:"plan_id returned by inventory_copy"`
	Confirm   *bool  `json:"confirm,omitempty" jsonschema:"true (default) completes no-copy moves; false leaves unique items in the source"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

func (s *Server) viewerStatus(ctx context.Context, _ *mcp.CallToolRequest, args viewerPIDArgs) (*mcp.CallToolResult, any, error) {
	inst, api, errRes := s.resolve(args.ViewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	payload := map[string]any{
		"viewer": inst.Redacted(),
	}
	status, err := api.Status(ctx)
	if err != nil {
		payload["bridge_error"] = err.Error()
		payload["connected"] = false
		if ve, ok := err.(*viewerapi.Error); ok {
			if policy, parsed := viewerapi.ParseStatusPolicy(ve.Body); parsed && policy.Present {
				s.rememberPolicy(inst.PID, policy)
				payload["status"] = json.RawMessage(ve.Body)
			}
		}
	} else {
		payload["connected"] = true
		payload["status"] = json.RawMessage(status)
		if policy, parsed := viewerapi.ParseStatusPolicy(status); parsed {
			s.rememberPolicy(inst.PID, policy)
		}
	}
	return jsonResult(payload)
}

func (s *Server) viewerList(_ context.Context, _ *mcp.CallToolRequest, _ struct{}) (*mcp.CallToolResult, any, error) {
	insts, err := s.find()
	if err != nil {
		return errorResult("discovery_failed", err.Error(), nil)
	}
	redacted := make([]discover.Instance, 0, len(insts))
	for _, inst := range insts {
		redacted = append(redacted, inst.Redacted())
	}
	s.mu.Lock()
	selected := s.selectedPID
	s.mu.Unlock()
	return jsonResult(map[string]any{
		"viewers":      redacted,
		"selected_pid": selected,
		"count":        len(redacted),
	})
}

func (s *Server) viewerSelect(_ context.Context, _ *mcp.CallToolRequest, args selectArgs) (*mcp.CallToolResult, any, error) {
	if args.PID == 0 {
		return errorResult("invalid_args", "pid is required", nil)
	}
	insts, err := s.find()
	if err != nil {
		return errorResult("discovery_failed", err.Error(), nil)
	}
	var found *discover.Instance
	for i := range insts {
		if insts[i].PID == args.PID {
			found = &insts[i]
			break
		}
	}
	if found == nil {
		return errorResult("viewer_not_found", fmt.Sprintf("no live viewer with pid %d", args.PID), map[string]any{
			"viewers": redactedInstances(insts),
		})
	}
	s.mu.Lock()
	s.selectedPID = found.PID
	s.mu.Unlock()
	return jsonResult(map[string]any{
		"selected": found.Redacted(),
	})
}

func (s *Server) inventoryGet(ctx context.Context, _ *mcp.CallToolRequest, args getArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" {
		return errorResult("invalid_args", "id is required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassRead)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.Get(ctx, args.ID))
}

func (s *Server) inventoryList(ctx context.Context, _ *mcp.CallToolRequest, args listArgs) (*mcp.CallToolResult, any, error) {
	if args.FolderID == "" {
		return errorResult("invalid_args", "folder_id is required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassRead)
	if errRes != nil {
		return errRes, nil, nil
	}
	params := map[string]any{"folder_id": args.FolderID}
	if args.Limit > 0 {
		params["limit"] = args.Limit
	}
	if args.Offset > 0 {
		params["offset"] = args.Offset
	}
	return apiResult(api.List(ctx, params))
}

func (s *Server) inventorySearch(ctx context.Context, _ *mcp.CallToolRequest, args searchArgs) (*mcp.CallToolResult, any, error) {
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassRead)
	if errRes != nil {
		return errRes, nil, nil
	}
	query := firstNonEmpty(args.Query, args.Name)
	params := map[string]any{}
	if query != "" {
		params["name"] = query
		params["query"] = query
	}
	if args.Desc != "" {
		params["desc"] = args.Desc
	}
	if args.Type != "" {
		params["type"] = args.Type
	}
	if args.FolderID != "" {
		params["folder_id"] = args.FolderID
	}
	if args.FilterLinks != "" {
		params["filter_links"] = args.FilterLinks
	}
	if args.IncludeTrash != nil {
		params["include_trash"] = *args.IncludeTrash
	}
	if args.CreatorID != "" {
		params["creator_id"] = args.CreatorID
	}
	if args.CreatorName != "" {
		params["creator_name"] = args.CreatorName
	}
	if args.InvType != "" {
		params["inv_type"] = args.InvType
	}
	if args.LinkedID != "" {
		params["linked_id"] = args.LinkedID
	}
	if args.Worn != nil {
		params["worn"] = *args.Worn
	}
	if args.Copyable != nil {
		params["copyable"] = *args.Copyable
	}
	if args.Modifiable != nil {
		params["modifiable"] = *args.Modifiable
	}
	if args.Favorite != nil {
		params["favorite"] = *args.Favorite
	}
	if args.CreatedAfter > 0 {
		params["created_after"] = args.CreatedAfter
	}
	if args.CreatedBefore > 0 {
		params["created_before"] = args.CreatedBefore
	}
	if args.Limit > 0 {
		params["limit"] = args.Limit
	}
	return apiResult(api.Search(ctx, params))
}

func (s *Server) inventorySystemFolder(ctx context.Context, _ *mcp.CallToolRequest, args systemFolderArgs) (*mcp.CallToolResult, any, error) {
	ft := firstNonEmpty(args.Type, args.FtName)
	if ft == "" {
		return errorResult("invalid_args", "type or ft_name is required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassRead)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.SystemFolder(ctx, ft))
}

func (s *Server) inventoryCreateFolder(ctx context.Context, _ *mcp.CallToolRequest, args createFolderArgs) (*mcp.CallToolResult, any, error) {
	if args.ParentID == "" || args.Name == "" {
		return errorResult("invalid_args", "parent_id and name are required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassCreate)
	if errRes != nil {
		return errRes, nil, nil
	}
	params := map[string]any{"parent_id": args.ParentID, "name": args.Name}
	if args.Type != "" {
		params["type"] = args.Type
	}
	return apiResult(api.CreateFolder(ctx, params))
}

func (s *Server) inventoryMove(ctx context.Context, _ *mcp.CallToolRequest, args moveArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" || args.ParentID == "" {
		return errorResult("invalid_args", "id and parent_id are required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassMove)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.Move(ctx, map[string]any{"id": args.ID, "parent_id": args.ParentID}))
}

func (s *Server) inventoryRename(ctx context.Context, _ *mcp.CallToolRequest, args renameArgs) (*mcp.CallToolResult, any, error) {
	if args.ID == "" || args.Name == "" {
		return errorResult("invalid_args", "id and name are required", nil)
	}
	api, _, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassEdit)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.Rename(ctx, map[string]any{"id": args.ID, "name": args.Name}))
}

func (s *Server) inventoryCopy(ctx context.Context, req *mcp.CallToolRequest, args copyArgs) (*mcp.CallToolResult, any, error) {
	if len(req.Params.InputResponses) > 0 {
		return s.finishCopyElicitation(ctx, req)
	}
	if args.ID == "" || args.ParentID == "" {
		return errorResult("invalid_args", "id and parent_id are required", nil)
	}
	api, policy, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassMove)
	if errRes != nil {
		return errRes, nil, nil
	}
	params := map[string]any{"id": args.ID, "parent_id": args.ParentID}
	if args.Policy != "" {
		params["policy"] = args.Policy
	}
	raw, err := api.Copy(ctx, params)
	if err != nil {
		return apiError(err)
	}
	return s.finishCopy(ctx, req, api, policy, args.ViewerPID, raw, args.SkipElicitation)
}

func (s *Server) finishCopyElicitation(ctx context.Context, req *mcp.CallToolRequest) (*mcp.CallToolResult, any, error) {
	var state copyState
	if err := json.Unmarshal([]byte(req.Params.RequestState), &state); err != nil {
		return errorResult("internal", "missing copy plan state; call inventory_confirm_copy with plan_id", nil)
	}
	elicit, _ := req.Params.InputResponses["no_copy_confirm"].(*mcp.ElicitResult)
	accepted := elicit != nil && elicit.Action == "accept" && confirmFromContent(elicit.Content)
	if !accepted {
		payload := state.Payload
		if payload == nil {
			payload = map[string]any{}
		}
		payload["confirmation"] = "declined"
		payload["plan_id"] = state.PlanID
		payload["resume"] = "Call inventory_confirm_copy with this plan_id if you later choose to move unique no-copy items."
		return jsonResult(payload)
	}
	_, api, errRes := s.resolve(state.ViewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	return apiResult(api.ConfirmCopy(ctx, map[string]any{
		"plan_id": state.PlanID,
		"confirm": true,
	}))
}

func (s *Server) inventoryConfirmCopy(ctx context.Context, _ *mcp.CallToolRequest, args confirmCopyArgs) (*mcp.CallToolResult, any, error) {
	if args.PlanID == "" {
		return errorResult("invalid_args", "plan_id is required", nil)
	}
	confirm := true
	if args.Confirm != nil {
		confirm = *args.Confirm
	}
	api, policy, errRes := s.openClass(ctx, args.ViewerPID, viewerapi.ClassNoCopy)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present {
		return apiResult(api.ConfirmCopy(ctx, map[string]any{
			"plan_id": args.PlanID,
			"confirm": confirm,
		}))
	}
	if !confirm {
		return jsonResult(map[string]any{
			"confirmation": "declined",
			"plan_id":      args.PlanID,
		})
	}
	return apiResult(api.ConfirmCopy(ctx, map[string]any{
		"plan_id": args.PlanID,
	}))
}

func (s *Server) find() ([]discover.Instance, error) {
	if s.Find == nil {
		return discover.Find()
	}
	return s.Find()
}

func (s *Server) resolve(pidHint int) (discover.Instance, API, *mcp.CallToolResult) {
	insts, err := s.find()
	if err != nil {
		res, _, _ := errorResult("discovery_failed", err.Error(), nil)
		return discover.Instance{}, nil, res
	}
	if len(insts) == 0 {
		res, _, _ := errorResult("viewer_not_found", "no live Folderstorm MCP discovery files found; turn the bridge on under Preferences, Privacy, General, Local assistant. --mcp-api still forces it on for one session and does not save that choice", map[string]any{
			"hint": "The sidecar searches the Folderstorm user_settings directory. If settings were moved, set FIRESTORM_MCP_DISCOVERY to that directory or to an fs-mcp-<pid>.json file",
		})
		return discover.Instance{}, nil, res
	}

	wantPID := pidHint
	if wantPID == 0 {
		s.mu.Lock()
		wantPID = s.selectedPID
		s.mu.Unlock()
	}

	if wantPID != 0 {
		for _, inst := range insts {
			if inst.PID == wantPID {
				return s.clientFor(inst)
			}
		}
		if pidHint != 0 {
			res, _, _ := errorResult("viewer_not_found", fmt.Sprintf("viewer pid %d is not live", pidHint), map[string]any{
				"viewers": redactedInstances(insts),
			})
			return discover.Instance{}, nil, res
		}
		s.mu.Lock()
		s.selectedPID = 0
		s.mu.Unlock()
	}

	if len(insts) == 1 {
		s.mu.Lock()
		s.selectedPID = insts[0].PID
		s.mu.Unlock()
		return s.clientFor(insts[0])
	}
	res, _, _ := errorResult("multiple_viewers", "multiple Firestorm viewers are running; call viewer_list then viewer_select", map[string]any{
		"viewers": redactedInstances(insts),
	})
	return discover.Instance{}, nil, res
}

func (s *Server) clientFor(inst discover.Instance) (discover.Instance, API, *mcp.CallToolResult) {
	factory := s.NewClient
	if factory == nil {
		factory = func(i discover.Instance) API { return viewerapi.New(i) }
	}
	return inst, factory(inst), nil
}

func apiResult(raw json.RawMessage, err error) (*mcp.CallToolResult, any, error) {
	if err != nil {
		return apiError(err)
	}
	var payload any
	if unmarshalErr := json.Unmarshal(raw, &payload); unmarshalErr != nil {
		return jsonResult(map[string]any{"raw": string(raw)})
	}
	return jsonResult(payload)
}

func apiError(err error) (*mcp.CallToolResult, any, error) {
	if ve, ok := err.(*viewerapi.Error); ok {
		details := map[string]any{}
		if ve.StatusCode != 0 {
			details["status_code"] = ve.StatusCode
		}
		if ve.Op != "" {
			details["op"] = ve.Op
		}
		if len(ve.Body) > 0 {
			var body any
			if json.Unmarshal(ve.Body, &body) == nil {
				details["body"] = body
			}
		}
		return errorResult(ve.Code, ve.Message, details)
	}
	return errorResult("viewer_error", err.Error(), nil)
}

func redactedInstances(insts []discover.Instance) []discover.Instance {
	out := make([]discover.Instance, 0, len(insts))
	for _, inst := range insts {
		out = append(out, inst.Redacted())
	}
	return out
}

func firstNonEmpty(vals ...string) string {
	for _, v := range vals {
		if strings.TrimSpace(v) != "" {
			return v
		}
	}
	return ""
}

func supportsElicitation(req *mcp.CallToolRequest) bool {
	if req == nil {
		return false
	}
	caps := req.ClientCapabilities()
	return caps != nil && caps.Elicitation != nil
}

type copyState struct {
	PlanID    string         `json:"plan_id"`
	ViewerPID int            `json:"viewer_pid,omitempty"`
	Payload   map[string]any `json:"payload,omitempty"`
}

type copyResult struct {
	NeedsConfirmation bool
	PlanID            string
	Payload           map[string]any
	ProposedMoves     []any
}

func parseCopyResult(raw json.RawMessage) copyResult {
	var payload map[string]any
	if err := json.Unmarshal(raw, &payload); err != nil {
		return copyResult{Payload: map[string]any{"raw": string(raw)}}
	}
	planID := stringField(payload, "plan_id", "planId")
	status := strings.ToLower(stringField(payload, "status", "result"))
	needs := planID != "" && (status == "confirmation_required" ||
		status == "needs_confirmation" ||
		boolField(payload, "needs_confirmation", "confirmation_required"))
	if !needs && planID != "" && payload["proposed_moves"] != nil {
		needs = true
	}
	return copyResult{
		NeedsConfirmation: needs,
		PlanID:            planID,
		Payload:           payload,
		ProposedMoves:     sliceField(payload, "proposed_moves", "proposedMoves"),
	}
}

func confirmationRequiredResult(parsed copyResult) (*mcp.CallToolResult, any, error) {
	payload := parsed.Payload
	if payload == nil {
		payload = map[string]any{}
	}
	payload["status"] = "confirmation_required"
	payload["plan_id"] = parsed.PlanID
	payload["resume"] = "Approval completes the destination by moving unique no-copy items out of the source. Call inventory_confirm_copy with this plan_id."
	raw, _ := json.Marshal(payload)
	return &mcp.CallToolResult{
		Content:           []mcp.Content{&mcp.TextContent{Text: string(raw)}},
		StructuredContent: payload,
	}, payload, nil
}

func noCopyMessage(parsed copyResult) string {
	var b strings.Builder
	b.WriteString("Firestorm copied every copyable item. Completing this copy will MOVE unique no-copy items from the source into the destination. Those items will no longer exist in the source.\n")
	if parsed.PlanID != "" {
		fmt.Fprintf(&b, "plan_id: %s\n", parsed.PlanID)
	}
	if len(parsed.ProposedMoves) == 0 {
		b.WriteString("The viewer did not list individual moves; confirm only if you intend to relocate unique no-copy leaves.")
		return b.String()
	}
	b.WriteString("Proposed no-copy moves:\n")
	for i, move := range parsed.ProposedMoves {
		raw, _ := json.Marshal(move)
		fmt.Fprintf(&b, "%d. %s\n", i+1, raw)
	}
	return b.String()
}

func noCopySchema() *jsonschema.Schema {
	return &jsonschema.Schema{
		Type: "object",
		Properties: map[string]*jsonschema.Schema{
			"confirm": {
				Type:        "boolean",
				Description: "true to move unique no-copy items out of the source into the destination",
			},
		},
		Required: []string{"confirm"},
	}
}

func confirmFromContent(content map[string]any) bool {
	if content == nil {
		return true
	}
	v, ok := content["confirm"]
	if !ok {
		return true
	}
	switch t := v.(type) {
	case bool:
		return t
	case string:
		return strings.EqualFold(t, "true") || t == "1" || strings.EqualFold(t, "yes")
	default:
		return false
	}
}

func stringField(m map[string]any, keys ...string) string {
	for _, k := range keys {
		if v, ok := m[k]; ok {
			if s, ok := v.(string); ok {
				return s
			}
		}
	}
	return ""
}

func boolField(m map[string]any, keys ...string) bool {
	for _, k := range keys {
		if v, ok := m[k]; ok {
			if b, ok := v.(bool); ok && b {
				return true
			}
		}
	}
	return false
}

func sliceField(m map[string]any, keys ...string) []any {
	for _, k := range keys {
		if v, ok := m[k]; ok {
			if s, ok := v.([]any); ok {
				return s
			}
		}
	}
	return nil
}
