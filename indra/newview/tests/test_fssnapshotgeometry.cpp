/**
 * @file fsworkspacecontroller.cpp
 * @brief Manual workspace adapters and reversible Preferences previews
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
#include "../fsworldviewgeometry.h"
#include <cassert>
int main()
{
    using namespace FSWorldViewGeometry;
    const Rect viewport{1000, 100, 1800, 1000};
    const auto wide = fitImage(viewport, 1920, 1080);
    assert(wide.left == 1000 && wide.right == 1800 && wide.height() == 450 && wide.bottom == 325);
    const auto portrait = fitImage(viewport, 600, 900);
    assert(portrait.bottom == 100 && portrait.top == 1000 && portrait.width() == 600 && portrait.left == 1100);
    const auto matching = fitImage(viewport, 1600, 1800);
    assert(matching.left == viewport.left && matching.bottom == viewport.bottom && matching.width() == viewport.width());
    assert(fitImage(viewport, 0, 900).empty());
    assert(sameRelativeMonitorRect({-1000,0,0,900},{0,0,1600,900}, {-1200,50,-200,950},{-200,50,1400,950}));
    assert(!sameRelativeMonitorRect({-1000,0,0,900},{0,0,1600,900}, {-1000,0,0,1000},{0,0,1600,900}));
}
