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
#ifndef FS_WORKSPACE_QUICK_ACCESS_H
#define FS_WORKSPACE_QUICK_ACCESS_H
#include "lluuid.h"
#include <set>
#include <string>
#include <vector>

namespace FSWorkspaceQuickAccess
{
constexpr const char* FAVORITES_SETTING = "FSWorkspaceQuickSwitchFavorites";
struct Entry { std::string key, label; bool layout = false; };
bool cycle(bool forward);
bool shortcut();
std::vector<Entry> entries();
std::set<std::string> favorites(const std::vector<Entry>& entries);
std::vector<Entry> orderedFavorites(const std::vector<Entry>& entries);
bool toggleFavorite(const std::string& key, const LLUUID& account, const LLUUID& session);
bool moveFavorite(const std::string& key, bool forward, const LLUUID& account, const LLUUID& session);
bool apply(const std::string& key, const LLUUID& account, const LLUUID& session);
}
#endif
