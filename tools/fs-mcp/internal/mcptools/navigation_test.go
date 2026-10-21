package mcptools

import (
	"context"
	"encoding/json"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"math"
	"reflect"
	"testing"
)

func navigationClient(t *testing.T, state *Server) *mcp.ClientSession {
	t.Helper()
	server := mcp.NewServer(&mcp.Implementation{Name: "navigation-test", Version: "1"}, nil)
	registerNavigation(server, state, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
	ctx := context.Background()
	ct, st := mcp.NewInMemoryTransports()
	ss, err := server.Connect(ctx, st, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = ss.Close() })
	client := mcp.NewClient(&mcp.Implementation{Name: "client", Version: "1"}, nil)
	cs, err := client.Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs
}
func TestNavigationExactNativeSurface(t *testing.T) {
	for _, test := range []struct {
		name, op string
		p        map[string]any
	}{
		{"navigation_status", "status", map[string]any{}},
		{"navigation_move_to", "moveTo", map[string]any{"position": []float64{200000, 200001, 30}, "mode": "walk", "stop_distance": .5, "timeout_seconds": 15}},
		{"navigation_flying_set", "flyingSet", map[string]any{"flying": false}},
		{"navigation_teleport", "teleport", map[string]any{"position": []float64{200000, 200001, 30}}},
		{"navigation_stop", "stop", map[string]any{"operation_id": "assistant-nav-owned"}},
	} {
		t.Run(test.name, func(t *testing.T) {
			s, api := buildingFixture()
			api.status = json.RawMessage(`{"permissions":{"world_read":"allow","navigation_move":"ask","navigation_teleport":"ask"}}`)
			api.health = json.RawMessage(`{"capabilities":{"navigation_tools":1}}`)
			cs := navigationClient(t, s)
			test.p["viewer_session"] = "login-a"
			r := call(t, cs, test.name, test.p)
			if r.IsError {
				t.Fatal(textOf(r))
			}
			if !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health", "FSNavigation." + test.op}) {
				t.Fatal(api.opsCopy())
			}
			delete(test.p, "viewer_session")
			want, _ := json.Marshal(test.p)
			got, _ := json.Marshal(api.lastNamed)
			if string(want) != string(got) {
				t.Fatalf("%s != %s", want, got)
			}
		})
	}
}
func TestNavigationInvalidSourceAndBoundsNeverDispatch(t *testing.T) {
	s, api := buildingFixture()
	ctx := context.Background()
	for _, a := range []navigationMoveArgs{
		{Position: []float64{1, 2, math.Inf(1)}, Mode: "walk", StopDistance: .5, TimeoutSeconds: 10, ViewerSession: "login-a"},
		{Position: []float64{1, 2, 3}, Mode: "follow", StopDistance: .5, TimeoutSeconds: 10, ViewerSession: "login-a"},
		{Position: []float64{1, 2, 3}, Mode: "walk", StopDistance: .5, TimeoutSeconds: 31, ViewerSession: "login-a"},
		{Position: []float64{1, 2, 3}, Mode: "walk", StopDistance: .5, TimeoutSeconds: 10},
	} {
		r, _, _ := s.navigationMove(ctx, nil, a)
		if !r.IsError {
			t.Fatal(a)
		}
	}
	if len(api.opsCopy()) != 0 {
		t.Fatal(api.opsCopy())
	}
	r, _, _ := s.navigationStatus(ctx, nil, navigationViewerArgs{ViewerSession: "stale"})
	if !r.IsError {
		t.Fatal("stale source")
	}
	api.status = json.RawMessage(`{"permissions":{"world_read":"allow","navigation_move":"deny","navigation_teleport":"deny"}}`)
	r, _, _ = s.navigationFlying(ctx, nil, navigationFlyingArgs{Flying: true, ViewerSession: "login-a"})
	if !r.IsError {
		t.Fatal("denied permission")
	}
	for _, op := range api.opsCopy() {
		if op == "FSNavigation.flyingSet" {
			t.Fatal("denied dispatch")
		}
	}
}
func TestNavigationSchemasKeepExplicitSession(t *testing.T) {
	s, _ := buildingFixture()
	cs := navigationClient(t, s)
	list, err := cs.ListTools(context.Background(), nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(list.Tools) != 5 {
		t.Fatal(len(list.Tools))
	}
	for _, tool := range list.Tools {
		raw, _ := json.Marshal(tool.InputSchema)
		var schema struct {
			Required []string `json:"required"`
		}
		_ = json.Unmarshal(raw, &schema)
		found := false
		for _, key := range schema.Required {
			found = found || key == "viewer_session"
		}
		if !found {
			t.Fatal(tool.Name)
		}
	}
}

func TestNavigationStopIsOwnedCleanupWithoutNewMovePermission(t *testing.T) {
	s, api := buildingFixture()
	api.status = json.RawMessage(`{"permissions":{"world_read":"allow","navigation_move":"deny","navigation_teleport":"deny"}}`)
	api.health = json.RawMessage(`{"capabilities":{"navigation_tools":1}}`)
	r, _, _ := s.navigationStop(context.Background(), nil, navigationStopArgs{OperationID: "assistant-nav-owned", ViewerSession: "login-a"})
	if r.IsError {
		t.Fatal(textOf(r))
	}
	if !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health", "FSNavigation.stop"}) {
		t.Fatal(api.opsCopy())
	}
	// Native dispatch still verifies the original task and exact token; this
	// does not authorize any new flight, move, or teleport.
}
