package mcptools

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"time"

	"github.com/google/jsonschema-go/jsonschema"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type storedPlan struct {
	API       string
	Op        string
	Params    map[string]any
	ViewerPID int
	Message   string
	Expires   time.Time
}

type confirmArgs struct {
	PlanID    string `json:"plan_id" jsonschema:"plan id returned when confirmation could not be prompted"`
	Confirm   *bool  `json:"confirm,omitempty" jsonschema:"approve the plan; defaults to true"`
	ViewerPID int    `json:"viewer_pid,omitempty" jsonschema:"optional Firestorm process id"`
}

type confirmState struct {
	PlanID string `json:"plan_id"`
}

func (s *Server) confirmAction(ctx context.Context, _ *mcp.CallToolRequest, args confirmArgs) (*mcp.CallToolResult, any, error) {
	if args.PlanID == "" {
		return errorResult("invalid_args", "plan_id is required", nil)
	}
	approve := true
	if args.Confirm != nil {
		approve = *args.Confirm
	}
	s.mu.Lock()
	plan, ok := s.plans[args.PlanID]
	if ok {
		delete(s.plans, args.PlanID)
	}
	s.mu.Unlock()
	if !ok || time.Now().After(plan.Expires) {
		return errorResult("not_found", "confirmation plan was not found or has expired", map[string]any{"plan_id": args.PlanID})
	}
	if !approve {
		return jsonResult(map[string]any{
			"confirmation": "declined",
			"plan_id":      args.PlanID,
			"op":           plan.Op,
		})
	}
	if args.ViewerPID != 0 {
		plan.ViewerPID = args.ViewerPID
	}
	return s.runConfirmed(ctx, plan)
}

func (s *Server) runConfirmed(ctx context.Context, plan storedPlan) (*mcp.CallToolResult, any, error) {
	_, api, errRes := s.resolve(plan.ViewerPID)
	if errRes != nil {
		return errRes, nil, nil
	}
	params := map[string]any{}
	for key, value := range plan.Params {
		params[key] = value
	}
	params["confirm"] = true
	return apiResult(api.CallNamed(ctx, plan.API, plan.Op, params))
}

func (s *Server) gateConfirm(ctx context.Context, req *mcp.CallToolRequest, viewerPID int, apiName, op string, params map[string]any, message string, skip bool) (*mcp.CallToolResult, any, error) {
	if len(req.Params.InputResponses) > 0 {
		return s.finishConfirmElicitation(ctx, req)
	}
	plan := storedPlan{
		API:       apiName,
		Op:        op,
		Params:    params,
		ViewerPID: viewerPID,
		Message:   message,
		Expires:   time.Now().Add(10 * time.Minute),
	}
	planID := newPlanID()
	s.mu.Lock()
	if s.plans == nil {
		s.plans = map[string]storedPlan{}
	}
	s.prunePlansLocked()
	s.plans[planID] = plan
	s.mu.Unlock()

	if skip || !supportsElicitation(req) {
		return jsonResult(map[string]any{
			"status":             "confirmation_required",
			"needs_confirmation": true,
			"plan_id":            planID,
			"message":            message,
			"resume":             "Call confirm_action with this plan_id.",
		})
	}
	state, _ := json.Marshal(confirmState{PlanID: planID})
	return &mcp.CallToolResult{
		InputRequests: mcp.InputRequestMap{
			"confirm_action": &mcp.ElicitParams{
				Mode:            "form",
				Message:         message,
				RequestedSchema: confirmSchema(),
			},
		},
		RequestState: string(state),
	}, nil, nil
}

func (s *Server) finishConfirmElicitation(ctx context.Context, req *mcp.CallToolRequest) (*mcp.CallToolResult, any, error) {
	var state confirmState
	if err := json.Unmarshal([]byte(req.Params.RequestState), &state); err != nil || state.PlanID == "" {
		return errorResult("internal", "missing confirmation state; call confirm_action with plan_id", nil)
	}
	elicit, _ := req.Params.InputResponses["confirm_action"].(*mcp.ElicitResult)
	accepted := elicit != nil && elicit.Action == "accept" && confirmFromContent(elicit.Content)
	s.mu.Lock()
	plan, ok := s.plans[state.PlanID]
	if ok && accepted {
		delete(s.plans, state.PlanID)
	}
	s.mu.Unlock()
	if !ok {
		return errorResult("not_found", "confirmation plan was not found or has expired", map[string]any{"plan_id": state.PlanID})
	}
	if !accepted {
		return jsonResult(map[string]any{
			"confirmation": "declined",
			"plan_id":      state.PlanID,
			"op":           plan.Op,
			"resume":       "Call confirm_action with this plan_id if you later approve it.",
		})
	}
	return s.runConfirmed(ctx, plan)
}

func (s *Server) prunePlansLocked() {
	now := time.Now()
	for id, plan := range s.plans {
		if now.After(plan.Expires) {
			delete(s.plans, id)
		}
	}
}

func newPlanID() string {
	var buf [16]byte
	if _, err := rand.Read(buf[:]); err != nil {
		return time.Now().UTC().Format("20060102150405.000000000")
	}
	return hex.EncodeToString(buf[:])
}

func confirmSchema() *jsonschema.Schema {
	return &jsonschema.Schema{
		Type: "object",
		Properties: map[string]*jsonschema.Schema{
			"confirm": {Type: "boolean", Description: "Approve this action"},
		},
		Required: []string{"confirm"},
	}
}
