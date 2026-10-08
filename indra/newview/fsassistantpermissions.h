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
enum class ActionClass { Unknown, Read, Camera, Create, Edit, Move, Trash, NoCopy, Wear, Links, Workspace, Permanent };
struct PermissionClass
{
    ActionClass kind;
    const char* id;
    const char* title;
    const char* fallback;
    bool ask;
    const char* widget;
};
inline constexpr std::array<PermissionClass, 10> permissionClasses {{
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
