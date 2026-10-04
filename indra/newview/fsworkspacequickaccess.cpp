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
#include "fsworkspacefavorites.h"
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
    std::set<std::string> result;
    for (const auto& entry : orderedFavorites(entries)) result.insert(entry.key);
    return result;
}
std::vector<FSWorkspaceQuickAccess::Entry> FSWorkspaceQuickAccess::orderedFavorites(const std::vector<Entry>& entries)
{
    std::vector<std::string> valid, saved;
    for (const auto& entry : entries) valid.push_back(entry.key);
    const LLSD data = gSavedPerAccountSettings.getLLSD(FAVORITES_SETTING);
    if (data.isArray())
    {
        const S32 count = static_cast<S32>(std::min<size_t>(data.size(), 128));
        for (S32 i = 0; i < count; ++i)
            if (data[i].isString()) saved.push_back(data[i].asString());
    }
    std::vector<Entry> result;
    for (const auto& key : orderedFavoriteKeys(saved, valid))
        for (const auto& entry : entries) if (entry.key == key) { result.push_back(entry); break; }
    return result;
}
bool FSWorkspaceQuickAccess::toggleFavorite(const std::string& key, const LLUUID& account, const LLUUID& session)
{
    if (!FSWorkspaceController::instance().canQuickSwitch() || account != gAgent.getID() || session != gAgent.getSessionID()) return false;
    const auto all = entries();
    if (std::none_of(all.begin(), all.end(), [&](const Entry& entry) { return entry.key == key; })) return false;
    auto ordered = orderedFavorites(all);
    auto it = std::find_if(ordered.begin(), ordered.end(), [&](const Entry& entry) { return entry.key == key; });
    if (it != ordered.end()) ordered.erase(it);
    else if (ordered.size() < 128) ordered.push_back(*std::find_if(all.begin(), all.end(), [&](const Entry& entry) { return entry.key == key; }));
    else return false;
    LLSD data = LLSD::emptyArray();
    for (const auto& entry : ordered) data.append(entry.key);
    gSavedPerAccountSettings.setLLSD(FAVORITES_SETTING, data);
    return true;
}
bool FSWorkspaceQuickAccess::moveFavorite(const std::string& key, bool forward, const LLUUID& account, const LLUUID& session)
{
    if (!FSWorkspaceController::instance().canQuickSwitch() || account != gAgent.getID() || session != gAgent.getSessionID()) return false;
    std::vector<std::string> keys;
    for (const auto& entry : orderedFavorites(entries())) keys.push_back(entry.key);
    if (!moveFavoriteKey(keys, key, forward)) return false;
    LLSD data = LLSD::emptyArray();
    for (const auto& id : keys) data.append(id);
    gSavedPerAccountSettings.setLLSD(FAVORITES_SETTING, data);
    return true;
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

bool FSWorkspaceQuickAccess::shortcut()
{
    return apply(gSavedPerAccountSettings.getString("FSWorkspaceShortcutTarget"), gAgent.getID(), gAgent.getSessionID());
}
bool FSWorkspaceQuickAccess::cycle(bool forward)
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.canQuickSwitch()) return false;
    const auto all = entries();
    std::vector<std::string> keys;
    for (const auto& entry : orderedFavorites(all)) keys.push_back(entry.key);
    if (keys.empty()) return false;
    const auto active = controller.activeId();
    const auto current = active.empty() ? "layout:" + gSavedSettings.getString("FSChromeActiveProfile") : "workspace:" + active;
    auto it = std::find(keys.begin(), keys.end(), current);
    size_t index = forward ? 0 : keys.size() - 1;
    if (it != keys.end())
    {
        const auto old = static_cast<size_t>(it - keys.begin());
        index = forward ? (old + 1) % keys.size() : (old + keys.size() - 1) % keys.size();
    }
    return apply(keys[index], gAgent.getID(), gAgent.getSessionID());
}
