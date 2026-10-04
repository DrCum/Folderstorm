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
#include "llviewerprecompiledheaders.h"
#include "fsworkspacequickaccess.h"
#include "fsworkspacecontroller.h"
#include "fschromelayoutcontroller.h"
#include "llagent.h"
#include "llviewercontrol.h"
#include <algorithm>

std::vector<FSWorkspaceQuickAccess::Entry> FSWorkspaceQuickAccess::entries()
{
    std::vector<Entry> result;
    if (!FSWorkspaceController::instance().available()) return result;
    const LLSD profiles = gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    if (profiles.isMap())
        for (auto it = profiles.beginMap(); it != profiles.endMap() &&
             result.size() < static_cast<size_t>(FSWorkspaceLayout::MAX_PROFILES); ++it)
            if (FSWorkspaceLayout::isSafeProfileName(it->first))
                result.push_back({"workspace:" + it->first, it->first, false});
    for (const auto& id : FSChromeLayoutController::instance().profileNames())
        result.push_back({"layout:" + id,
            FSChromeLayout::isBuiltinProfileId(id) ? FSChromeLayout::builtinProfileLabel(id) : id, true});
    return result;
}
std::set<std::string> FSWorkspaceQuickAccess::favorites(const std::vector<Entry>& entries)
{
    std::set<std::string> valid, result;
    for (const auto& entry : entries) valid.insert(entry.key);
    const LLSD data = gSavedPerAccountSettings.getLLSD(FAVORITES_SETTING);
    if (data.isArray())
    {
        const S32 count = static_cast<S32>(std::min<size_t>(data.size(), 128));
        for (S32 i = 0; i < count; ++i)
            if (data[i].isString() && valid.count(data[i].asString())) result.insert(data[i].asString());
    }
    return result;
}
bool FSWorkspaceQuickAccess::apply(const std::string& key, const LLUUID& account, const LLUUID& session)
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.canQuickSwitch() || account != gAgent.getID() || session != gAgent.getSessionID()) return false;
    // Recheck the target at click time, including clicks from an older menu.
    for (const auto& entry : entries())
        if (entry.key == key)
            return entry.layout ? controller.quickSwitchLayout(key.substr(7)) : controller.quickSwitchWorkspace(key.substr(10));
    return false;
}
