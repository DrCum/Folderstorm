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
#ifndef FS_WORKSPACE_FAVORITES_H
#define FS_WORKSPACE_FAVORITES_H
#include <algorithm>
#include <string>
#include <vector>

namespace FSWorkspaceQuickAccess
{
inline std::vector<std::string> orderedFavoriteKeys(const std::vector<std::string>& saved,
                                                  const std::vector<std::string>& valid)
{
    std::vector<std::string> result;
    for (size_t i = 0; i < std::min<size_t>(saved.size(), 128); ++i)
        if (std::find(valid.begin(), valid.end(), saved[i]) != valid.end() &&
            std::find(result.begin(), result.end(), saved[i]) == result.end()) result.push_back(saved[i]);
    return result;
}
inline bool moveFavoriteKey(std::vector<std::string>& keys, const std::string& key, bool forward)
{
    const auto it = std::find(keys.begin(), keys.end(), key);
    if (it == keys.end() || (forward ? it + 1 == keys.end() : it == keys.begin())) return false;
    std::iter_swap(it, forward ? it + 1 : it - 1);
    return true;
}
}
#endif
