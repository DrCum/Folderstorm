/**
 * @file fsworkspaceserialization.cpp
 * @brief Strict workspace schema validation without viewer runtime dependencies
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (c) 2026 The Phoenix Firestorm Project, Inc.
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
#include "fsworkspacelayout.h"
#include "llsd.h"
#include "lluuid.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace FSWorkspaceLayout
{
namespace
{
bool fail(std::string& error, const std::string& field)
{
    error = "Invalid workspace field: " + field;
    return false;
}

bool number(const LLSD& map, const char* key, float& result,
            float lower, float upper, bool positive = false)
{
    const LLSD& value = map[key];
    if (!value.isReal() && !value.isInteger()) return false;
    const double raw = value.asReal();
    if (!std::isfinite(raw) || raw < lower || raw > upper || (positive && raw <= 0.)) return false;
    result = static_cast<float>(raw);
    return std::isfinite(result);
}

bool integer(const LLSD& map, const char* key, int& result, int lower, int upper)
{
    if (!map[key].isInteger()) return false;
    result = map[key].asInteger();
    return result >= lower && result <= upper;
}

bool boolean(const LLSD& map, const char* key, bool& result)
{
    if (!map[key].isBoolean()) return false;
    result = map[key].asBoolean();
    return true;
}

void countUnknown(const LLSD& map, std::initializer_list<const char*> known, int& ignored)
{
    for (auto it = map.beginMap(); it != map.endMap(); ++it)
    {
        if (std::none_of(known.begin(), known.end(), [&](const char* key) { return it->first == key; }))
            ++ignored;
    }
}

LLSD spanToLLSD(const FSChromeLayout::SpanRequest& span)
{
    LLSD data = LLSD::emptyMap();
    data["region"] = static_cast<int>(span.region);
    data["start"] = span.custom_start_percent;
    data["end"] = span.custom_end_percent;
    data["margin_left"] = span.margin_left;
    data["margin_right"] = span.margin_right;
    return data;
}

bool spanFromLLSD(const LLSD& data, FSChromeLayout::SpanRequest& span, int& ignored)
{
    int region;
    if (!data.isMap() || !integer(data, "region", region, 0, FSChromeLayout::kRegionCount - 1) ||
        !number(data, "start", span.custom_start_percent, 0.f, 100.f) ||
        !number(data, "end", span.custom_end_percent, 0.f, 100.f) ||
        span.custom_start_percent > span.custom_end_percent ||
        !integer(data, "margin_left", span.margin_left, 0, static_cast<int>(MAX_UI_SIZE)) ||
        !integer(data, "margin_right", span.margin_right, 0, static_cast<int>(MAX_UI_SIZE))) return false;
    span.region = static_cast<FSChromeLayout::Region>(region);
    countUnknown(data, {"region", "start", "end", "margin_left", "margin_right"}, ignored);
    return true;
}

LLSD chromeToLLSD(const FSChromeLayout::Snapshot& chrome)
{
    LLSD data = LLSD::emptyMap();
    data["schema"] = chrome.schema_version;
    data["viewport_enabled"] = chrome.viewport_enabled;
    data["inset_left"] = chrome.inset_left;
    data["inset_right"] = chrome.inset_right;
    data["inset_top"] = chrome.inset_top;
    data["inset_bottom"] = chrome.inset_bottom;
    data["left_placement"] = static_cast<int>(chrome.left_placement);
    data["right_placement"] = static_cast<int>(chrome.right_placement);
    data["left_offset"] = chrome.left_offset;
    data["right_offset"] = chrome.right_offset;
    data["bottom_dock"] = spanToLLSD(chrome.bottom_dock);
    data["nav_favorites"] = spanToLLSD(chrome.nav_favorites);
    data["menu_status"] = spanToLLSD(chrome.menu_status);
    return data;
}

bool chromeFromLLSD(const LLSD& data, FSChromeLayout::Snapshot& chrome,
                    int& ignored, std::string& error)
{
    int left, right;
    if (!data.isMap() || !integer(data, "schema", chrome.schema_version, 1, 1) ||
        !boolean(data, "viewport_enabled", chrome.viewport_enabled) ||
        !number(data, "inset_left", chrome.inset_left, 0.f, 100.f) ||
        !number(data, "inset_right", chrome.inset_right, 0.f, 100.f) ||
        !number(data, "inset_top", chrome.inset_top, 0.f, 100.f) ||
        !number(data, "inset_bottom", chrome.inset_bottom, 0.f, 100.f) ||
        !integer(data, "left_placement", left, 0, FSChromeLayout::kPlacementCount - 1) ||
        !integer(data, "right_placement", right, 0, FSChromeLayout::kPlacementCount - 1) ||
        !integer(data, "left_offset", chrome.left_offset, 0, static_cast<int>(MAX_UI_SIZE)) ||
        !integer(data, "right_offset", chrome.right_offset, 0, static_cast<int>(MAX_UI_SIZE)))
        return fail(error, "chrome");
    chrome.left_placement = static_cast<FSChromeLayout::SidePlacement>(left);
    chrome.right_placement = static_cast<FSChromeLayout::SidePlacement>(right);
    if (!spanFromLLSD(data["bottom_dock"], chrome.bottom_dock, ignored)) return fail(error, "chrome.bottom_dock");
    if (!spanFromLLSD(data["nav_favorites"], chrome.nav_favorites, ignored)) return fail(error, "chrome.nav_favorites");
    if (!spanFromLLSD(data["menu_status"], chrome.menu_status, ignored)) return fail(error, "chrome.menu_status");
    countUnknown(data, {"schema", "viewport_enabled", "inset_left", "inset_right", "inset_top", "inset_bottom",
                       "left_placement", "right_placement", "left_offset", "right_offset",
                       "bottom_dock", "nav_favorites", "menu_status"}, ignored);
    return true;
}

LLSD windowToLLSD(Role role, const Window& window)
{
    LLSD data = LLSD::emptyMap();
    if (role != Role::ConversationsGeometry)
    {
        data["visible"] = window.visible;
        data["minimized"] = window.minimized;
    }
    if (window.has_geometry)
    {
        data["center_x"] = window.center_x;
        data["center_y"] = window.center_y;
        data["width_ui"] = window.width_ui;
        data["height_ui"] = window.height_ui;
    }
    if (window.inventory_folder.present)
    {
        data["inventory_folder"]["single_folder"] = window.inventory_folder.single_folder;
        data["inventory_folder"]["view_mode"] = window.inventory_folder.view_mode;
        if (!window.inventory_folder.folder_id.empty())
            data["inventory_folder"]["folder_id"] = LLUUID(window.inventory_folder.folder_id);
    }
    return data;
}

bool windowFromLLSD(const LLSD& data, Role role, Window& window, int& ignored)
{
    if (!data.isMap()) return false;
    if (role == Role::ConversationsGeometry)
    {
        if (data.has("visible") || data.has("minimized")) return false;
    }
    else
    {
        if (!boolean(data, "visible", window.visible)) return false;
        // A hidden never-created window has only visible:false. Minimized is
        // optional for compatibility, but must still be a boolean when present.
        if (data.has("minimized") && !boolean(data, "minimized", window.minimized)) return false;
    }
    const bool any_geometry = data.has("center_x") || data.has("center_y") ||
                              data.has("width_ui") || data.has("height_ui");
    if (any_geometry || window.visible || role == Role::ConversationsGeometry)
    {
        if (!number(data, "center_x", window.center_x, -MAX_UI_SIZE, MAX_UI_SIZE) ||
            !number(data, "center_y", window.center_y, -MAX_UI_SIZE, MAX_UI_SIZE) ||
            !number(data, "width_ui", window.width_ui, 0.f, MAX_UI_SIZE, true) ||
            !number(data, "height_ui", window.height_ui, 0.f, MAX_UI_SIZE, true)) return false;
        window.has_geometry = true;
    }
    else if (window.minimized) return false;
    if (data.has("inventory_folder"))
    {
        if (role != Role::InventoryPrimary && role != Role::InventoryExtra1) return false;
        const auto& folder = data["inventory_folder"];
        if (!folder.isMap() || !boolean(folder, "single_folder", window.inventory_folder.single_folder) ||
            !integer(folder, "view_mode", window.inventory_folder.view_mode, 0, 2)) return false;
        if (folder.has("folder_id"))
        {
            if (!folder["folder_id"].isUUID() || folder["folder_id"].asUUID().isNull()) return false;
            window.inventory_folder.folder_id = folder["folder_id"].asUUID().asString();
        }
        if (window.inventory_folder.single_folder && window.inventory_folder.folder_id.empty()) return false;
        window.inventory_folder.present = true;
        countUnknown(folder, {"single_folder", "view_mode", "folder_id"}, ignored);
    }
    countUnknown(data, {"visible", "minimized", "center_x", "center_y", "width_ui", "height_ui", "inventory_folder"}, ignored);
    return true;
}
} // namespace

LLSD toLLSD(const Workspace& workspace)
{
    LLSD data = LLSD::emptyMap();
    data["schema"] = SCHEMA_VERSION;
    data["type"] = "workspace";
    data["components"] = workspace.components;
    data["chrome"] = chromeToLLSD(workspace.chrome);
    data["world_view_in_mouselook"] = workspace.world_view_in_mouselook;
    data["remember_inventory_folders"] = workspace.remember_inventory_folders;
    data["frame"]["width_ui"] = workspace.frame_width;
    data["frame"]["height_ui"] = workspace.frame_height;
    data["windows"] = LLSD::emptyMap();
    for (const auto& entry : workspace.windows)
        data["windows"][roleId(entry.first)] = windowToLLSD(entry.first, entry.second);
    data["extra_inventory"] = LLSD::emptyArray();
    for (const auto& window : workspace.extra_inventory)
        data["extra_inventory"].append(windowToLLSD(Role::InventoryExtra1, window));
    data["panels"] = LLSD::emptyMap();
    if (workspace.has_inbox)
    {
        data["panels"]["inventory_primary_inbox"]["expanded"] = workspace.inbox_expanded;
        data["panels"]["inventory_primary_inbox"]["height_ui"] = workspace.inbox_height;
    }
    return data;
}

bool fromLLSD(const LLSD& data, Workspace& workspace, std::string& error)
{
    error.clear();
    Workspace parsed;
    int schema;
    if (!data.isMap()) return fail(error, "envelope");
    if (!integer(data, "schema", schema, SCHEMA_VERSION, SCHEMA_VERSION))
    {
        error = "Unsupported or missing workspace schema";
        return false;
    }
    if (!data["type"].isString() || data["type"].asString() != "workspace") return fail(error, "type");
    if (data.has("components") && !integer(data, "components", parsed.components, 1, All)) return fail(error, "components");
    if (!chromeFromLLSD(data["chrome"], parsed.chrome, parsed.ignored_details, error)) return false;
    if (!boolean(data, "world_view_in_mouselook", parsed.world_view_in_mouselook))
        return fail(error, "world_view_in_mouselook");
    if (data.has("remember_inventory_folders") &&
        !boolean(data, "remember_inventory_folders", parsed.remember_inventory_folders))
        return fail(error, "remember_inventory_folders");
    if (!data["frame"].isMap() ||
        !number(data["frame"], "width_ui", parsed.frame_width, 0.f, MAX_UI_SIZE, true) ||
        !number(data["frame"], "height_ui", parsed.frame_height, 0.f, MAX_UI_SIZE, true)) return fail(error, "frame");
    countUnknown(data["frame"], {"width_ui", "height_ui"}, parsed.ignored_details);
    const LLSD& windows = data["windows"];
    if (!windows.isMap() || windows.size() > MAX_ROLES) return fail(error, "windows");
    for (auto it = windows.beginMap(); it != windows.endMap(); ++it)
    {
        Role role;
        if (!parseRole(it->first, role))
        {
            ++parsed.ignored_details;
            continue;
        }
        if (!(parsed.components & componentForRole(role))) return fail(error, "excluded window component");
        Window window;
        if (!windowFromLLSD(it->second, role, window, parsed.ignored_details))
            return fail(error, "windows." + it->first);
        if (window.inventory_folder.present && !parsed.remember_inventory_folders)
            return fail(error, "remember_inventory_folders");
        parsed.windows.emplace(role, window);
    }
    if (data.has("extra_inventory"))
    {
        const LLSD& extras = data["extra_inventory"];
        if (!extras.isArray() || extras.size() > static_cast<size_t>(MAX_EXTRA_INVENTORY_WINDOWS))
            return fail(error, "extra_inventory");
        if (!(parsed.components & Inventory) && extras.size()) return fail(error, "excluded Inventory");
        for (auto it = extras.beginArray(); it != extras.endArray(); ++it)
        {
            Window window;
            if (!windowFromLLSD(*it, Role::InventoryExtra1, window, parsed.ignored_details))
                return fail(error, "extra_inventory");
            if (window.inventory_folder.present && !parsed.remember_inventory_folders)
                return fail(error, "remember_inventory_folders");
            parsed.extra_inventory.push_back(window);
        }
    }
    if (!data["panels"].isMap()) return fail(error, "panels");
    const LLSD& panels = data["panels"];
    countUnknown(panels, {"inventory_primary_inbox"}, parsed.ignored_details);
    if (panels.has("inventory_primary_inbox"))
    {
        if (!(parsed.components & Inventory)) return fail(error, "excluded Inbox");
        const LLSD& inbox = panels["inventory_primary_inbox"];
        if (!inbox.isMap() || !boolean(inbox, "expanded", parsed.inbox_expanded) ||
            !number(inbox, "height_ui", parsed.inbox_height, 0.f, MAX_UI_SIZE, true))
            return fail(error, "panels.inventory_primary_inbox");
        parsed.has_inbox = true;
        countUnknown(inbox, {"expanded", "height_ui"}, parsed.ignored_details);
    }
    if (parsed.remember_inventory_folders && !(parsed.components & Inventory)) return fail(error, "excluded folders");
    countUnknown(data, {"schema", "type", "components", "chrome", "world_view_in_mouselook", "remember_inventory_folders", "frame", "windows", "extra_inventory", "panels"},
                 parsed.ignored_details);
    workspace = parsed;
    return true;
}

bool canSaveProfile(const LLSD& profiles, const std::string& name, std::string& error)
{
    error.clear();
    if (!isSafeProfileName(name))
    {
        error = "Use a workspace name of 1 to 64 letters, numbers, spaces, hyphens, underscores or periods";
        return false;
    }
    if (!profiles.isMap() && !profiles.isUndefined())
    {
        error = "Invalid workspace collection";
        return false;
    }
    if (profiles.isMap() && (profiles.size() > MAX_PROFILES ||
                            (profiles.size() == MAX_PROFILES && !profiles.has(name))))
    {
        error = "At most 32 workspaces can be saved";
        return false;
    }
    // Unsupported existing definitions require a different name; preserve them.
    if (profiles.has(name))
    {
        Workspace existing;
        if (!fromLLSD(profiles[name], existing, error)) return false;
    }
    return true;
}
} // namespace FSWorkspaceLayout
