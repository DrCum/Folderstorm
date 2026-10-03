package mcptools

import (
	"context"
	"encoding/json"

	"fs-mcp/internal/viewerapi"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func classForOp(apiName, op string) string {
	switch apiName {
	case viewerapi.AppearanceAPI:
		switch op {
		case "getOutfitsList", "getOutfitItems", "worn":
			return viewerapi.ClassRead
		case "wearOutfit", "wearItems", "detachItems":
			return viewerapi.ClassWear
		}
	case viewerapi.CameraAPI:
		switch op {
		case "get":
			return viewerapi.ClassRead
		case "set", "setPose", "reset", "snapshot":
			return viewerapi.ClassCamera
		}
	default:
		switch op {
		case "status", "get", "list", "search", "systemFolder", "types", "getMany",
			"resolvePath", "protectedFolders", "readNotecard", "readScript", "landmark", "changes",
			"getItemsInfo", "getFolderTypeNames", "getAssetTypeNames", "getBasicFolderID",
			"getDirectDescendants", "collectDescendantsIf":
			return viewerapi.ClassRead
		case "createFolder", "createItem", "link":
			return viewerapi.ClassCreate
		case "rename", "batchRename", "setDescription", "setThumbnail", "setFavorite":
			return viewerapi.ClassEdit
		case "move", "batchMove", "restore", "copy", "batchCopy":
			return viewerapi.ClassMove
		case "trash":
			return viewerapi.ClassTrash
		case "confirmCopy":
			return viewerapi.ClassNoCopy
		case "replaceLinks":
			return viewerapi.ClassLinks
		}
	}
	return ""
}

func isPermanentOp(op string) bool {
	return op == "purge" || op == "emptyTrash"
}

func permanentDeleteResult() (*mcp.CallToolResult, any, error) {
	return errorResult("not_permitted", "Permanent delete is not available. The viewer bridge denies purge and emptyTrash, and no setting can enable them.", nil)
}

func (s *Server) rememberPolicy(pid int, policy viewerapi.StatusPolicy) {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.policies == nil {
		s.policies = map[int]viewerapi.StatusPolicy{}
	}
	s.policies[pid] = policy
}

func (s *Server) loadPolicy(ctx context.Context, pid int, api API) (viewerapi.StatusPolicy, error) {
	raw, err := api.Status(ctx)
	if err != nil {
		if ve, ok := err.(*viewerapi.Error); ok {
			if policy, parsed := viewerapi.ParseStatusPolicy(ve.Body); parsed && policy.Present {
				s.rememberIfGenerationChanged(pid, policy)
				return s.cachedPolicy(pid, policy), nil
			}
		}
		return viewerapi.StatusPolicy{}, err
	}
	policy, parsed := viewerapi.ParseStatusPolicy(raw)
	if !parsed {
		return viewerapi.StatusPolicy{}, errStatus(raw)
	}
	s.rememberIfGenerationChanged(pid, policy)
	return s.cachedPolicy(pid, policy), nil
}

func errStatus(raw []byte) error {
	return &viewerapi.Error{Code: "viewer_error", Message: "inventory status was not a JSON object", Body: raw}
}

func (s *Server) rememberIfGenerationChanged(pid int, policy viewerapi.StatusPolicy) {
	s.mu.Lock()
	prev, had := s.policies[pid]
	s.mu.Unlock()
	if had && prev.Present == policy.Present && prev.Generation == policy.Generation {
		return
	}
	s.rememberPolicy(pid, policy)
}

func (s *Server) cachedPolicy(pid int, fallback viewerapi.StatusPolicy) viewerapi.StatusPolicy {
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.policies == nil {
		return fallback
	}
	if cached, ok := s.policies[pid]; ok {
		return cached
	}
	return fallback
}

// openClass loads the viewer's policy and refuses Never before the tool body runs.
// A missing permissions object leaves Present false so the caller can keep the
// old elicitation path. Ask and Allow both return the client; the viewer dialog
// is the question for Ask.
func (s *Server) openClass(ctx context.Context, viewerPID int, class string) (API, viewerapi.StatusPolicy, *mcp.CallToolResult) {
	return s.openClasses(ctx, viewerPID, []string{class})
}

// Resolve and load policy once for compound actions. Ask still belongs to the
// viewer, which rechecks every required class immediately before dispatch.
func (s *Server) openClasses(ctx context.Context, viewerPID int, classes []string) (API, viewerapi.StatusPolicy, *mcp.CallToolResult) {
	inst, api, errRes := s.resolve(viewerPID)
	if errRes != nil {
		return nil, viewerapi.StatusPolicy{}, errRes
	}
	policy, err := s.loadPolicy(ctx, inst.PID, api)
	if err != nil {
		res, _, _ := apiError(err)
		return nil, viewerapi.StatusPolicy{}, res
	}
	if policy.Present {
		for _, class := range classes {
			if policy.Level(class) == "deny" {
				res, _, _ := errorResult("not_permitted", "This action is not allowed by the viewer permission settings", map[string]any{
					"class": class, "required_classes": classes,
				})
				return nil, policy, res
			}
		}
	}
	return api, policy, nil
}

// callOrConfirm runs a classed write. An older viewer keeps sidecar
// elicitation. A current viewer is called once; Ask is the viewer's dialog,
// and skip_elicitation does not bypass it.
func (s *Server) callOrConfirm(ctx context.Context, req *mcp.CallToolRequest, viewerPID int, class, apiName, op string, params map[string]any, message string, skip bool) (*mcp.CallToolResult, any, error) {
	api, policy, errRes := s.openClass(ctx, viewerPID, class)
	if errRes != nil {
		return errRes, nil, nil
	}
	if !policy.Present {
		return s.gateConfirm(ctx, req, viewerPID, apiName, op, params, message, skip)
	}
	return apiResult(api.CallNamed(ctx, apiName, op, params))
}

// finishCopy applies the no-copy half of a copy the viewer has already started.
// A current viewer asks in its own dialog: Ask and Allow call confirmCopy and
// do not elicit. Never does not call confirmCopy. An older viewer, with no
// permissions object, keeps the sidecar form and plan_id.
func (s *Server) finishCopy(ctx context.Context, req *mcp.CallToolRequest, api API, policy viewerapi.StatusPolicy, viewerPID int, raw json.RawMessage, skip bool) (*mcp.CallToolResult, any, error) {
	parsed := parseCopyResult(raw)
	ids := copyConfirmPlanIDs(parsed.Payload)
	if len(ids) == 0 {
		return jsonResult(parsed.Payload)
	}
	if !policy.Present {
		if parsed.NeedsConfirmation && parsed.PlanID != "" {
			if skip || !supportsElicitation(req) {
				return confirmationRequiredResult(parsed)
			}
			state, _ := json.Marshal(copyState{
				PlanID:    parsed.PlanID,
				ViewerPID: viewerPID,
				Payload:   parsed.Payload,
			})
			return &mcp.CallToolResult{
				InputRequests: mcp.InputRequestMap{
					"no_copy_confirm": &mcp.ElicitParams{
						Mode:            "form",
						Message:         noCopyMessage(parsed),
						RequestedSchema: noCopySchema(),
					},
				},
				RequestState: string(state),
			}, nil, nil
		}
		return jsonResult(parsed.Payload)
	}
	if policy.Level(viewerapi.ClassNoCopy) == "deny" {
		return errorResult("not_permitted", "This action is not allowed by the viewer permission settings", map[string]any{
			"class": viewerapi.ClassNoCopy,
		})
	}
	confirmed := make([]any, 0, len(ids))
	for _, id := range ids {
		rawConfirm, err := api.ConfirmCopy(ctx, map[string]any{"plan_id": id})
		if err != nil {
			return apiError(err)
		}
		var body any
		if unmarshalErr := json.Unmarshal(rawConfirm, &body); unmarshalErr != nil {
			body = map[string]any{"raw": string(rawConfirm)}
		}
		confirmed = append(confirmed, body)
	}
	if len(confirmed) == 1 {
		return jsonResult(confirmed[0])
	}
	return jsonResult(map[string]any{"confirmed": confirmed})
}

func copyConfirmPlanIDs(payload map[string]any) []string {
	if payload == nil {
		return nil
	}
	raw, err := json.Marshal(payload)
	if err != nil {
		return nil
	}
	var ids []string
	seen := map[string]bool{}
	add := func(id string) {
		if id == "" || seen[id] {
			return
		}
		seen[id] = true
		ids = append(ids, id)
	}
	top := parseCopyResult(raw)
	if top.NeedsConfirmation {
		add(top.PlanID)
	}
	for _, item := range sliceField(payload, "results") {
		row, ok := item.(map[string]any)
		if !ok {
			continue
		}
		status := stringField(row, "status", "result")
		needs := boolField(row, "needs_confirmation", "confirmation_required") ||
			status == "confirmation_required" || status == "needs_confirmation"
		if needs {
			add(stringField(row, "plan_id", "planId"))
		}
	}
	return ids
}
