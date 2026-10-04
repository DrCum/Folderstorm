/**
 * @file fssettingssearch.h
 * @brief Scoped aliases for real Settings controls
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
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
#ifndef FS_SETTINGS_SEARCH_H
#define FS_SETTINGS_SEARCH_H

#include <string>
#include <vector>

namespace fs_settings_search
{
inline std::vector<std::string> aliases(const std::string& scope, const std::string& control)
{
    if (scope == "local_assistant" || scope == "local_assistant_panel")
    {
        if (control == "copy_configuration" || control == "setup_guide")
            return {"mcp", "model context protocol", "assistant setup"};
        if (control == "check_connection")
            return {"mcp connection", "model context protocol connection"};
        if (control == "history_and_undo")
            return {"undo", "inventory history", "assistant history"};
    }
    if (scope == "viewport_editor")
    {
        if (control == "world_view_enabled" || control == "world_view_monitor_combo" ||
            control == "world_view_use_monitor")
            return {"dual monitor", "dual-monitor", "multiple monitors", "multi monitor", "second monitor"};
    }
    if (scope == "workspace_editor")
    {
        if (control == "workspace_combo" || control == "workspace_preview" || control == "workspace_save" ||
            control == "workspace_rename" || control == "workspace_delete" ||
            control == "workspace_inventory_sorting" || control == "workspace_driving")
            return {"workspace", "window arrangement"};
    }
    return {};
}
}

#endif
