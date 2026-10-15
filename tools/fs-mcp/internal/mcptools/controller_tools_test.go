package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/controller"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"strings"
	"testing"
)

type controllerFake struct{ calls []controller.Request }

func (f *controllerFake) Call(_ context.Context, q controller.Request) (json.RawMessage, error) {
	f.calls = append(f.calls, q)
	return json.RawMessage(`{"state":"pending_approval"}`), nil
}
func TestControllerTargetNeverUsesViewerSelection(t *testing.T) {
	fake := &controllerFake{}
	instance := controller.Instance{ControllerInstance: strings.Repeat("a", 32)}
	state := &ControllerTools{Find: func() ([]controller.Instance, error) { return []controller.Instance{instance}, nil }, NewClient: func(controller.Instance) ControllerAPI { return fake }}
	id, _ := controller.NewRequestID()
	args := controllerTargetArgs{ControllerInstance: instance.ControllerInstance, RequestID: id, ProfileID: strings.Repeat("b", 32), ProfileRevision: "1"}
	result, _, _ := state.target(context.Background(), args, controller.Activate)
	if !result.IsError || len(fake.calls) != 0 {
		t.Fatal("missing running source reached controller")
	}
	args.WorkerID = strings.Repeat("c", 32)
	args.WorkerGeneration = "9"
	args.ViewerSession = "opaque-login"
	result, _, _ = state.target(context.Background(), args, controller.Activate)
	if result.IsError || len(fake.calls) != 1 || fake.calls[0].ViewerSession != args.ViewerSession || fake.calls[0].Op != controller.Activate {
		t.Fatal("exact binding was not preserved")
	}
	args.ControllerInstance = ""
	result, _, _ = state.target(context.Background(), args, controller.Restart)
	if !result.IsError || len(fake.calls) != 1 {
		t.Fatal("implicit controller")
	}
}

func TestControllerToolsHaveIndependentRegistration(t *testing.T) {
	server := mcp.NewServer(&mcp.Implementation{Name: "controller-test", Version: "1"}, nil)
	RegisterControllerWith(server, NewControllerTools())
}
