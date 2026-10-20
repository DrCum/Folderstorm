package mcptools

import (
	"context"
	"encoding/json"
	"fs-mcp/internal/discover"
	"github.com/modelcontextprotocol/go-sdk/mcp"
	"math"
	"reflect"
	"testing"
)

const buildID = "11111111-2222-4333-8444-555555555555"
const buildRevision = "22222222-3333-4444-8555-666666666666"

func buildingFixture() (*Server, *preferencesTestAPI) {
	api := &preferencesTestAPI{fakeAPI: fakeAPI{status: json.RawMessage(`{"policy_generation":1,"permissions":{"read":"allow","world_read":"allow","build_edit":"ask","build_rez":"ask","inventory_permissions":"ask"}}`)}, health: json.RawMessage(`{"capabilities":{"building_tools":1}}`)}
	state := reviewServer(api)
	inst := testInstance(11)
	inst.ViewerSession = "login-a"
	state.Find = func() ([]discover.Instance, error) { return []discover.Instance{inst}, nil }
	return state, api
}
func buildingClient(t *testing.T, state *Server) *mcp.ClientSession {
	t.Helper()
	server := mcp.NewServer(&mcp.Implementation{Name: "building-test", Version: "1"}, nil)
	registerBuilding(server, state, &mcp.ToolAnnotations{ReadOnlyHint: true}, &mcp.ToolAnnotations{})
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
func TestBuildingExactNativeSurface(t *testing.T) {
	for _, test := range []struct {
		name, op string
		p        map[string]any
	}{
		{"build_object_get", "objectGet", map[string]any{"id": buildID}},
		{"inventory_permissions_get", "permissionsGet", map[string]any{"id": buildID}},
		{"build_object_transform", "transform", map[string]any{"id": buildID, "expected_revision": buildRevision, "position": []float64{20, 25, 30}}},
		{"build_object_surface", "surface", map[string]any{"id": buildID, "expected_revision": buildRevision, "face": 0, "fullbright": false}},
		{"build_object_material", "material", map[string]any{"id": buildID, "expected_revision": buildRevision, "face": 2, "material_item": buildID}},
		{"build_rez_inventory", "rezInventory", map[string]any{"id": buildID, "position": []float64{20, 25, 30}}},
		{"build_rez_primitive", "rezPrimitive", map[string]any{"shape": "cube", "position": []float64{20, 25, 30}, "scale": []float64{1, 1, 1}, "rotation": []float64{0, 0, 0, 1}}},
		{"inventory_next_owner_set", "nextOwnerSet", map[string]any{"id": buildID, "expected_revision": buildRevision, "copy": false, "modify": false, "transfer": true}},
	} {
		t.Run(test.name, func(t *testing.T) {
			s, api := buildingFixture()
			client := buildingClient(t, s)
			test.p["viewer_session"] = "login-a"
			r := call(t, client, test.name, test.p)
			if r.IsError {
				t.Fatal(textOf(r))
			}
			if !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health", "FSBuild." + test.op}) {
				t.Fatal(api.opsCopy())
			}
			delete(test.p, "viewer_session")
			got, _ := json.Marshal(api.lastNamed)
			want, _ := json.Marshal(test.p)
			if string(got) != string(want) {
				t.Fatalf("%s != %s", got, want)
			}
		})
	}
}
func TestBuildingRejectsUnsafeMaterialBeforeTransport(t *testing.T) {
	s, api := buildingFixture()
	ctx := context.Background()
	cases := []struct {
		op   string
		args any
	}{
		{"transform", buildingTransformArgs{ID: buildID, ExpectedRevision: buildRevision, Position: []float64{math.NaN(), 0, 1}}},
		{"transform", buildingTransformArgs{ID: buildID, ExpectedRevision: buildRevision, Rotation: []float64{0, 0, 0, 0}}},
		{"nextOwnerSet", buildingNextOwnerArgs{ID: buildID, ExpectedRevision: buildRevision}},
		{"surface", buildingSurfaceArgs{ID: buildID, ExpectedRevision: buildRevision, Face: 64}},
	}
	for _, c := range cases {
		r, _, _ := s.buildingCall(ctx, "login-a", 0, c.op, c.args)
		if !r.IsError {
			t.Fatal(c.op)
		}
	}
	if len(api.opsCopy()) != 0 {
		t.Fatal(api.opsCopy())
	}
	r, _, _ := s.buildingGet(ctx, nil, buildingTargetArgs{ID: buildID})
	if !r.IsError {
		t.Fatal("missing session")
	}
	r, _, _ = s.buildingGet(ctx, nil, buildingTargetArgs{ID: buildID, ViewerSession: "other-login"})
	if !r.IsError {
		t.Fatal("stale login")
	}
	api.health = json.RawMessage(`{"capabilities":{"building_tools":2}}`)
	r, _, _ = s.buildingGet(ctx, nil, buildingTargetArgs{ID: buildID, ViewerSession: "login-a"})
	if !r.IsError {
		t.Fatal("wrong version")
	}
	for _, op := range api.opsCopy() {
		if op == "FSBuild.objectGet" {
			t.Fatal("fallback")
		}
	}
}
func TestBuildingSchemasKeepExplicitSession(t *testing.T) {
	s, _ := buildingFixture()
	cs := buildingClient(t, s)
	list, err := cs.ListTools(context.Background(), nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(list.Tools) != 8 {
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
