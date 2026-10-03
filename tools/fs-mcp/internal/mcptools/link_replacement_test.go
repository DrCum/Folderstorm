package mcptools

import (
	"context"
	"encoding/json"
	"testing"

	"fs-mcp/internal/discover"
)

type replacementResultAPI struct {
	fakeAPI
	outcome json.RawMessage
}

func (f *replacementResultAPI) CallNamed(ctx context.Context, apiName, op string, params map[string]any) (json.RawMessage, error) {
	if op == "replaceLinks" {
		f.record(apiName + "." + op)
		return f.outcome, nil
	}
	return f.fakeAPI.CallNamed(ctx, apiName, op, params)
}

func TestLinkReplacementPreservesPartialViewerResults(t *testing.T) {
	api := &replacementResultAPI{
		fakeAPI: fakeAPI{status: json.RawMessage(`{"logged_in":true,"permissions":{"links":"allow"},"policy_generation":1}`)},
		outcome: json.RawMessage(`{"ok":false,"count":2,"completed_count":1,"failed_count":1,"skipped_count":0,"partial":true,"operation_id":"attempt","results":[{"id":"old1","parent_id":"folder","new_id":"new1","ok":true,"status":"replacement_created_trash_submitted","trash_state":"submitted","original_preserved":false,"retry_safe":true},{"id":"old2","parent_id":"folder","ok":false,"status":"creation_unconfirmed","error":"Creation deadline expired","trash_state":"not_submitted","original_preserved":true,"retry_safe":false}]}`),
	}
	state := &Server{
		Find:      func() ([]discover.Instance, error) { return []discover.Instance{testInstance(11)}, nil },
		NewClient: func(discover.Instance) API { return api },
	}
	cs := connect(t, state, nil)
	result := call(t, cs, "inventory_replace_links", map[string]any{"source_id": "source", "target_id": "target"})
	if result.IsError {
		t.Fatalf("partial viewer outcome was discarded as tool error: %s", textOf(result))
	}
	var payload map[string]any
	if err := json.Unmarshal([]byte(textOf(result)), &payload); err != nil {
		t.Fatal(err)
	}
	if payload["ok"] != false || payload["partial"] != true || payload["operation_id"] != "attempt" {
		t.Fatalf("partial result metadata lost: %#v", payload)
	}
	rows := payload["results"].([]any)
	if len(rows) != 2 || rows[0].(map[string]any)["new_id"] != "new1" ||
		rows[0].(map[string]any)["trash_state"] != "submitted" ||
		rows[1].(map[string]any)["retry_safe"] != false ||
		rows[1].(map[string]any)["error"] != "Creation deadline expired" {
		t.Fatalf("per-link outcomes lost: %#v", rows)
	}
}
