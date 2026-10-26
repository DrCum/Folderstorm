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
	ClassRead                 = "read"
	ClassCamera               = "camera"
	ClassCreate               = "create"
	ClassEdit                 = "edit"
	ClassMove                 = "move"
	ClassTrash                = "trash"
	ClassNoCopy               = "nocopy"
	ClassWear                 = "wear"
	ClassLinks                = "links"
	ClassWorkspace            = "workspace"
	ClassChatRead             = "chat_read"
	ClassChatState            = "chat_state"
	ClassChatSendIM           = "chat_send_im"
	ClassChatSendGroup        = "chat_send_group"
	ClassChatSendConference   = "chat_send_conference"
	ClassChatSendNearby       = "chat_send_nearby"
	ClassFriendsRead          = "friends_read"
	ClassResidentSearch       = "resident_search"
	ClassProfileRead          = "profile_read"
	ClassNotificationsRead    = "notifications_read"
	ClassNotificationReview   = "notification_review"
	ClassWorldRead            = "world_read"
	ClassGestureRead          = "gesture_read"
	ClassGesturePlay          = "gesture_play"
	ClassPoseRead             = "pose_read"
	ClassPoseEdit             = "pose_edit"
	ClassPoseSave             = "pose_save"
	ClassPoseShare            = "pose_share"
	ClassContentEdit          = "content_edit"
	ClassPreferencesRead      = "preferences_read"
	ClassPreferencesWrite     = "preferences_write"
	ClassBuildEdit            = "build_edit"
	ClassBuildRez             = "build_rez"
	ClassInventoryPermissions = "inventory_permissions"
	ClassNavigationMove       = "navigation_move"
	ClassNavigationTeleport   = "navigation_teleport"
	ClassTaskInventoryRead    = "task_inventory_read"
	ClassScriptDebugRead      = "script_debug_read"
	ClassTaskInventoryWrite   = "task_inventory_write"
	ClassTaskInventoryRemove  = "task_inventory_remove"
	ClassScriptRun            = "script_run"
	ClassObjectTake           = "object_take"
)

// StatusPolicy is the viewer's effective permission map.
// Present is false when the status payload has no permissions object,
// which is an older viewer.
type StatusPolicy struct {
	Capabilities        map[string]int
	Present             bool
	Generation          int
	Levels              map[string]string
	BulkInventoryReview int
	SocialChat          int
	SocialContacts      int
	SocialEvents        int
	WorldObservation    int
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
	{ClassChatRead, "deny", false},
	{ClassChatState, "ask", true},
	{ClassChatSendIM, "ask", true},
	{ClassChatSendGroup, "ask", true},
	{ClassChatSendConference, "ask", true},
	{ClassChatSendNearby, "ask", true},
	{ClassFriendsRead, "deny", false},
	{ClassResidentSearch, "deny", false},
	{ClassProfileRead, "deny", false},
	{ClassNotificationsRead, "deny", false},
	{ClassNotificationReview, "ask", true},
	{ClassWorldRead, "deny", false},
	{ClassGestureRead, "deny", false},
	{ClassGesturePlay, "ask", true},
	{ClassPoseRead, "deny", false},
	{ClassPoseEdit, "ask", true},
	{ClassPoseSave, "ask", true},
	{ClassPoseShare, "ask", true},
	{ClassContentEdit, "ask", true},
	{ClassPreferencesRead, "deny", false},
	{ClassPreferencesWrite, "ask", true},
	{ClassBuildEdit, "ask", true},
	{ClassBuildRez, "ask", true},
	{ClassInventoryPermissions, "ask", true},
	{ClassNavigationMove, "ask", true},
	{ClassNavigationTeleport, "ask", true},
	{ClassTaskInventoryRead, "deny", false},
	{ClassScriptDebugRead, "deny", false},
	{ClassTaskInventoryWrite, "ask", true},
	{ClassTaskInventoryRemove, "ask", true},
	{ClassScriptRun, "ask", true},
	{ClassObjectTake, "ask", true},
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
	featureVersions := map[string]int{}
	socialChat, socialContacts, socialEvents, world := 0, 0, 0, 0
	bulkVersion := 0
	workspaceVersion := 0
	if capabilities, ok := payload["capabilities"].(map[string]any); ok {
		for _, key := range []string{"social_chat", "social_contacts", "social_events", "world_observation", "gesture_tools", "pose_tools", "shared_pose_tools", "content_editing", "preferences_tools", "building_tools", "navigation_tools", "task_inventory", "script_diagnostics", "task_scripts", "object_lifecycle"} {
			if version, ok := capabilities[key].(float64); ok && version == 1 {
				featureVersions[key] = 1
			}
		}
		if v, ok := capabilities["social_chat"].(float64); ok && v == 1 {
			socialChat = 1
		}
		if v, ok := capabilities["social_contacts"].(float64); ok && v == 1 {
			socialContacts = 1
		}
		if v, ok := capabilities["social_events"].(float64); ok && v == 1 {
			socialEvents = 1
		}
		if v, ok := capabilities["world_observation"].(float64); ok && v == 1 {
			world = 1
		}
		if version, ok := capabilities["workspace_tools"].(float64); ok && version == WorkspaceToolsVersion {
			workspaceVersion = WorkspaceToolsVersion
		}
		if version, ok := capabilities["bulk_inventory_review"].(float64); ok && version == BulkInventoryReviewVersion {
			bulkVersion = BulkInventoryReviewVersion
		}
	}
	perms, ok := payload["permissions"]
	if !ok {
		return StatusPolicy{Capabilities: featureVersions, Levels: map[string]string{}, BulkInventoryReview: bulkVersion, WorkspaceTools: workspaceVersion, SocialChat: socialChat, SocialContacts: socialContacts, SocialEvents: socialEvents, WorldObservation: world}, true
	}
	obj, ok := perms.(map[string]any)
	if !ok {
		return StatusPolicy{Capabilities: featureVersions, Levels: map[string]string{}, BulkInventoryReview: bulkVersion, WorkspaceTools: workspaceVersion, SocialChat: socialChat, SocialContacts: socialContacts, SocialEvents: socialEvents, WorldObservation: world}, true
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
		Capabilities:        featureVersions,
		Present:             true,
		Generation:          generationFrom(payload["policy_generation"]),
		Levels:              levels,
		BulkInventoryReview: bulkVersion,
		WorkspaceTools:      workspaceVersion,
		SocialChat:          socialChat, SocialContacts: socialContacts, SocialEvents: socialEvents, WorldObservation: world,
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
