package mcptools

import (
	"context"
	"encoding/json"
	"fmt"
	"fs-mcp/internal/viewerapi"
	"strings"
	"testing"
)

func workspaceTestStatus(read, workspace string, version int) json.RawMessage {
	return json.RawMessage(fmt.Sprintf(`{"policy_generation":1,"permissions":{"read":%q,"workspace":%q},"capabilities":{"workspace_tools":%d}}`, read, workspace, version))
}
func TestWorkspaceToolsForwardToDedicatedAPI(t *testing.T) {
	tests := []struct {
		op     string
		read   bool
		params map[string]any
	}{
		{"list", true, map[string]any{}}, {"status", true, map[string]any{}},
		{"preview", true, map[string]any{"kind": "workspace", "id": "Driving"}},
		{"apply", false, map[string]any{"kind": "layout", "id": "Two Monitors"}},
		{"previous", false, map[string]any{}},
	}
	for _, tt := range tests {
		t.Run(tt.op, func(t *testing.T) {
			api := &fakeAPI{status: workspaceTestStatus("allow", "ask", 1)}
			result, _, err := reviewServer(api).workspaceCall(context.Background(), 11, tt.op, tt.params, tt.read)
			if err != nil || result.IsError || !contains(api.opsCopy(), "LLWorkspace."+tt.op) {
				t.Fatalf("did not forward %s: %v %v", tt.op, err, result)
			}
		})
	}
}
func TestWorkspaceUnsupportedNeverFallsBack(t *testing.T) {
	for _, status := range []json.RawMessage{json.RawMessage(`{"logged_in":true}`), workspaceTestStatus("allow", "allow", 0), workspaceTestStatus("allow", "allow", 2)} {
		api := &fakeAPI{status: status}
		result, _, _ := reviewServer(api).workspaceCall(context.Background(), 11, "apply", map[string]any{"kind": "workspace", "id": "Driving"}, false)
		if !result.IsError || contains(api.opsCopy(), "LLWorkspace.apply") {
			t.Fatal("unsupported viewer received a switch")
		}
	}
}
func TestWorkspaceReadAndSwitchPermissionsAreIndependent(t *testing.T) {
	tests := []struct {
		read, workspace, op string
		requireRead, denied bool
	}{
		{"deny", "allow", "apply", false, false}, {"allow", "deny", "apply", false, true},
		{"deny", "allow", "list", true, true}, {"allow", "deny", "status", true, false},
	}
	for _, tt := range tests {
		api := &fakeAPI{status: workspaceTestStatus(tt.read, tt.workspace, 1)}
		result, _, _ := reviewServer(api).workspaceCall(context.Background(), 11, tt.op, map[string]any{}, tt.requireRead)
		if result.IsError != tt.denied {
			t.Fatalf("permission mismatch: %+v", tt)
		}
	}
}
func TestWorkspaceTargetsAreExact(t *testing.T) {
	for _, args := range []workspaceTargetArgs{{Kind: "other", ID: "Driving"}, {Kind: "workspace", ID: ""}, {Kind: "layout", ID: " Driving"}, {Kind: "workspace", ID: strings.Repeat("x", 129)}} {
		if validWorkspaceTarget(args) {
			t.Fatalf("accepted invalid target %+v", args)
		}
	}
	if !validWorkspaceTarget(workspaceTargetArgs{Kind: "workspace", ID: "Driving"}) {
		t.Fatal("valid target rejected")
	}
	if classForOp(viewerapi.WorkspaceAPI, "apply") != viewerapi.ClassWorkspace || classForOp(viewerapi.WorkspaceAPI, "preview") != viewerapi.ClassRead {
		t.Fatal("wrong classification")
	}
}

func TestWorkspaceRegisteredTools(t *testing.T) {
	tests := []struct {
		name, op string
		args     map[string]any
	}{
		{"workspace_list", "list", map[string]any{}},
		{"workspace_status", "status", map[string]any{}},
		{"workspace_preview", "preview", map[string]any{"kind": "workspace", "id": "Driving"}},
		{"workspace_apply", "apply", map[string]any{"kind": "workspace", "id": "Driving"}},
		{"workspace_previous", "previous", map[string]any{}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			api := &fakeAPI{status: workspaceTestStatus("allow", "ask", 1)}
			cs := connect(t, reviewServer(api), nil)
			result := call(t, cs, tt.name, tt.args)
			if result.IsError || !contains(api.opsCopy(), "LLWorkspace."+tt.op) {
				t.Fatalf("registered tool failed: %s", textOf(result))
			}
		})
	}
}
