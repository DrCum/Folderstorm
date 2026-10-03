package mcptools

import (
	"context"
	"encoding/json"
	"strings"
	"testing"

	"fs-mcp/internal/discover"
	"fs-mcp/internal/viewerapi"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type bulkReviewAPI struct {
	fakeAPI
	response  json.RawMessage
	statusErr error
}

func (api *bulkReviewAPI) Status(ctx context.Context) (json.RawMessage, error) {
	if api.statusErr != nil {
		api.record("status")
		return nil, api.statusErr
	}
	return api.fakeAPI.Status(ctx)
}

func (api *bulkReviewAPI) CallNamed(ctx context.Context, apiName, op string, params map[string]any) (json.RawMessage, error) {
	_, _ = api.fakeAPI.CallNamed(ctx, apiName, op, params)
	if api.response != nil {
		return api.response, nil
	}
	return json.RawMessage(`{"ok":true}`), nil
}

func reviewStatus(read string) json.RawMessage {
	return json.RawMessage(`{"policy_generation":7,"permissions":{"read":"` + read + `","edit":"ask","move":"ask"},"capabilities":{"bulk_inventory_review":1}}`)
}

func reviewServer(api API) *Server {
	return &Server{
		Find:      func() ([]discover.Instance, error) { return []discover.Instance{testInstance(11)}, nil },
		NewClient: func(discover.Instance) API { return api },
	}
}

func TestBulkReviewForwardsOnlyNewProtocol(t *testing.T) {
	tests := []struct {
		tool string
		op   string
		args map[string]any
	}{
		{"inventory_preview_batch_rename", "previewBatchRename", map[string]any{"items": []any{map[string]any{"id": "item-1", "name": " Coat 🧥 "}}}},
		{"inventory_preview_batch_move", "previewBatchMove", map[string]any{"ids": []string{"item-1"}, "parent_id": "folder-1"}},
		{"inventory_execute_plan", "executeBulkPlan", map[string]any{"plan_id": "plan-1"}},
		{"inventory_history", "bulkHistory", map[string]any{}},
		{"inventory_history", "bulkHistory", map[string]any{"operation_id": "op-1", "offset": 2, "limit": 50}},
		{"inventory_preview_undo", "previewBulkUndo", map[string]any{"operation_id": "op-1", "ids": []string{"item-1"}}},
		{"inventory_preview_undo", "previewBulkUndo", map[string]any{"operation_id": "op-1"}},
	}
	for _, test := range tests {
		t.Run(test.tool+"/"+test.op, func(t *testing.T) {
			api := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("allow")}}
			res := call(t, connect(t, reviewServer(api), nil), test.tool, test.args)
			if res.IsError || count(api.opsCopy(), "LLInventory."+test.op) != 1 || count(api.opsCopy(), "status") != 1 {
				t.Fatalf("%s; ops=%v", textOf(res), api.opsCopy())
			}
			if len(api.opsCopy()) != 2 {
				t.Fatalf("unexpected operation/fallback: %v", api.opsCopy())
			}
			params := api.lastNamed
			for key, want := range test.args {
				wantJSON, _ := json.Marshal(want)
				gotJSON, _ := json.Marshal(params[key])
				if string(wantJSON) != string(gotJSON) {
					t.Fatalf("%s=%s; want %s", key, gotJSON, wantJSON)
				}
			}
			if test.tool == "inventory_history" && len(test.args) == 0 {
				if params["offset"] != 0 || params["limit"] != 20 {
					t.Fatalf("history defaults=%v", params)
				}
			}
		})
	}
}

func TestBulkReviewRequiresReadForExportedData(t *testing.T) {
	for _, test := range []struct {
		tool string
		args map[string]any
	}{
		{"inventory_preview_batch_rename", map[string]any{"items": []any{map[string]any{"id": "item", "name": "New"}}}},
		{"inventory_preview_batch_move", map[string]any{"ids": []string{"item"}, "parent_id": "folder"}},
		{"inventory_history", map[string]any{}},
		{"inventory_preview_undo", map[string]any{"operation_id": "operation"}},
	} {
		t.Run(test.tool, func(t *testing.T) {
			api := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("deny")}}
			res := call(t, connect(t, reviewServer(api), nil), test.tool, test.args)
			if !res.IsError || !strings.Contains(textOf(res), "not_permitted") || len(api.opsCopy()) != 1 {
				t.Fatalf("Read denial bypassed: %s; %v", textOf(res), api.opsCopy())
			}
		})
	}
}

func TestBulkExecuteReadDeniedUsesViewerOwnedClassOnce(t *testing.T) {
	api := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("deny")}, response: json.RawMessage(`{"operation_id":"op-1","status":"completed","results":[{"id":"item-1","ok":true,"status":"unconfirmed","undo_available":false}]}`)}
	cs := connect(t, reviewServer(api), &mcp.ClientOptions{ElicitationHandler: func(context.Context, *mcp.ElicitRequest) (*mcp.ElicitResult, error) {
		t.Fatal("sidecar duplicated viewer approval")
		return nil, nil
	}})
	forged := call(t, cs, "inventory_execute_plan", map[string]any{"plan_id": "plan-1", "required_class": "read", "confirm": true, "ids": []string{"injected"}, "deadline": 99999999})
	if !forged.IsError || len(api.opsCopy()) != 0 {
		t.Fatalf("schema accepted caller-supplied authority: %s; %v", textOf(forged), api.opsCopy())
	}
	res := call(t, cs, "inventory_execute_plan", map[string]any{"plan_id": "plan-1"})
	if res.IsError || count(api.opsCopy(), "LLInventory.executeBulkPlan") != 1 {
		t.Fatalf("execute with Read Never: %s; %v", textOf(res), api.opsCopy())
	}
	if len(api.lastNamed) != 1 || api.lastNamed["plan_id"] != "plan-1" {
		t.Fatalf("caller authority forwarded: %#v", api.lastNamed)
	}
	if !strings.Contains(textOf(res), "unconfirmed") || !strings.Contains(textOf(res), "op-1") {
		t.Fatalf("outcome discarded: %s", textOf(res))
	}
}

func TestBulkExecuteCapabilityFromReadDeniedStatus(t *testing.T) {
	api := &bulkReviewAPI{statusErr: &viewerapi.Error{Code: "forbidden", StatusCode: 403, Message: "Read denied", Body: reviewStatus("deny")}}
	res := call(t, connect(t, reviewServer(api), nil), "inventory_execute_plan", map[string]any{"plan_id": "plan-1"})
	if res.IsError || count(api.opsCopy(), "LLInventory.executeBulkPlan") != 1 {
		t.Fatalf("denied status lost capability: %s; %v", textOf(res), api.opsCopy())
	}
}

func TestBulkUnsupportedNeverFallsBackToMutation(t *testing.T) {
	for _, status := range []string{
		`{"logged_in":true}`,
		`{"permissions":{"read":"allow"}}`,
		`{"permissions":{"read":"allow"},"capabilities":{"bulk_inventory_review":true}}`,
		`{"permissions":{"read":"allow"},"capabilities":{"bulk_inventory_review":2}}`,
		`{"permissions":{"read":"allow"},"capabilities":{"bulk_inventory_review":1.5}}`,
		`{"capabilities":{"bulk_inventory_review":1}}`,
	} {
		api := &bulkReviewAPI{fakeAPI: fakeAPI{status: json.RawMessage(status)}}
		res := call(t, connect(t, reviewServer(api), nil), "inventory_execute_plan", map[string]any{"plan_id": "plan-1"})
		if !res.IsError || !strings.Contains(textOf(res), "unsupported_feature") || len(api.opsCopy()) != 1 {
			t.Fatalf("unsupported protocol attempted write: %s; %v", textOf(res), api.opsCopy())
		}
	}
}

func TestBulkCapabilityChangeWithSamePolicyGeneration(t *testing.T) {
	api := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("allow")}}
	cs := connect(t, reviewServer(api), nil)
	first := call(t, cs, "inventory_history", map[string]any{})
	if first.IsError {
		t.Fatal(textOf(first))
	}
	api.status = json.RawMessage(`{"policy_generation":7,"permissions":{"read":"allow"}}`)
	second := call(t, cs, "inventory_history", map[string]any{})
	if !second.IsError || !strings.Contains(textOf(second), "unsupported_feature") || count(api.opsCopy(), "LLInventory.bulkHistory") != 1 {
		t.Fatalf("stale capability survived: %s; %v", textOf(second), api.opsCopy())
	}
}

func TestBulkPreviewAvailableWhenMutationDenied(t *testing.T) {
	api := &bulkReviewAPI{fakeAPI: fakeAPI{status: json.RawMessage(`{"permissions":{"read":"allow","edit":"deny","move":"deny"},"capabilities":{"bulk_inventory_review":1}}`)}}
	cs := connect(t, reviewServer(api), nil)
	res := call(t, cs, "inventory_preview_batch_move", map[string]any{"ids": []string{"item"}, "parent_id": "folder"})
	if res.IsError || !contains(api.opsCopy(), "LLInventory.previewBatchMove") {
		t.Fatalf("preview treated as mutation: %s", textOf(res))
	}
}

func TestBulkInvalidInputDoesNotReachViewer(t *testing.T) {
	tooMany := make([]string, 51)
	for i := range tooMany {
		tooMany[i] = "item"
	}
	for _, test := range []struct {
		tool string
		args map[string]any
	}{
		{"inventory_preview_batch_move", map[string]any{"ids": []string{}, "parent_id": "folder"}},
		{"inventory_preview_batch_move", map[string]any{"ids": tooMany, "parent_id": "folder"}},
		{"inventory_preview_batch_move", map[string]any{"ids": []string{"item", "item"}, "parent_id": "folder"}},
		{"inventory_preview_batch_move", map[string]any{"ids": []string{" "}, "parent_id": "folder"}},
		{"inventory_preview_batch_rename", map[string]any{"items": []any{map[string]any{"id": "item", "name": " \t"}}}},
		{"inventory_preview_batch_rename", map[string]any{"items": []any{map[string]any{"id": "item", "name": "A"}, map[string]any{"id": "item", "name": "B"}}}},
		{"inventory_execute_plan", map[string]any{"plan_id": " "}},
		{"inventory_history", map[string]any{"offset": -1}},
		{"inventory_history", map[string]any{"limit": 51}},
		{"inventory_preview_undo", map[string]any{"operation_id": "op", "ids": []string{}}},
		{"inventory_preview_undo", map[string]any{"operation_id": "op", "ids": []string{"item", "item"}}},
	} {
		api := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("allow")}}
		res := call(t, connect(t, reviewServer(api), nil), test.tool, test.args)
		if !res.IsError || !strings.Contains(textOf(res), "invalid_args") || len(api.opsCopy()) != 0 {
			t.Fatalf("invalid input reached viewer: %s %#v => %s; %v", test.tool, test.args, textOf(res), api.opsCopy())
		}
	}
}

func TestBulkPartialResultsRemainStructured(t *testing.T) {
	api := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("allow")}, response: json.RawMessage(`{"operation_id":"op","status":"completed","results":[{"id":"a","ok":true,"status":"confirmed","undo_available":true},{"id":"b","ok":false,"status":"failed","error":"Server refused update"},{"id":"c","ok":true,"status":"unconfirmed","undo_available":false}]}`)}
	res := call(t, connect(t, reviewServer(api), nil), "inventory_execute_plan", map[string]any{"plan_id": "plan"})
	if res.IsError {
		t.Fatalf("partial outcomes discarded: %s", textOf(res))
	}
	for _, value := range []string{"confirmed", "failed", "unconfirmed", "Server refused update"} {
		if !strings.Contains(textOf(res), value) {
			t.Fatalf("missing %s: %s", value, textOf(res))
		}
	}
}

func TestBulkPlanUsesExplicitSelectedViewer(t *testing.T) {
	a := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("allow")}}
	b := &bulkReviewAPI{fakeAPI: fakeAPI{status: reviewStatus("allow")}}
	state := &Server{Find: func() ([]discover.Instance, error) {
		return []discover.Instance{testInstance(11), testInstance(12)}, nil
	}, NewClient: func(inst discover.Instance) API {
		if inst.PID == 11 {
			return a
		}
		return b
	}}
	res := call(t, connect(t, state, nil), "inventory_execute_plan", map[string]any{"plan_id": "plan", "viewer_pid": 12})
	if res.IsError || len(a.opsCopy()) != 0 || count(b.opsCopy(), "LLInventory.executeBulkPlan") != 1 {
		t.Fatalf("wrong viewer received plan: %s; a=%v b=%v", textOf(res), a.opsCopy(), b.opsCopy())
	}
}
