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
#include "../fsworkspacefavorites.h"
#include "../fsquietui.h"
#include <iostream>

int main()
{
    int failed = 0;
    const auto expect = [&failed](bool result, const char* message)
    { if (!result) { ++failed; std::cerr << message << '\n'; } };
    using namespace FSWorkspaceQuickAccess;
    const std::vector<std::string> valid{"workspace:Driving", "workspace:Sorting", "layout:Wide"};
    auto order = orderedFavoriteKeys({"layout:Wide", "workspace:Driving", "invalid", "layout:Wide", "workspace:Sorting"}, valid);
    expect(order == std::vector<std::string>({"layout:Wide", "workspace:Driving", "workspace:Sorting"}), "Preserve mixed favorite order, remove invalid/duplicate keys");
    expect(moveFavoriteKey(order, "workspace:Sorting", false) && order[1] == "workspace:Sorting", "Move favorite up without sorting other entries");
    expect(moveFavoriteKey(order, "layout:Wide", true) && order[0] == "workspace:Sorting", "Move favorite down across workspace/layout types");
    const auto unchanged = order;
    expect(!moveFavoriteKey(order, order.front(), false) && !moveFavoriteKey(order, order.back(), true) &&
        !moveFavoriteKey(order, "missing", true) && order == unchanged, "Boundaries and stale selection leave favorites unchanged");
    std::vector<std::string> empty;
    expect(!moveFavoriteKey(empty, "missing", true), "Empty favorites are safe");
    std::vector<std::string> oversized(128, "invalid"); oversized.push_back("workspace:Driving");
    expect(orderedFavoriteKeys(oversized, valid).empty(), "Bound untrusted saved favorites input");
    expect(FSQuietUI::quietRoutineNotification(true, false, false, false, "FriendOnlineOffline"), "Quiet routine notice");
    expect(!FSQuietUI::quietRoutineNotification(false, false, false, false, "FriendOnlineOffline"), "Normal mode preserves routine notice");
    expect(!FSQuietUI::quietRoutineNotification(true, true, false, false, "Notice"), "Force-show notices stay visible");
    expect(!FSQuietUI::quietRoutineNotification(true, false, true, false, "LocalAssistantConfirm"), "Interactive permission requests stay visible");
    expect(!FSQuietUI::quietRoutineNotification(true, false, false, true, "Notice"), "High priority notices stay visible");
    for (const auto& name : {"SystemMessage", "GodMessage"})
        expect(!FSQuietUI::quietRoutineNotification(true, false, false, false, name), "System alerts stay visible regardless of default priority");
    if (!failed) std::cout << "Favorite ordering and quiet notification policy checks passed\n";
    return failed ? 1 : 0;
}
