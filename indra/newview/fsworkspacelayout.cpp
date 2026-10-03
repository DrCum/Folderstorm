/**
 * @file fsworkspacelayout.cpp
 * @brief Pure workspace role, naming and placement helpers
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

#include <algorithm>
#include <cmath>

namespace FSWorkspaceLayout
{
namespace
{
const char* const ROLE_IDS[] = {
    "inventory_primary", "mini_map", "world_map", "nearby_chat",
    "conversations_geometry", "inventory_extra_1"
};

bool validRect(const Rect& rect)
{
    return std::isfinite(rect.left) && std::isfinite(rect.bottom) &&
           std::isfinite(rect.right) && std::isfinite(rect.top) &&
           std::isfinite(rect.width()) && std::isfinite(rect.height()) &&
           rect.width() > 0.f && rect.height() > 0.f;
}

float saneMinimum(float value)
{
    return std::isfinite(value) ? std::clamp(value, 1.f, MAX_UI_SIZE) : 1.f;
}
} // namespace

const char* roleId(Role role)
{
    const auto index = static_cast<unsigned>(role);
    return index < MAX_ROLES ? ROLE_IDS[index] : "";
}

bool parseRole(const std::string& id, Role& role)
{
    for (int index = 0; index < MAX_ROLES; ++index)
    {
        if (id == ROLE_IDS[index])
        {
            role = static_cast<Role>(index);
            return true;
        }
    }
    return false;
}

bool isBuiltinProfileId(const std::string& id)
{
    // Reserve the namespace as well as known IDs for future builtin templates.
    return id.compare(0, 8, "builtin:") == 0;
}

bool isSafeProfileName(const std::string& name)
{
    if (name.empty() || name.size() > 64 || isBuiltinProfileId(name)) return false;
    bool has_label = false;
    for (unsigned char ch : name)
    {
        // Same characters as layout profiles, independent of process locale.
        const bool alpha_numeric = (ch >= 'a' && ch <= 'z') ||
                                   (ch >= 'A' && ch <= 'Z') ||
                                   (ch >= '0' && ch <= '9');
        if (!(alpha_numeric || ch == ' ' || ch == '-' || ch == '_' || ch == '.')) return false;
        has_label = has_label || ch != ' ';
    }
    return has_label;
}

Window capture(const Rect& rect, const Rect& frame, bool visible, bool minimized)
{
    Window window;
    window.visible = visible;
    window.minimized = minimized;
    if (validRect(rect) && validRect(frame) &&
        rect.width() <= MAX_UI_SIZE && rect.height() <= MAX_UI_SIZE)
    {
        window.center_x = ((rect.left - frame.left) + rect.width() * .5f) / frame.width();
        window.center_y = ((rect.bottom - frame.bottom) + rect.height() * .5f) / frame.height();
        window.width_ui = rect.width();
        window.height_ui = rect.height();
        window.has_geometry = std::isfinite(window.center_x) && std::isfinite(window.center_y);
    }
    return window;
}

Placement fit(const Window& window, const Rect& frame, float min_width, float min_height)
{
    Placement placement;
    if (!validRect(frame) || !window.has_geometry ||
        !std::isfinite(window.center_x) || !std::isfinite(window.center_y) ||
        !std::isfinite(window.width_ui) || !std::isfinite(window.height_ui) ||
        window.width_ui <= 0.f || window.height_ui <= 0.f)
    {
        placement.adjusted = true;
        return placement;
    }
    const float minimum_width = saneMinimum(min_width);
    const float minimum_height = saneMinimum(min_height);
    const float width = std::clamp(window.width_ui, minimum_width,
                                  std::max(minimum_width, frame.width()));
    const float height = std::clamp(window.height_ui, minimum_height,
                                   std::max(minimum_height, frame.height()));
    // Clamp before multiplying to avoid overflow even for hostile coordinates.
    const float cx = std::clamp(window.center_x, 0.f, 1.f);
    const float cy = std::clamp(window.center_y, 0.f, 1.f);
    // Preserve the right-hand title/close controls when minimum width is larger.
    const float left = width > frame.width() ? frame.right - width :
        std::clamp(frame.left + cx * frame.width() - width * .5f,
                                  frame.left, std::max(frame.left, frame.right - width));
    // If minimum height exceeds the frame, pin the top edge, not the bottom.
    const float bottom = height > frame.height() ? frame.top - height :
        std::clamp(frame.bottom + cy * frame.height() - height * .5f,
                   frame.bottom, frame.top - height);
    placement.rect = {left, bottom, left + width, bottom + height};
    placement.adjusted = width != window.width_ui || height != window.height_ui ||
        std::abs(((left - frame.left) + width * .5f) / frame.width() - window.center_x) > .000001f ||
        std::abs(((bottom - frame.bottom) + height * .5f) / frame.height() - window.center_y) > .000001f;
    return placement;
}
} // namespace FSWorkspaceLayout
