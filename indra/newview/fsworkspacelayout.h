/**
 * @file fsworkspacelayout.h
 * @brief Bounded, side-effect-free workspace records and placement helpers
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
#ifndef FSWORKSPACELAYOUT_H
#define FSWORKSPACELAYOUT_H

#include "fschromelayout.h"
#include "fsworkspacecontext.h"
#include <map>
#include <string>
#include <vector>

class LLSD;

namespace FSWorkspaceLayout
{
constexpr int SCHEMA_VERSION = 2;
constexpr int MAX_PROFILES = 32;
constexpr int MAX_ROLES = 6;
constexpr int MAX_EXTRA_INVENTORY_WINDOWS = 16;
constexpr int MAX_EXPANDED_INVENTORY_FOLDERS = 256;
constexpr float MAX_UI_SIZE = 16384.f;

enum class Role
{
    InventoryPrimary, MiniMap, WorldMap, NearbyChat,
    ConversationsGeometry, InventoryExtra1
};

struct InventoryFolder
{
    bool present = false;
    bool single_folder = false;
    int view_mode = 0; // List, gallery or combination, matching Inventory's modes.
    std::string folder_id; // Optional selected folder for the normal view.
    bool has_expanded_folders = false; // Absent in older saves: leave expansion alone.
    std::vector<std::string> expanded_folders{}; // Folder references only, never items.
};

struct Window
{
    bool visible = false;
    bool minimized = false;
    bool has_geometry = false;
    float center_x = .5f;
    float center_y = .5f;
    float width_ui = 0.f;
    float height_ui = 0.f;
    InventoryFolder inventory_folder;
};

enum Component { Chrome = 1, Inventory = 2, Maps = 4, Chat = 8, All = 15, Graphics = 16, Camera = 32, HUDs = 64, AllComponents = 127 };
int componentForRole(Role role);

struct Toolbar
{
    int location = 1; // Registered toolbar locations: left, right, bottom.
    int display_mode = 0;
    std::vector<std::string> commands;
};
constexpr int MAX_TOOLBAR_COMMANDS = 64;

struct Workspace
{
    FSWorkspaceContext::Options context;
    bool remember_toolbars = false;
    std::vector<Toolbar> toolbars;
    int components = All;
    FSChromeLayout::Snapshot chrome;
    bool world_view_in_mouselook = true;
    bool remember_inventory_folders = false;
    float frame_width = 1.f;
    float frame_height = 1.f;
    std::map<Role, Window> windows;
    // Standard additional Inventory windows, separate from the owned legacy role.
    std::vector<Window> extra_inventory;
    bool has_inbox = false;
    bool inbox_expanded = false;
    float inbox_height = 200.f;
    int ignored_details = 0;
};

// Select a transient subset of a validated save. The source and output on
// failure are unchanged; absent groups never acquire default values.
bool selectGroups(const Workspace& source, int components, bool toolbars, Workspace& result);

struct Rect
{
    float left = 0.f;
    float bottom = 0.f;
    float right = 0.f;
    float top = 0.f;
    float width() const { return right - left; }
    float height() const { return top - bottom; }
};

struct Placement
{
    Rect rect;
    bool adjusted = false;
};

const char* roleId(Role role);
bool parseRole(const std::string& id, Role& role);
bool isBuiltinProfileId(const std::string& id);
bool isSafeProfileName(const std::string& name);
// Replacement is permitted at the limit; malformed collections fail rather
// than silently erasing unsupported or corrupt saved definitions.
bool canSaveProfile(const LLSD& profiles, const std::string& name, std::string& error);

// fromLLSD does not coerce field types, default missing required data, or mutate
// output on failure. Unknown extensions are counted in ignored_details.
LLSD toLLSD(const Workspace& workspace);
bool fromLLSD(const LLSD& data, Workspace& workspace, std::string& error);

// Geometry uses bottom-left UI logical units, never native display pixels.
// An oversized minimum retains its size with the title/close edge in frame.
Placement fit(const Window& window, const Rect& frame, float min_width, float min_height);
Placement fitWithNeighbors(const Window& window, const Rect& frame, float min_width, float min_height, const std::vector<Rect>& placed);
Window capture(const Rect& rect, const Rect& frame, bool visible, bool minimized);
} // namespace FSWorkspaceLayout

#endif
