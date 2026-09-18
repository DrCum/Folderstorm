/**
 * @file lltoolbaravoidance.h
 * @brief Geometry helper so mid-window toolbars do not yank floaters out of gutters
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (c) 2026 The Phoenix Firestorm Project, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * $/LicenseInfo$
 */

#ifndef LL_LLTOOLBARAVOIDANCE_H
#define LL_LLTOOLBARAVOIDANCE_H

// Custom world viewports can sit side toolbars at the 3D-view edge, leaving a
// utility gutter on the far side. The historic "keep the drag header inside
// the toolbar" push assumed the toolbar was on the window edge, so any floater
// in that gutter was yanked back onto the viewport. Return the X translation
// needed to clear a vertical toolbar without that yank.
inline int computeVerticalToolbarClearanceX(
    int floater_left,
    int floater_right,
    int toolbar_left,
    int toolbar_right,
    int constraint_left,
    int constraint_right,
    bool left_toolbar)
{
    if (floater_right <= floater_left || toolbar_right <= toolbar_left)
    {
        return 0;
    }

    if (left_toolbar)
    {
        if (floater_left >= toolbar_right)
        {
            return 0;
        }

        const bool has_outer_gutter = toolbar_left > constraint_left;
        if (floater_right <= toolbar_left)
        {
            return has_outer_gutter ? 0 : (toolbar_right - floater_right);
        }

        const int to_inside = toolbar_right - floater_left;
        const int to_outside = floater_right - toolbar_left;
        if (has_outer_gutter && to_outside < to_inside)
        {
            return -to_outside;
        }
        return to_inside;
    }

    if (floater_right <= toolbar_left)
    {
        return 0;
    }

    const bool has_outer_gutter = toolbar_right < constraint_right;
    if (floater_left >= toolbar_right)
    {
        return has_outer_gutter ? 0 : (toolbar_left - floater_left);
    }

    const int to_inside = floater_right - toolbar_left;
    const int to_outside = toolbar_right - floater_left;
    if (has_outer_gutter && to_outside < to_inside)
    {
        return to_outside;
    }
    return -to_inside;
}

#endif
