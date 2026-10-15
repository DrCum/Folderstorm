package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"sync"
	"time"
)

type sessionContextKey struct{}
type sessionRequest struct {
	mu        sync.Mutex
	reference string
	bound     *discover.Instance
}

func withViewerSession(ctx context.Context, reference string) context.Context {
	return context.WithValue(ctx, sessionContextKey{}, &sessionRequest{reference: reference})
}

func (s *Server) resolveSession(ctx context.Context, pid int) (discover.Instance, API, *mcp.CallToolResult) {
	fail := func(code, message string) (discover.Instance, API, *mcp.CallToolResult) {
		r, _, _ := errorResult(code, message, nil)
		return discover.Instance{}, nil, r
	}
	request, _ := ctx.Value(sessionContextKey{}).(*sessionRequest)
	reference, key := "", ""
	if request != nil {
		request.mu.Lock()
		defer request.mu.Unlock()
		reference = request.reference
		if request.bound != nil {
			if pid != 0 && pid != request.bound.PID {
				return fail("session_changed", "Request target changed")
			}
			pid = request.bound.PID
			reference = request.bound.ViewerSession
			key = request.bound.BindingKey()
		}
	}
	if len(reference) > 128 || pid < 0 {
		return fail("invalid_args", "Invalid viewer target")
	}
	instances, err := s.find()
	if err != nil {
		return fail("discovery_failed", err.Error())
	}
	var found *discover.Instance
	if reference != "" {
		for i := range instances {
			if instances[i].ViewerSession == reference {
				if found != nil {
					return fail("ambiguous_target", "Duplicate session references")
				}
				found = &instances[i]
			}
		}
		if found == nil {
			return fail("session_changed", "The selected login is no longer available; list viewers and choose a fresh session")
		}
		if pid != 0 && found.PID != pid {
			return fail("session_changed", "PID and login reference identify different viewers")
		}
	} else {
		want := pid
		if want == 0 {
			s.mu.Lock()
			want = s.selectedPID
			key = s.selectedKey
			s.mu.Unlock()
		}
		if want != 0 {
			for i := range instances {
				if instances[i].PID == want {
					found = &instances[i]
					break
				}
			}
			if found == nil {
				return fail("target_unavailable", "The selected viewer is unavailable; choose a target explicitly")
			}
		} else if len(instances) == 1 {
			found = &instances[0]
			if !found.Managed {
				s.mu.Lock()
				s.selectedPID = found.PID
				s.selectedKey = found.BindingKey()
				s.mu.Unlock()
			}
		} else if len(instances) == 0 {
			return fail("viewer_not_found", "No live MCP bridge found; enable Local Assistant for the desired profile")
		} else {
			r, _, _ := errorResult("multiple_viewers", "Call viewer_list and specify viewer_session", map[string]any{"viewers": redactedInstances(instances)})
			return discover.Instance{}, nil, r
		}
		if found.Managed {
			return fail("target_required", "Managed characters require viewer_session from viewer_list")
		}
	}
	if key != "" && key != found.BindingKey() {
		return fail("session_changed", "The selected viewer login or connection changed")
	}
	inst, api, res := s.clientFor(*found)
	if res != nil {
		return inst, api, res
	}
	if inst.Managed {
		raw, e := api.CallNamed(ctx, "LocalAssistant", "health", map[string]any{"diagnostic": true})
		if e != nil {
			r, _, _ := apiError(e)
			return discover.Instance{}, nil, r
		}
		var health struct {
			Session      string `json:"viewer_session"`
			Account      string `json:"account"`
			Grid         string `json:"grid"`
			Capabilities struct {
				Managed int `json:"managed_sessions"`
			} `json:"capabilities"`
		}
		if len(raw) > 65536 || json.Unmarshal(raw, &health) != nil || health.Session != inst.ViewerSession || health.Account != inst.Account || health.Grid != inst.Grid || health.Capabilities.Managed != 1 {
			return fail("session_changed", "Authenticated viewer identity/capability does not match discovery")
		}
	}
	if request != nil && request.bound == nil {
		copy := inst
		request.bound = &copy
	}
	return inst, api, nil
}

// Pin sidecar continuations to the connection that prepared them, including
// old standalone viewers without public session references.
func requestIdentity(ctx context.Context) (discover.Instance, bool) {
	r, _ := ctx.Value(sessionContextKey{}).(*sessionRequest)
	if r == nil {
		return discover.Instance{}, false
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.bound == nil {
		return discover.Instance{}, false
	}
	return *r.bound, true
}

func (s *Server) rememberCopyTargets(ctx context.Context, ids []string) {
	inst, ok := requestIdentity(ctx)
	if !ok {
		return
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if s.plans == nil {
		s.plans = map[string]storedPlan{}
	}
	s.prunePlansLocked()
	for _, id := range ids {
		s.plans["copy:"+id] = storedPlan{ViewerPID: inst.PID, ViewerSession: inst.ViewerSession, TargetKey: inst.BindingKey(), Managed: inst.Managed, Expires: time.Now().Add(10 * time.Minute)}
	}
}
func (s *Server) copyTarget(ctx context.Context, id string, pid int) (context.Context, int, *mcp.CallToolResult) {
	s.mu.Lock()
	plan, ok := s.plans["copy:"+id]
	s.mu.Unlock()
	if !ok {
		return ctx, pid, nil
	}
	fail := func(message string) (context.Context, int, *mcp.CallToolResult) {
		r, _, _ := errorResult("session_changed", message, nil)
		return ctx, pid, r
	}
	if time.Now().After(plan.Expires) || (pid != 0 && pid != plan.ViewerPID) {
		return fail("Copy confirmation no longer belongs to this viewer")
	}
	caller, _ := ctx.Value(sessionContextKey{}).(*sessionRequest)
	if caller != nil && caller.reference != "" && caller.reference != plan.ViewerSession {
		return fail("Copy confirmation cannot change its login")
	}
	if plan.Managed && (caller == nil || caller.reference == "") {
		r, _, _ := errorResult("target_required", "Specify the preparing viewer_session", nil)
		return ctx, pid, r
	}
	ctx = withViewerSession(ctx, plan.ViewerSession)
	inst, _, err := s.resolve(ctx, plan.ViewerPID)
	if err != nil {
		return ctx, pid, err
	}
	if inst.BindingKey() != plan.TargetKey {
		return fail("The login which prepared this copy changed")
	}
	return ctx, plan.ViewerPID, nil
}
