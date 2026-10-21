package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/controller"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// ControllerTools holds separate host discovery/authentication. It deliberately
// has no Server, viewer client, selected PID or worker permission dependency.
type ControllerAPI interface {
	Call(context.Context, controller.Request) (json.RawMessage, error)
}
type ControllerTools struct {
	Find      func() ([]controller.Instance, error)
	NewClient func(controller.Instance) ControllerAPI
}

func NewControllerTools() *ControllerTools {
	return &ControllerTools{Find: controller.Find, NewClient: func(i controller.Instance) ControllerAPI { return controller.New(i) }}
}

// RegisterController is the single integration hook for NewMCPServer.
func RegisterController(server *mcp.Server) { RegisterControllerWith(server, NewControllerTools()) }
func RegisterControllerWith(server *mcp.Server, state *ControllerTools) {
	read := &mcp.ToolAnnotations{ReadOnlyHint: true, OpenWorldHint: boolPtr(false)}
	write := &mcp.ToolAnnotations{ReadOnlyHint: false, DestructiveHint: boolPtr(false), OpenWorldHint: boolPtr(true)}
	lifecycle := &mcp.ToolAnnotations{ReadOnlyHint: false, DestructiveHint: boolPtr(true), OpenWorldHint: boolPtr(true)}
	mcp.AddTool(server, &mcp.Tool{Name: "controller_list", Description: "Discover enabled current-user session controllers. Returns opaque controller_instance and a fresh request_id for one explicit mutation; never returns credentials or private paths. Controller authority is separate from viewer permissions.", Annotations: read}, state.list)
	mcp.AddTool(server, &mcp.Tool{Name: "sessions_list", Description: "Read explicitly opted-in host profiles, including stopped profiles. Requires exact controller_instance and independent host metadata permission. No active-character fallback.", Annotations: read}, state.sessions)
	mcp.AddTool(server, &mcp.Tool{Name: "session_status", Description: "Read one exact controller profile revision. Running profiles identify their exact worker and opaque viewer_session when ready. Does not activate, log in or enable MCP.", Annotations: read}, state.status)
	mcp.AddTool(server, &mcp.Tool{Name: "controller_operation_status", Description: "Inspect an original controller request_id. Submitted does not prove logout, restart or process exit. Unknown/expired outcomes require inspection; never replay using a fresh request ID.", Annotations: read}, state.receipt)
	for _, entry := range []struct {
		name, description string
		op                controller.Op
		annotations       *mcp.ToolAnnotations
	}{
		{"session_activate", "Explicitly hand off the active world to this exact ready worker using native input/microphone serialization. Requires independent host activation permission and opted-in affected profiles.", controller.Activate, write},
		{"session_background_mode", "Set an exact inactive worker to warm or economy through the native host. Never promotes a worker or changes voice ownership.", controller.Background, write},
		{"session_open_login", "Open the configured stopped profile's matching native login window. Does not supply credentials, complete login or enable worker MCP; native first-open choices remain authoritative.", controller.OpenLogin, write},
		{"session_restart", "Request native close, wait for process/profile release, then open login. Native cancellation wins. No force kill, credential entry or uncertain automatic retry.", controller.Restart, lifecycle},
		{"session_logout", "Submit the exact worker's ordinary native logout request; dialogs and cancellation remain authoritative. A receipt is not proof of process exit.", controller.Logout, lifecycle},
		{"session_detach", "Stop managing the exact worker, leaving its ordinary viewer logged in. Cancels an armed restart; never kills the process.", controller.Detach, lifecycle},
		{"monitor_open", "Open a bounded read-only preview of the exact inactive worker. Reuses native render budgets and configured size/rate; does not activate a character or grant Chat access.", controller.MonitorOpen, write},
		{"monitor_close", "Close an exact monitor_id still bound to this exact profile/worker. Does not log out or activate a character.", controller.MonitorClose, write},
	} {
		op := entry.op
		mcp.AddTool(server, &mcp.Tool{Name: entry.name, Description: entry.description, Annotations: entry.annotations}, func(ctx context.Context, _ *mcp.CallToolRequest, args controllerTargetArgs) (*mcp.CallToolResult, any, error) {
			return state.target(ctx, args, op)
		})
	}
	mcp.AddTool(server, &mcp.Tool{Name: "monitor_list", Description: "Read current opted-in monitor bindings and cycle state. No images or private Chat are returned; requires independent controller metadata permission.", Annotations: read}, state.monitors)
	for _, entry := range []struct {
		name, description string
		op                controller.Op
	}{
		{"monitors_open_all", "Open previews of eligible opted-in inactive workers within the four fixed-monitor limit; refuse capacity conflicts before opening any window.", controller.MonitorOpenAll},
		{"monitors_arrange", "Arrange existing opted-in preview windows in a grid or row on the host screen. Checks every destination DPI minimum before moving any window.", controller.MonitorArrange},
		{"monitor_cycle_configure", "Configure the separate cycling preview with frozen opted-in sources, 2–3600 second interval and explicit play/pause. It never changes the active character; native pause-before-Chat rules remain.", controller.CycleConfigure},
		{"monitor_cycle_pause", "Pause the cycling preview without activating a worker or modifying Chat drafts.", controller.CyclePause},
		{"monitor_cycle_next", "Pause and advance the cycling preview to the next eligible opted-in source. Clears stale pixels and source-bound Chat selection before rebinding.", controller.CycleNext},
	} {
		op := entry.op
		mcp.AddTool(server, &mcp.Tool{Name: entry.name, Description: entry.description, Annotations: write}, func(ctx context.Context, _ *mcp.CallToolRequest, args controllerGlobalWriteArgs) (*mcp.CallToolResult, any, error) {
			return state.global(ctx, args, op)
		})
	}
}

type controllerArgs struct {
	ControllerInstance string `json:"controller_instance" jsonschema:"exact opaque controller reference from controller_list; required, never selected automatically"`
}
type controllerTargetArgs struct {
	ControllerInstance string `json:"controller_instance" jsonschema:"exact controller_instance from controller_list"`
	RequestID          string `json:"request_id" jsonschema:"fresh timestamped request ID from controller_list; reuse exactly for receipt inspection, not a fresh mutation"`
	ProfileID          string `json:"profile_id" jsonschema:"stable profile ID from sessions_list"`
	ProfileRevision    string `json:"profile_revision" jsonschema:"exact decimal revision from sessions_list"`
	WorkerID           string `json:"worker_id,omitempty" jsonschema:"exact running worker ID; omit for a stopped profile"`
	WorkerGeneration   string `json:"worker_generation,omitempty" jsonschema:"exact decimal running worker generation; omit for stopped profiles"`
	ViewerSession      string `json:"viewer_session,omitempty" jsonschema:"exact opaque running login reference, required for running mutations"`
	BackgroundMode     string `json:"background_mode,omitempty" jsonschema:"warm or economy for session_background_mode"`
	MonitorID          *int   `json:"monitor_id,omitempty" jsonschema:"monitor ID 0..4 from monitor_list, required for monitor_close"`
}
type controllerStatusArgs struct {
	ControllerInstance string `json:"controller_instance"`
	ProfileID          string `json:"profile_id"`
	ProfileRevision    string `json:"profile_revision"`
}
type controllerReceiptArgs struct {
	ControllerInstance string `json:"controller_instance"`
	RequestID          string `json:"request_id"`
}
type controllerGlobalWriteArgs struct {
	ControllerInstance string `json:"controller_instance"`
	RequestID          string `json:"request_id"`
	Layout             string `json:"layout,omitempty" jsonschema:"grid or row for monitors_arrange; default grid"`
	Seconds            *int   `json:"seconds,omitempty" jsonschema:"cycle interval, 2..3600; default 10"`
	Playing            bool   `json:"playing,omitempty" jsonschema:"whether to play after configuring cycling; default false"`
}

func (s *ControllerTools) call(ctx context.Context, q controller.Request) (*mcp.CallToolResult, any, error) {
	if err := controller.Validate(q); err != nil {
		return errorResult("invalid_args", err.Error(), nil)
	}
	instances, err := s.Find()
	if err != nil {
		return errorResult("controller_unavailable", "Controller discovery unavailable; enable host controller access explicitly in native Preferences.", nil)
	}
	instance, err := controller.Resolve(instances, q.ControllerInstance)
	if err != nil {
		return errorResult("controller_changed", err.Error(), nil)
	}
	raw, err := s.NewClient(instance).Call(ctx, q)
	if err != nil {
		return errorResult("controller_outcome_unknown", err.Error(), nil)
	}
	var envelope map[string]any
	if json.Unmarshal(raw, &envelope) != nil {
		return errorResult("invalid_response", "Invalid controller response; inspect the original request ID.", nil)
	}
	if code, ok := envelope["error_code"].(string); ok && code != "" && envelope["request_id"] == nil {
		return errorResult(code, code, nil)
	}
	return apiResult(raw, nil)
}
func (s *ControllerTools) list(context.Context, *mcp.CallToolRequest, struct{}) (*mcp.CallToolResult, any, error) {
	instances, err := s.Find()
	if err != nil {
		return errorResult("controller_unavailable", "No enabled current-user controller was found.", nil)
	}
	rows := make([]map[string]string, 0, len(instances))
	for _, instance := range instances {
		id, err := controller.NewRequestID()
		if err != nil {
			return errorResult("request_id_failed", "Could not allocate request identity.", nil)
		}
		rows = append(rows, map[string]string{"controller_instance": instance.ControllerInstance, "request_id": id})
	}
	raw, _ := json.Marshal(map[string]any{"controllers": rows})
	return apiResult(raw, nil)
}
func (s *ControllerTools) sessions(ctx context.Context, _ *mcp.CallToolRequest, a controllerArgs) (*mcp.CallToolResult, any, error) {
	return s.call(ctx, controller.Request{ControllerInstance: a.ControllerInstance, Op: controller.List, Seconds: 10})
}
func (s *ControllerTools) monitors(ctx context.Context, _ *mcp.CallToolRequest, a controllerArgs) (*mcp.CallToolResult, any, error) {
	return s.call(ctx, controller.Request{ControllerInstance: a.ControllerInstance, Op: controller.MonitorList, Seconds: 10})
}
func (s *ControllerTools) status(ctx context.Context, _ *mcp.CallToolRequest, a controllerStatusArgs) (*mcp.CallToolResult, any, error) {
	revision, err := controller.ParseRevision(a.ProfileRevision)
	if err != nil {
		return errorResult("invalid_args", err.Error(), nil)
	}
	return s.call(ctx, controller.Request{ControllerInstance: a.ControllerInstance, Op: controller.Status, ProfileID: a.ProfileID, ProfileRevision: revision, Seconds: 10})
}
func (s *ControllerTools) receipt(ctx context.Context, _ *mcp.CallToolRequest, a controllerReceiptArgs) (*mcp.CallToolResult, any, error) {
	return s.call(ctx, controller.Request{ControllerInstance: a.ControllerInstance, RequestID: a.RequestID, Op: controller.Receipt, Seconds: 10})
}
func (s *ControllerTools) target(ctx context.Context, a controllerTargetArgs, op controller.Op) (*mcp.CallToolResult, any, error) {
	revision, err := controller.ParseRevision(a.ProfileRevision)
	if err != nil {
		return errorResult("invalid_args", err.Error(), nil)
	}
	var generation uint64
	if a.WorkerGeneration != "" {
		generation, err = controller.ParseRevision(a.WorkerGeneration)
		if err != nil {
			return errorResult("invalid_args", err.Error(), nil)
		}
	}
	monitorID := 0
	if a.MonitorID != nil {
		monitorID = *a.MonitorID
	} else if op == controller.MonitorClose {
		return errorResult("invalid_args", "monitor_id is required for monitor_close", nil)
	}
	if monitorID < 0 || monitorID >= 5 {
		return errorResult("invalid_args", "monitor_id must be 0..4", nil)
	}
	if op == controller.Background && a.BackgroundMode != "warm" && a.BackgroundMode != "economy" {
		return errorResult("invalid_args", "background_mode must be warm or economy", nil)
	}
	q := controller.Request{ControllerInstance: a.ControllerInstance, RequestID: a.RequestID, Op: op, ProfileID: a.ProfileID, ProfileRevision: revision, WorkerID: a.WorkerID, WorkerGeneration: generation, ViewerSession: a.ViewerSession, Economy: a.BackgroundMode == "economy", MonitorID: uint32(monitorID), Seconds: 10}
	return s.call(ctx, q)
}
func (s *ControllerTools) global(ctx context.Context, a controllerGlobalWriteArgs, op controller.Op) (*mcp.CallToolResult, any, error) {
	seconds := 10
	if a.Seconds != nil {
		seconds = *a.Seconds
	}
	if seconds < 2 || seconds > 3600 || a.Layout != "" && a.Layout != "grid" && a.Layout != "row" {
		return errorResult("invalid_args", "layout must be grid or row; seconds must be 2..3600", nil)
	}
	return s.call(ctx, controller.Request{ControllerInstance: a.ControllerInstance, RequestID: a.RequestID, Op: op, Seconds: uint32(seconds), Row: a.Layout == "row", Playing: a.Playing})
}
