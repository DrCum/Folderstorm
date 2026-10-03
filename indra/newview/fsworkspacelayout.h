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
#include <map>
#include <string>

class LLSD;

namespace FSWorkspaceLayout
{
constexpr int SCHEMA_VERSION = 1;
constexpr int MAX_PROFILES = 32;
constexpr int MAX_ROLES = 6;
constexpr float MAX_UI_SIZE = 16384.f;

enum class Role
{
    InventoryPrimary, MiniMap, WorldMap, NearbyChat,
    ConversationsGeometry, InventoryExtra1
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
};

struct Workspace
{
    FSChromeLayout::Snapshot chrome;
    bool world_view_in_mouselook = true;
    float frame_width = 1.f;
    float frame_height = 1.f;
    std::map<Role, Window> windows;
    bool has_inbox = false;
    bool inbox_expanded = false;
    float inbox_height = 200.f;
    int ignored_details = 0;
};

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
Window capture(const Rect& rect, const Rect& frame, bool visible, bool minimized);
} // namespace FSWorkspaceLayout

#endif
