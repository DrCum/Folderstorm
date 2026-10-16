package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"sync"
	"testing"
)

type sessionAPI struct {
	fakeAPI
	instance discover.Instance
	wrong    bool
}

func (a *sessionAPI) CallNamed(ctx context.Context, api, op string, params map[string]any) (json.RawMessage, error) {
	if api == "LocalAssistant" && op == "health" {
		ref := a.instance.ViewerSession
		if a.wrong {
			ref = "other"
		}
		raw, _ := json.Marshal(map[string]any{"viewer_session": ref, "account": a.instance.Account, "grid": a.instance.Grid, "capabilities": map[string]int{"managed_sessions": 1}})
		return raw, nil
	}
	return a.fakeAPI.CallNamed(ctx, api, op, params)
}
func TestSessionTargetNeverFallsBack(t *testing.T) {
	ctx := context.Background()
	a := discover.Instance{PID: 11, Token: "one", ViewerSession: "login-a"}
	b := discover.Instance{PID: 22, Token: "two", ViewerSession: "login-b"}
	live := []discover.Instance{a, b}
	s := New()
	s.Find = func() ([]discover.Instance, error) { return live, nil }
	s.NewClient = func(i discover.Instance) API { return &sessionAPI{instance: i} }
	if r, _, _ := s.viewerSelect(ctx, nil, selectArgs{PID: a.PID}); r.IsError {
		t.Fatal(r)
	}
	live = []discover.Instance{b}
	if _, _, r := s.resolve(withViewerSession(ctx, ""), 0); r == nil || !r.IsError {
		t.Fatal("missing selection fell back")
	}
	live = []discover.Instance{{PID: 11, Token: "new", ViewerSession: "replacement"}, b}
	if _, _, r := s.resolve(withViewerSession(ctx, ""), 0); r == nil || !r.IsError {
		t.Fatal("PID reuse adopted")
	}
	if _, _, r := s.resolve(withViewerSession(ctx, a.ViewerSession), 0); r == nil || !r.IsError {
		t.Fatal("old reference adopted")
	}
	if inst, _, r := s.resolve(withViewerSession(ctx, b.ViewerSession), 0); r != nil || inst.PID != b.PID {
		t.Fatal("explicit new choice failed", r)
	}
}
func TestManagedSessionsAreExplicitAndAuthenticated(t *testing.T) {
	a := discover.Instance{PID: 11, Token: "one", ViewerSession: "login-a", Managed: true, Account: "account-a", Grid: "grid"}
	s := New()
	s.Find = func() ([]discover.Instance, error) { return []discover.Instance{a}, nil }
	wrong := false
	s.NewClient = func(i discover.Instance) API { return &sessionAPI{instance: i, wrong: wrong} }
	for _, ctx := range []context.Context{context.Background(), withViewerSession(context.Background(), "")} {
		if _, _, r := s.resolve(ctx, a.PID); r == nil {
			t.Fatal("managed PID-only call accepted")
		}
	}
	if _, _, r := s.resolve(withViewerSession(context.Background(), a.ViewerSession), 22); r == nil {
		t.Fatal("conflicting PID accepted")
	}
	if r, _, _ := s.inventoryGet(context.Background(), nil, getArgs{ID: "item", ViewerSession: a.ViewerSession}); r.IsError {
		t.Fatal("typed tool did not propagate reference", r)
	}
	wrong = true
	if _, _, r := s.resolve(withViewerSession(context.Background(), a.ViewerSession), 0); r == nil {
		t.Fatal("mismatched authenticated identity accepted")
	}
}
func TestSessionRequestPinningAndConcurrentTargets(t *testing.T) {
	a := discover.Instance{PID: 11, Token: "one", ViewerSession: "a"}
	b := discover.Instance{PID: 22, Token: "two", ViewerSession: "b"}
	s := New()
	s.Find = func() ([]discover.Instance, error) { return []discover.Instance{a, b}, nil }
	s.NewClient = func(i discover.Instance) API { return &sessionAPI{instance: i} }
	var wg sync.WaitGroup
	for _, ref := range []string{"a", "b"} {
		wg.Add(1)
		go func(ref string) {
			defer wg.Done()
			ctx := withViewerSession(context.Background(), ref)
			inst, _, r := s.resolve(ctx, 0)
			if r != nil || inst.ViewerSession != ref {
				t.Error("wrong source", r)
			}
			other := 11
			if ref == "a" {
				other = 22
			}
			if _, _, r = s.resolve(ctx, other); r == nil {
				t.Error("request was rebound")
			}
		}(ref)
	}
	wg.Wait()
}
func TestCopyContinuationCannotChangeLogin(t *testing.T) {
	a := discover.Instance{PID: 11, Token: "one", ViewerSession: "a"}
	s := New()
	s.Find = func() ([]discover.Instance, error) { return []discover.Instance{a}, nil }
	s.NewClient = func(i discover.Instance) API { return &sessionAPI{instance: i} }
	ctx := withViewerSession(context.Background(), a.ViewerSession)
	if _, _, r := s.resolve(ctx, 0); r != nil {
		t.Fatal(r)
	}
	s.rememberCopyTargets(ctx, []string{"copy"})
	a.Token = "replacement"
	a.ViewerSession = "new-login"
	if _, _, r := s.copyTarget(context.Background(), "copy", 0); r == nil {
		t.Fatal("copy migrated to new login")
	}
}

func TestEveryToolKeepsExplicitSessionInSchema(t *testing.T) {
	// Host commands use a separate authority from any selected viewer. Keep an
	// explicit set so a new tool cannot silently bypass either targeting rule.
	controllerTools := map[string]bool{
		"sessions_list": true, "session_status": true, "controller_operation_status": true,
		"session_activate": true, "session_background_mode": true, "session_open_login": true,
		"session_restart": true, "session_logout": true, "session_detach": true,
		"monitor_open": true, "monitor_close": true, "monitor_list": true,
		"monitors_open_all": true, "monitors_arrange": true, "monitor_cycle_configure": true,
		"monitor_cycle_pause": true, "monitor_cycle_next": true,
	}
	cs := connect(t, New(), nil)
	list, err := cs.ListTools(context.Background(), nil)
	if err != nil {
		t.Fatal(err)
	}
	for _, tool := range list.Tools {
		if tool.Name == "viewer_list" || tool.Name == "controller_list" {
			continue
		}
		target := "viewer_session"
		if controllerTools[tool.Name] {
			target = "controller_instance"
		}
		raw, _ := json.Marshal(tool.InputSchema)
		var schema struct {
			Properties map[string]any `json:"properties"`
		}
		if json.Unmarshal(raw, &schema) != nil || schema.Properties[target] == nil {
			t.Errorf("%s lacks %s", tool.Name, target)
		}
	}
}
