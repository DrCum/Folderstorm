package viewerapi

import "encoding/json"

// BulkInventoryReviewVersion is the additive preview/history protocol this
// sidecar understands. Unknown versions must not fall back to direct mutation.
const BulkInventoryReviewVersion = 1
const WorkspaceToolsVersion = 1

// Preference classes published on inventory status. Permanent delete is not
// one of them: purge and emptyTrash are denied by the viewer with no key
// that can enable them.
const (
	ClassRead      = "read"
	ClassCamera    = "camera"
	ClassCreate    = "create"
	ClassEdit      = "edit"
	ClassMove      = "move"
	ClassTrash     = "trash"
	ClassNoCopy    = "nocopy"
	ClassWear      = "wear"
	ClassLinks     = "links"
	ClassWorkspace = "workspace"
)

// StatusPolicy is the viewer's effective permission map.
// Present is false when the status payload has no permissions object,
// which is an older viewer.
type StatusPolicy struct {
	Present             bool
	Generation          int
	Levels              map[string]string
	BulkInventoryReview int
	WorkspaceTools      int
}

type classDefault struct {
	id       string
	fallback string
	ask      bool
}

var classDefaults = []classDefault{
	{ClassRead, "allow", false},
	{ClassCamera, "allow", true},
	{ClassCreate, "allow", true},
	{ClassEdit, "allow", true},
	{ClassMove, "allow", true},
	{ClassTrash, "allow", true},
	{ClassNoCopy, "ask", true},
	{ClassWear, "ask", true},
	{ClassLinks, "ask", true},
	{ClassWorkspace, "ask", true},
}

// Level returns the effective level for a preference class.
// Missing or invalid values use the same defaults as the viewer.
func (p StatusPolicy) Level(class string) string {
	fallback := "deny"
	ask := true
	for _, def := range classDefaults {
		if def.id == class {
			fallback = def.fallback
			ask = def.ask
			break
		}
	}
	if !p.Present {
		return fallback
	}
	raw := p.Levels[class]
	if raw == "allow" || raw == "deny" || (ask && raw == "ask") {
		return raw
	}
	return fallback
}

// ParseStatusPolicy reads permissions and policy_generation from an inventory
// status payload. The boolean is false only when the body is not a JSON object.
// A JSON object without permissions is an older viewer: Present is false.
func ParseStatusPolicy(raw []byte) (StatusPolicy, bool) {
	var payload map[string]any
	if err := json.Unmarshal(raw, &payload); err != nil || payload == nil {
		return StatusPolicy{}, false
	}
	bulkVersion := 0
	workspaceVersion := 0
	if capabilities, ok := payload["capabilities"].(map[string]any); ok {
		if version, ok := capabilities["workspace_tools"].(float64); ok && version == WorkspaceToolsVersion {
			workspaceVersion = WorkspaceToolsVersion
		}
		if version, ok := capabilities["bulk_inventory_review"].(float64); ok && version == BulkInventoryReviewVersion {
			bulkVersion = BulkInventoryReviewVersion
		}
	}
	perms, ok := payload["permissions"]
	if !ok {
		return StatusPolicy{Levels: map[string]string{}, BulkInventoryReview: bulkVersion, WorkspaceTools: workspaceVersion}, true
	}
	obj, ok := perms.(map[string]any)
	if !ok {
		return StatusPolicy{Levels: map[string]string{}, BulkInventoryReview: bulkVersion, WorkspaceTools: workspaceVersion}, true
	}
	levels := map[string]string{}
	for _, def := range classDefaults {
		if value, exists := obj[def.id]; exists {
			if text, isString := value.(string); isString {
				levels[def.id] = text
			}
		}
	}
	return StatusPolicy{
		Present:             true,
		Generation:          generationFrom(payload["policy_generation"]),
		Levels:              levels,
		BulkInventoryReview: bulkVersion,
		WorkspaceTools:      workspaceVersion,
	}, true
}

func generationFrom(value any) int {
	switch typed := value.(type) {
	case float64:
		return int(typed)
	case int:
		return typed
	case json.Number:
		n, _ := typed.Int64()
		return int(n)
	default:
		return 0
	}
}
