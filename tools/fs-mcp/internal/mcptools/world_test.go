package mcptools

import (
	"context"
	"encoding/json"
	"math"
	"reflect"
	"strings"
	"testing"

	"fs-mcp/internal/discover"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type worldTestAPI struct {
	fakeAPI
	health json.RawMessage
	result json.RawMessage
}

func (a *worldTestAPI) CallNamed(ctx context.Context, api, op string, params map[string]any) (json.RawMessage, error) {
	_, _ = a.fakeAPI.CallNamed(ctx, api, op, params)
	if api == "LocalAssistant" && op == "health" {
		return a.health, nil
	}
	if a.result != nil {
		return a.result, nil
	}
	return json.RawMessage(`{"status":"observed","entries":[],"complete":false}`), nil
}

func TestWorldCapabilityIsExactAndBounded(t *testing.T) {
	for _, raw := range []string{
		`{}`, `null`, `{"capabilities":null}`, `{"capabilities":{"world_observation":true}}`,
		`{"capabilities":{"world_observation":"1"}}`, `{"capabilities":{"world_observation":2}}`,
		`{"capabilities":{"world_observation":1.5}}`, `{"capabilities":{"world_observation":0}}`,
		`{"capabilities":{"world_observation":1}}` + strings.Repeat(" ", 65536),
	} {
		if worldCapability([]byte(raw)) {
			t.Fatalf("accepted unsupported capability: %.100s", raw)
		}
	}
	if !worldCapability([]byte(`{"capabilities":{"world_observation":1}}`)) {
		t.Fatal("supported capability refused")
	}
}

func TestWorldObservationsNeverFallBackToAgentOrControls(t *testing.T) {
	for _, op := range []string{"location", "parcel", "avatars", "objects", "audio", "media"} {
		api := &worldTestAPI{health: json.RawMessage(`{"capabilities":{"world_observation":1}}`)}
		params := map[string]any{"radius": 42., "limit": 3}
		res, _, err := callWorldObservation(context.Background(), api, op, params)
		if err != nil || res.IsError || !reflect.DeepEqual(api.opsCopy(), []string{"LocalAssistant.health", "FSWorld." + op}) || !reflect.DeepEqual(api.lastNamed, params) {
			t.Fatalf("%s: result=%v error=%v operations=%v params=%v", op, res, err, api.opsCopy(), api.lastNamed)
		}
	}
	for _, op := range []string{"requestTeleport", "startAutoPilot", "requestSit", "requestStand", "requestTouch", "set", "build"} {
		api := &worldTestAPI{health: json.RawMessage(`{"capabilities":{"world_observation":1}}`)}
		res, _, _ := callWorldObservation(context.Background(), api, op, nil)
		if !res.IsError || len(api.opsCopy()) != 0 {
			t.Fatalf("excluded operation %s reached viewer: %v", op, api.opsCopy())
		}
	}
}

func TestWorldCapabilityRecheckedAndNoLegacyRead(t *testing.T) {
	api := &worldTestAPI{health: json.RawMessage(`{"capabilities":{"world_observation":1}}`)}
	if result, _, _ := callWorldObservation(context.Background(), api, "location", nil); result.IsError {
		t.Fatal("first supported read failed")
	}
	api.health = json.RawMessage(`{"capabilities":{"world_observation":2}}`)
	result, _, _ := callWorldObservation(context.Background(), api, "location", nil)
	if !result.IsError || !strings.Contains(textOf(result), "unsupported_feature") || count(api.opsCopy(), "FSWorld.location") != 1 || count(api.opsCopy(), "LocalAssistant.health") != 2 {
		t.Fatalf("stale capability survived: %s; operations=%v", textOf(result), api.opsCopy())
	}
}

func TestWorldNearbyBoundsBeforeDiscovery(t *testing.T) {
	for _, args := range []worldNearbyArgs{
		{Radius: worldFloat(0)}, {Radius: worldFloat(257)}, {Radius: worldFloat(math.NaN())},
		{Radius: worldFloat(math.Inf(1))}, {Limit: worldInt(0)}, {Limit: worldInt(101)},
	} {
		state := &Server{Find: func() ([]discover.Instance, error) { t.Fatal("invalid bounds reached discovery"); return nil, nil }}
		res, _, _ := state.worldObjects(context.Background(), nil, args)
		if !res.IsError || !strings.Contains(textOf(res), "invalid_args") {
			t.Fatalf("invalid arguments accepted: %s", textOf(res))
		}
	}
	for _, args := range []worldNearbyArgs{{}, {Radius: worldFloat(1), Limit: worldInt(1)}, {Radius: worldFloat(256), Limit: worldInt(100)}} {
		if _, valid := worldNearbyParams(args); !valid {
			t.Fatal("valid bounds refused")
		}
	}
	params, _ := worldNearbyParams(worldNearbyArgs{})
	if params["radius"] != 64. || params["limit"] != 25 {
		t.Fatal("wrong defaults", params)
	}
}

func TestWorldRequiresExplicitPermissionSupport(t *testing.T) {
	for _, status := range []string{`{}`, `{"permissions":{"read":"allow"}}`, `{"permissions":{"world_read":"deny"}}`} {
		api := &worldTestAPI{fakeAPI: fakeAPI{status: json.RawMessage(status)}, health: json.RawMessage(`{"capabilities":{"world_observation":1}}`)}
		res, _, _ := reviewServer(api).worldLocation(context.Background(), nil, viewerArgs{})
		if !res.IsError || count(api.opsCopy(), "FSWorld.location") != 0 {
			t.Fatalf("world opt-in bypassed: %s; operations=%v", textOf(res), api.opsCopy())
		}
	}
}

func TestWorldTypedWrappersUseOptInClassAndExactFeature(t *testing.T) {
	for _, op := range []string{"location", "parcel", "avatars", "objects", "audio", "media"} {
		api := &worldTestAPI{
			fakeAPI: fakeAPI{status: json.RawMessage(`{"policy_generation":1,"permissions":{"read":"deny","world_read":"allow"}}`)},
			health:  json.RawMessage(`{"capabilities":{"world_observation":1}}`),
		}
		state := reviewServer(api)
		var result *mcp.CallToolResult
		switch op {
		case "location":
			result, _, _ = state.worldLocation(context.Background(), nil, viewerArgs{})
		case "parcel":
			result, _, _ = state.worldParcel(context.Background(), nil, viewerArgs{})
		case "avatars":
			result, _, _ = state.worldAvatars(context.Background(), nil, worldNearbyArgs{})
		case "objects":
			result, _, _ = state.worldObjects(context.Background(), nil, worldNearbyArgs{})
		case "audio":
			result, _, _ = state.worldAudio(context.Background(), nil, worldNearbyArgs{})
		case "media":
			result, _, _ = state.worldMedia(context.Background(), nil, worldNearbyArgs{})
		}
		if result == nil || result.IsError || !reflect.DeepEqual(api.opsCopy(), []string{"status", "LocalAssistant.health", "FSWorld." + op}) {
			t.Fatalf("%s did not use its own read permission: %v; operations=%v", op, result, api.opsCopy())
		}
	}
}

func TestWorldWrongLoginNeverReads(t *testing.T) {
	api := &worldTestAPI{fakeAPI: fakeAPI{status: json.RawMessage(`{}`)}}
	state := reviewServer(api)
	res, _, _ := state.worldLocation(context.Background(), nil, viewerArgs{ViewerSession: "expired-login"})
	if !res.IsError || len(api.opsCopy()) != 0 {
		t.Fatalf("old login read another viewer: %s; operations=%v", textOf(res), api.opsCopy())
	}
}

func TestWorldPreservesUnknownRestrictedAndUnavailable(t *testing.T) {
	for _, payload := range []string{
		`{"status":"restricted","reason":"native_visibility_restriction"}`,
		`{"status":"unavailable","reason":"audio_engine_unavailable"}`,
		`{"status":"observed","entries":[],"complete":false,"audibility":"unknown"}`,
	} {
		api := &worldTestAPI{health: json.RawMessage(`{"capabilities":{"world_observation":1}}`), result: json.RawMessage(payload)}
		res, data, err := callWorldObservation(context.Background(), api, "audio", nil)
		if err != nil || res.IsError {
			t.Fatalf("observation state treated as transport failure: %v %v", res, err)
		}
		got, _ := json.Marshal(data)
		var expected any
		_ = json.Unmarshal([]byte(payload), &expected)
		want, _ := json.Marshal(expected)
		if string(got) != string(want) {
			t.Fatalf("observation evidence changed: got=%s want=%s", got, want)
		}
	}
}

func TestWorldToolsRegisterOnlySixReadOperations(t *testing.T) {
	server := mcp.NewServer(&mcp.Implementation{Name: "world-test", Version: "1"}, nil)
	registerWorld(server, reviewServer(&worldTestAPI{}), &mcp.ToolAnnotations{ReadOnlyHint: true})
	ctx := context.Background()
	ct, st := mcp.NewInMemoryTransports()
	ss, err := server.Connect(ctx, st, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer ss.Close()
	client := mcp.NewClient(&mcp.Implementation{Name: "world-test-client", Version: "1"}, nil)
	cs, err := client.Connect(ctx, ct, nil)
	if err != nil {
		t.Fatal(err)
	}
	defer cs.Close()
	tools, err := cs.ListTools(ctx, nil)
	if err != nil {
		t.Fatal(err)
	}
	if len(tools.Tools) != 6 {
		t.Fatal("unexpected registered world tools", len(tools.Tools))
	}
	for _, tool := range tools.Tools {
		if !strings.HasPrefix(tool.Name, "world_") || tool.Annotations == nil || !tool.Annotations.ReadOnlyHint {
			t.Fatal("unexpected mutating world tool", tool.Name)
		}
	}
}

func worldFloat(value float64) *float64 { return &value }
func worldInt(value int) *int           { return &value }
