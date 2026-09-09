/**
 * @file fschromelayout.cpp
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
 * $/LicenseInfo$
 */

#include "fschromelayout.h"

#include <algorithm>
#include <cmath>

namespace FSChromeLayout
{
namespace
{

int clampInt(int value, int min_value, int max_value)
{
    return std::max(min_value, std::min(max_value, value));
}

Rect intersectX(const Rect& holder, const Rect& world)
{
    Rect result = world;
    result.left = clampInt(world.left, holder.left, holder.right);
    result.right = clampInt(world.right, holder.left, holder.right);
    if (result.right < result.left)
    {
        result.right = result.left;
    }
    return result;
}

int resolveLeftX(const Rect& holder, const Rect& world, const SideRequest& request, bool& used_fallback)
{
    used_fallback = false;
    const int width = std::max(0, request.toolbar_width);
    const int offset = clampNonNegative(request.offset);
    const int gutter = std::max(0, world.left - holder.left);

    auto inside = [&]() { return world.left + offset; };
    auto outside = [&]() { return world.left - width - offset; };
    auto window_edge = [&]() { return holder.left + offset; };

    int x = window_edge();
    switch (request.placement)
    {
        case SidePlacement::Hidden:
            return holder.left;
        case SidePlacement::WindowEdge:
            x = window_edge();
            break;
        case SidePlacement::InsideViewport:
            x = inside();
            break;
        case SidePlacement::OutsideViewport:
            if (gutter < width + offset)
            {
                used_fallback = true;
            }
            x = outside();
            break;
        case SidePlacement::Smart:
        default:
            if (gutter >= width + offset)
            {
                x = outside();
            }
            else
            {
                x = inside();
            }
            break;
    }

    const int max_x = std::max(holder.left, holder.right - width);
    return clampInt(x, holder.left, max_x);
}

int resolveRightX(const Rect& holder, const Rect& world, const SideRequest& request, bool& used_fallback)
{
    used_fallback = false;
    const int width = std::max(0, request.toolbar_width);
    const int offset = clampNonNegative(request.offset);
    const int gutter = std::max(0, holder.right - world.right);

    auto inside = [&]() { return world.right - width - offset; };
    auto outside = [&]() { return world.right + offset; };
    auto window_edge = [&]() { return holder.right - width - offset; };

    int x = window_edge();
    switch (request.placement)
    {
        case SidePlacement::Hidden:
            return holder.right - width;
        case SidePlacement::WindowEdge:
            x = window_edge();
            break;
        case SidePlacement::InsideViewport:
            x = inside();
            break;
        case SidePlacement::OutsideViewport:
            if (gutter < width + offset)
            {
                used_fallback = true;
            }
            x = outside();
            break;
        case SidePlacement::Smart:
        default:
            if (gutter >= width + offset)
            {
                x = outside();
            }
            else
            {
                x = inside();
            }
            break;
    }

    const int max_x = std::max(holder.left, holder.right - width);
    return clampInt(x, holder.left, max_x);
}

Span makeSpan(int left, int right, bool used_fallback, Region resolved)
{
    Span span;
    span.left = left;
    span.right = right;
    span.used_fallback = used_fallback;
    span.resolved_region = resolved;
    return span;
}

Span viewportSpan(const Rect& window, const Rect& world)
{
    const Rect clipped = intersectX(window, world);
    if (clipped.width() <= 0)
    {
        return makeSpan(window.left, window.right, true, Region::FullWindow);
    }
    return makeSpan(clipped.left, clipped.right, false, Region::Viewport);
}

} // namespace

SidePlacement clampSidePlacement(int value)
{
    if (value < 0 || value >= kPlacementCount)
    {
        return SidePlacement::Smart;
    }
    return static_cast<SidePlacement>(value);
}

Region clampRegion(int value)
{
    if (value < 0 || value >= kRegionCount)
    {
        return Region::Viewport;
    }
    return static_cast<Region>(value);
}

int clampNonNegative(int value)
{
    return std::max(0, value);
}

float clampPercent(float value)
{
    if (!std::isfinite(value))
    {
        return 0.f;
    }
    return std::max(0.f, std::min(100.f, value));
}

float clampInsetPercent(float value)
{
    return clampPercent(value);
}

SideLayout computeSideLayout(const Rect& holder, const Rect& world,
                             const SideRequest& left, const SideRequest& right)
{
    SideLayout layout;
    if (holder.width() <= 0)
    {
        layout.left_visible = false;
        layout.right_visible = false;
        return layout;
    }

    const Rect clipped = intersectX(holder, world.valid() ? world : holder);
    const bool hide_left = left.placement == SidePlacement::Hidden || left.toolbar_width <= 0;
    const bool hide_right = right.placement == SidePlacement::Hidden || right.toolbar_width <= 0;

    layout.left_visible = !hide_left;
    layout.right_visible = !hide_right;
    layout.left_toolbar_width = hide_left ? 0 : std::max(0, left.toolbar_width);
    layout.right_toolbar_width = hide_right ? 0 : std::max(0, right.toolbar_width);

    const int left_x = hide_left ? holder.left
                                 : resolveLeftX(holder, clipped, left, layout.left_used_fallback);
    const int right_x = hide_right ? holder.right
                                   : resolveRightX(holder, clipped, right, layout.right_used_fallback);

    layout.left_outer_spacer = std::max(0, left_x - holder.left);
    layout.right_outer_spacer = std::max(0, holder.right - (right_x + layout.right_toolbar_width));

    const int used_widths = layout.left_toolbar_width + layout.right_toolbar_width;
    int remaining = std::max(0, holder.width() - used_widths);
    int spacer_total = layout.left_outer_spacer + layout.right_outer_spacer;
    if (spacer_total > remaining)
    {
        if (spacer_total <= 0)
        {
            layout.left_outer_spacer = 0;
            layout.right_outer_spacer = 0;
        }
        else
        {
            const int left_spacer = (layout.left_outer_spacer * remaining) / spacer_total;
            layout.left_outer_spacer = left_spacer;
            layout.right_outer_spacer = remaining - left_spacer;
        }
    }

    return layout;
}

Span computeHorizontalSpan(const Rect& window, const Rect& world,
                           const SpanRequest& request, int min_width)
{
    const int needed = std::max(1, min_width);
    const Rect clipped_world = intersectX(window, world.valid() ? world : window);
    const Span fallback = viewportSpan(window, clipped_world);

    auto finalize = [&](int left, int right, Region resolved, bool forced_fallback) {
        left += clampNonNegative(request.margin_left);
        right -= clampNonNegative(request.margin_right);
        if (right - left < needed)
        {
            if (resolved != Region::Viewport)
            {
                Span viewport = fallback;
                viewport.used_fallback = true;
                return viewport;
            }
            left = fallback.left;
            right = fallback.right;
            forced_fallback = true;
            resolved = fallback.resolved_region;
        }
        if (right <= left)
        {
            Span viewport = fallback;
            viewport.used_fallback = true;
            return viewport;
        }
        return makeSpan(left, right, forced_fallback, resolved);
    };

    switch (request.region)
    {
        case Region::FullWindow:
            return finalize(window.left, window.right, Region::FullWindow, false);
        case Region::LeftUtility:
        {
            const int left = window.left;
            const int right = clipped_world.left;
            if (right - left < needed)
            {
                Span span = fallback;
                span.used_fallback = true;
                return span;
            }
            return finalize(left, right, Region::LeftUtility, false);
        }
        case Region::RightUtility:
        {
            const int left = clipped_world.right;
            const int right = window.right;
            if (right - left < needed)
            {
                Span span = fallback;
                span.used_fallback = true;
                return span;
            }
            return finalize(left, right, Region::RightUtility, false);
        }
        case Region::CustomSpan:
        {
            const float start = clampPercent(request.custom_start_percent);
            float end = clampPercent(request.custom_end_percent);
            if (end <= start)
            {
                Span span = fallback;
                span.used_fallback = true;
                return span;
            }
            const float width = static_cast<float>(std::max(0, window.width()));
            const int left = window.left + static_cast<int>(std::lround(width * start / 100.f));
            const int right = window.left + static_cast<int>(std::lround(width * end / 100.f));
            return finalize(left, right, Region::CustomSpan, false);
        }
        case Region::Viewport:
        default:
            return finalize(fallback.left, fallback.right, fallback.resolved_region, fallback.used_fallback);
    }
}

bool isBuiltinProfileId(const std::string& id)
{
    const auto ids = builtinProfileIds();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::vector<std::string> builtinProfileIds()
{
    return {
        "standard_window",
        "two_monitors_utility_left",
        "two_monitors_utility_right",
        "three_monitors_centered"
    };
}

std::string builtinProfileLabel(const std::string& id)
{
    if (id == "standard_window")
    {
        return "Standard Window";
    }
    if (id == "two_monitors_utility_left")
    {
        return "Two Monitors — Utility Left";
    }
    if (id == "two_monitors_utility_right")
    {
        return "Two Monitors — Utility Right";
    }
    if (id == "three_monitors_centered")
    {
        return "Three Monitors — Centered Viewport";
    }
    return id;
}

Snapshot builtinSnapshot(const std::string& id)
{
    Snapshot snapshot;
    snapshot.bottom_dock.region = Region::Viewport;
    snapshot.nav_favorites.region = Region::Viewport;
    snapshot.menu_status.region = Region::Viewport;
    snapshot.left_placement = SidePlacement::Smart;
    snapshot.right_placement = SidePlacement::Smart;

    if (id == "two_monitors_utility_left")
    {
        snapshot.viewport_enabled = true;
        snapshot.inset_left = 50.f;
    }
    else if (id == "two_monitors_utility_right")
    {
        snapshot.viewport_enabled = true;
        snapshot.inset_right = 50.f;
    }
    else if (id == "three_monitors_centered")
    {
        snapshot.viewport_enabled = true;
        snapshot.inset_left = 100.f / 3.f;
        snapshot.inset_right = 100.f / 3.f;
    }

    return snapshot;
}

Snapshot sanitizedSnapshot(const Snapshot& snapshot)
{
    Snapshot clean = snapshot;
    clean.schema_version = Snapshot::SCHEMA_VERSION;
    clean.inset_left = clampInsetPercent(snapshot.inset_left);
    clean.inset_right = clampInsetPercent(snapshot.inset_right);
    clean.inset_top = clampInsetPercent(snapshot.inset_top);
    clean.inset_bottom = clampInsetPercent(snapshot.inset_bottom);
    clean.left_placement = clampSidePlacement(static_cast<int>(snapshot.left_placement));
    clean.right_placement = clampSidePlacement(static_cast<int>(snapshot.right_placement));
    clean.left_offset = clampNonNegative(snapshot.left_offset);
    clean.right_offset = clampNonNegative(snapshot.right_offset);

    auto sanitize_span = [](SpanRequest request) {
        request.region = clampRegion(static_cast<int>(request.region));
        request.custom_start_percent = clampPercent(request.custom_start_percent);
        request.custom_end_percent = clampPercent(request.custom_end_percent);
        if (request.custom_end_percent < request.custom_start_percent)
        {
            std::swap(request.custom_start_percent, request.custom_end_percent);
        }
        request.margin_left = clampNonNegative(request.margin_left);
        request.margin_right = clampNonNegative(request.margin_right);
        return request;
    };

    clean.bottom_dock = sanitize_span(snapshot.bottom_dock);
    clean.nav_favorites = sanitize_span(snapshot.nav_favorites);
    clean.menu_status = sanitize_span(snapshot.menu_status);
    return clean;
}

std::string describeFallback(const std::string& group, Region requested, Region resolved)
{
    return group + " region fallback from " + std::to_string(static_cast<int>(requested)) +
           " to " + std::to_string(static_cast<int>(resolved));
}

} // namespace FSChromeLayout
