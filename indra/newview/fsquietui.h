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
#ifndef FS_QUIET_UI_H
#define FS_QUIET_UI_H
#include <string>
namespace FSQuietUI
{
bool active();
void setEnabled(bool enabled);
void refresh();
// A presentation policy, never an automatic answer to an interactive request.
inline bool quietRoutineNotification(bool quiet, bool forced, bool interactive, bool high_priority, const std::string& name)
{
    return quiet && !forced && !interactive && !high_priority && name != "SystemMessage" && name != "GodMessage";
}
}
#endif
