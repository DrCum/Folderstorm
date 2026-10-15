/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

#ifndef FS_ASSISTANT_PERMISSIONS_H
#define FS_ASSISTANT_PERMISSIONS_H

#include <array>
#include <string>

namespace fs_assistant
{
enum class ActionClass { Unknown, Read, Camera, Create, Edit, Move, Trash, NoCopy, Wear, Links, Workspace, Permanent, ChatRead, ChatState, ChatSendIM, ChatSendGroup, ChatSendConference, ChatSendNearby, FriendsRead, ResidentSearch, ProfileRead, NotificationsRead, NotificationReview, WorldRead, GestureRead, GesturePlay, PoseRead, PoseEdit, PoseShare, ContentEdit, PreferencesRead, PreferencesWrite };
struct PermissionClass
{
    ActionClass kind;
    const char* id;
    const char* title;
    const char* fallback;
    bool ask;
    const char* widget;
    bool privateRead = false;
};
inline constexpr std::array<PermissionClass, 30> permissionClasses {{
    {ActionClass::Read, "read", "Read inventory and outfits", "allow", false, "local_assistant_perm_read"},
    {ActionClass::Camera, "camera", "Move camera and take pictures", "allow", true, "local_assistant_perm_camera"},
    {ActionClass::Create, "create", "Create folders and items", "allow", true, "local_assistant_perm_create"},
    {ActionClass::Edit, "edit", "Rename and edit details", "allow", true, "local_assistant_perm_edit"},
    {ActionClass::Move, "move", "Move and copy", "allow", true, "local_assistant_perm_move"},
    {ActionClass::Trash, "trash", "Trash (can be restored)", "allow", true, "local_assistant_perm_trash"},
    {ActionClass::NoCopy, "nocopy", "Move no-copy items during a copy", "ask", true, "local_assistant_perm_nocopy"},
    {ActionClass::Wear, "wear", "Wear and detach", "ask", true, "local_assistant_perm_wear"},
    {ActionClass::Workspace, "workspace", "Switch workspaces and layouts", "ask", true, "local_assistant_perm_workspace"},
    {ActionClass::Links, "links", "Replace links", "ask", true, "local_assistant_perm_links"},
    {ActionClass::ChatRead, "chat_read", "Read Chat", "deny", false, "local_assistant_perm_chat_read", true},
    {ActionClass::ChatState, "chat_state", "Manage Chat drafts and read state", "ask", true, "local_assistant_perm_chat_state", false},
    {ActionClass::ChatSendIM, "chat_send_im", "Send direct IMs", "ask", true, "local_assistant_perm_chat_send_im", false},
    {ActionClass::ChatSendGroup, "chat_send_group", "Send group Chat", "ask", true, "local_assistant_perm_chat_send_group", false},
    {ActionClass::ChatSendConference, "chat_send_conference", "Send conference Chat", "ask", true, "local_assistant_perm_chat_send_conference", false},
    {ActionClass::ChatSendNearby, "chat_send_nearby", "Speak in Nearby Chat", "ask", true, "local_assistant_perm_chat_send_nearby", false},
    {ActionClass::FriendsRead, "friends_read", "Read friends and online status", "deny", false, "local_assistant_perm_friends_read", true},
    {ActionClass::ResidentSearch, "resident_search", "Search residents", "deny", false, "local_assistant_perm_resident_search", true},
    {ActionClass::ProfileRead, "profile_read", "Read resident profiles", "deny", false, "local_assistant_perm_profile_read", true},
    {ActionClass::NotificationsRead, "notifications_read", "Read viewer notifications", "deny", false, "local_assistant_perm_notifications_read", true},
    {ActionClass::NotificationReview, "notification_review", "Review native notifications", "ask", true, "local_assistant_perm_notification_review", false},
    {ActionClass::WorldRead, "world_read", "Read nearby world and media observations", "deny", false, "local_assistant_perm_world_read", true},
    {ActionClass::GestureRead, "gesture_read", "Read gestures and playback state", "deny", false, "local_assistant_perm_gesture_read", true},
    {ActionClass::GesturePlay, "gesture_play", "Play and stop gestures", "ask", true, "local_assistant_perm_gesture_play", false},
    {ActionClass::PoseRead, "pose_read", "Read poses and joint state", "deny", false, "local_assistant_perm_pose_read", true},
    {ActionClass::PoseEdit, "pose_edit", "Edit your own pose", "ask", true, "local_assistant_perm_pose_edit", false},
    {ActionClass::PoseShare, "pose_share", "Use owner-approved shared posing", "ask", true, "local_assistant_perm_pose_share", false},
    {ActionClass::ContentEdit, "content_edit", "Edit notecard and script contents", "ask", true, "local_assistant_perm_content_edit", false},
    {ActionClass::PreferencesRead, "preferences_read", "Read curated viewer preferences", "deny", false, "local_assistant_perm_preferences_read", true},
    {ActionClass::PreferencesWrite, "preferences_write", "Change curated viewer preferences", "ask", true, "local_assistant_perm_preferences_write", false},
}};
inline bool permissionLevelAllowed(const PermissionClass& def, const std::string& level)
{
    return level == "allow" || level == "deny" || (def.ask && level == "ask");
}
inline std::string effectivePermissionLevel(const PermissionClass& def, const std::string& raw)
{
    return permissionLevelAllowed(def, raw) ? raw : def.fallback;
}
enum class PermissionPreset { ReadOnly, AskBeforeChanges, Custom };
inline const char* presetLevel(PermissionPreset preset, const PermissionClass& def)
{
    if (preset == PermissionPreset::Custom) return nullptr;
    if (def.privateRead) return "deny"; // Existing presets never opt into new private data.
    return def.kind == ActionClass::Read ? "allow" : preset == PermissionPreset::ReadOnly ? "deny" : "ask";
}
// The lookup and write adapters preserve the viewer's LLSD types and unknown keys.
template<class Lookup> PermissionPreset permissionPreset(Lookup lookup)
{
    bool read_only = true, ask = true;
    for (const auto& def : permissionClasses)
    {
        const auto level = effectivePermissionLevel(def, lookup(def.id));
        read_only = read_only && level == presetLevel(PermissionPreset::ReadOnly, def);
        ask = ask && level == presetLevel(PermissionPreset::AskBeforeChanges, def);
    }
    return read_only ? PermissionPreset::ReadOnly : ask ? PermissionPreset::AskBeforeChanges : PermissionPreset::Custom;
}
template<class Write> void applyPermissionPreset(PermissionPreset preset, Write write)
{
    if (preset == PermissionPreset::Custom) return;
    for (const auto& def : permissionClasses) write(def.id, presetLevel(preset, def));
}
}
#endif
