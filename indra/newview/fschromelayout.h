/**
 * @file fschromelayout.h
 * @brief Pure geometry helpers for viewport-relative Firestorm chrome
 *
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

#ifndef FSCHROMELAYOUT_H
#define FSCHROMELAYOUT_H

#include <string>
#include <vector>

namespace FSChromeLayout
{

enum class SidePlacement
{
    WindowEdge = 0,
    InsideViewport = 1,
    OutsideViewport = 2,
    Smart = 3,
    Hidden = 4
};

enum class Region
{
    FullWindow = 0,
    Viewport = 1,
    LeftUtility = 2,
    RightUtility = 3,
    CustomSpan = 4
};

struct Rect
{
    int left = 0;
    int bottom = 0;
    int right = 0;
    int top = 0;

    int width() const { return right - left; }
    int height() const { return top - bottom; }
    bool valid() const { return right > left && top > bottom; }
    bool operator==(const Rect& other) const
    {
        return left == other.left && bottom == other.bottom &&
               right == other.right && top == other.top;
    }
    bool operator!=(const Rect& other) const { return !(*this == other); }
};

struct SideRequest
{
    SidePlacement placement = SidePlacement::Smart;
    int toolbar_width = 0;
    int offset = 0;

    bool operator==(const SideRequest& other) const
    {
        return placement == other.placement &&
               toolbar_width == other.toolbar_width &&
               offset == other.offset;
    }
    bool operator!=(const SideRequest& other) const { return !(*this == other); }
};

struct SideLayout
{
    int left_outer_spacer = 0;
    int right_outer_spacer = 0;
    int left_toolbar_width = 0;
    int right_toolbar_width = 0;
    bool left_visible = true;
    bool right_visible = true;
    bool left_used_fallback = false;
    bool right_used_fallback = false;

    bool operator==(const SideLayout& other) const
    {
        return left_outer_spacer == other.left_outer_spacer &&
               right_outer_spacer == other.right_outer_spacer &&
               left_toolbar_width == other.left_toolbar_width &&
               right_toolbar_width == other.right_toolbar_width &&
               left_visible == other.left_visible &&
               right_visible == other.right_visible &&
               left_used_fallback == other.left_used_fallback &&
               right_used_fallback == other.right_used_fallback;
    }
    bool operator!=(const SideLayout& other) const { return !(*this == other); }
};

struct SpanRequest
{
    Region region = Region::Viewport;
    float custom_start_percent = 0.f;
    float custom_end_percent = 100.f;
    int margin_left = 0;
    int margin_right = 0;

    bool operator==(const SpanRequest& other) const
    {
        return region == other.region &&
               custom_start_percent == other.custom_start_percent &&
               custom_end_percent == other.custom_end_percent &&
               margin_left == other.margin_left &&
               margin_right == other.margin_right;
    }
    bool operator!=(const SpanRequest& other) const { return !(*this == other); }
};

struct Span
{
    int left = 0;
    int right = 0;
    bool used_fallback = false;
    Region resolved_region = Region::Viewport;

    int width() const { return right - left; }
    bool valid() const { return right > left; }
    bool operator==(const Span& other) const
    {
        return left == other.left && right == other.right &&
               used_fallback == other.used_fallback &&
               resolved_region == other.resolved_region;
    }
    bool operator!=(const Span& other) const { return !(*this == other); }
};

struct Snapshot
{
    static constexpr int SCHEMA_VERSION = 1;

    int schema_version = SCHEMA_VERSION;
    bool viewport_enabled = false;
    float inset_left = 0.f;
    float inset_right = 0.f;
    float inset_top = 0.f;
    float inset_bottom = 0.f;
    SidePlacement left_placement = SidePlacement::Smart;
    SidePlacement right_placement = SidePlacement::Smart;
    int left_offset = 0;
    int right_offset = 0;
    SpanRequest bottom_dock;
    SpanRequest nav_favorites;
    SpanRequest menu_status;
};

constexpr int kMinInteractiveWidth = 80;
constexpr int kPlacementCount = 5;
constexpr int kRegionCount = 5;

SidePlacement clampSidePlacement(int value);
Region clampRegion(int value);
int clampNonNegative(int value);
float clampPercent(float value);
float clampInsetPercent(float value);

SideLayout computeSideLayout(const Rect& holder, const Rect& world,
                             const SideRequest& left, const SideRequest& right);
Span computeHorizontalSpan(const Rect& window, const Rect& world,
                           const SpanRequest& request, int min_width);

bool isBuiltinProfileId(const std::string& id);
std::vector<std::string> builtinProfileIds();
std::string builtinProfileLabel(const std::string& id);
Snapshot builtinSnapshot(const std::string& id);
Snapshot sanitizedSnapshot(const Snapshot& snapshot);
std::string describeFallback(const std::string& group, Region requested, Region resolved);

} // namespace FSChromeLayout

#endif
