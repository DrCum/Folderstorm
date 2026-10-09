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

/** Pure geometry shared by the world renderer and visual viewport editor. */
#ifndef FS_WORLD_VIEW_GEOMETRY_H
#define FS_WORLD_VIEW_GEOMETRY_H

#include <algorithm>
#include <cmath>

namespace FSWorldViewGeometry
{
struct Rect
{
    int left = 0, bottom = 0, right = 0, top = 0;
    int width() const { return right - left; }
    int height() const { return top - bottom; }
    bool empty() const { return width() <= 0 || height() <= 0; }
};

// Fractions, not percentages. The runtime has always kept at least 5% visible.
struct Insets
{
    float left = 0.f, right = 0.f, top = 0.f, bottom = 0.f;
    float horizontal() const { return left + right; }
    float vertical() const { return top + bottom; }
    bool isRealSlice() const { return horizontal() >= .20f || vertical() >= .20f; }
};

inline Insets normalize(Insets insets)
{
    auto edge = [](float value) { return std::isfinite(value) ? std::max(0.f, std::min(.95f, value)) : 0.f; };
    insets.left = edge(insets.left);
    insets.right = edge(insets.right);
    insets.top = edge(insets.top);
    insets.bottom = edge(insets.bottom);
    if (insets.horizontal() > .95f)
    {
        const float scale = .95f / insets.horizontal();
        insets.left *= scale;
        insets.right *= scale;
    }
    if (insets.vertical() > .95f)
    {
        const float scale = .95f / insets.vertical();
        insets.top *= scale;
        insets.bottom *= scale;
    }
    return insets;
}

inline Rect intersection(const Rect& a, const Rect& b)
{
    return {std::max(a.left, b.left), std::max(a.bottom, b.bottom),
            std::min(a.right, b.right), std::min(a.top, b.top)};
}

// Fit output-image aspect into an arbitrary source rectangle without losing its origin.
inline Rect fitImage(Rect bounds, int image_width, int image_height)
{
    if (bounds.empty() || image_width <= 0 || image_height <= 0) return {};
    const double aspect = static_cast<double>(image_width) / image_height;
    const double frame_aspect = static_cast<double>(bounds.width()) / bounds.height();
    if (aspect > frame_aspect)
    {
        const int height = std::max(1, static_cast<int>(std::llround(bounds.width() / aspect)));
        bounds.bottom += (bounds.height() - height) / 2; bounds.top = bounds.bottom + height;
    }
    else
    {
        const int width = std::max(1, static_cast<int>(std::llround(bounds.height() * aspect)));
        bounds.left += (bounds.width() - width) / 2; bounds.right = bounds.left + width;
    }
    return bounds;
}

// Compare display layouts independently of the viewer window's translation.
// All monitors are expressed in the same client space; the first monitor is
// only an anchor, not necessarily the desktop primary monitor.
inline bool sameRelativeMonitorRect(const Rect& a, const Rect& anchor_a, const Rect& b, const Rect& anchor_b)
{
    return a.width() == b.width() && a.height() == b.height() &&
           a.left - anchor_a.left == b.left - anchor_b.left &&
           a.bottom - anchor_a.bottom == b.bottom - anchor_b.bottom;
}

inline Rect apply(const Rect& base, Insets insets)
{
    insets = normalize(insets);
    return {base.left + static_cast<int>(std::lround(base.width() * insets.left)),
            base.bottom + static_cast<int>(std::lround(base.height() * insets.bottom)),
            base.right - static_cast<int>(std::lround(base.width() * insets.right)),
            base.top - static_cast<int>(std::lround(base.height() * insets.top))};
}

// Fit only the part of a monitor actually covered by the normal world area.
// A tiny overlap cannot be fitted exactly under the runtime's 5% minimum.
inline bool fit(const Rect& base, const Rect& monitor, Insets& result)
{
    const Rect target = intersection(base, monitor);
    if (base.empty() || target.empty() || target.width() < base.width() * .05f ||
        target.height() < base.height() * .05f)
    {
        return false;
    }
    result = normalize({static_cast<float>(target.left - base.left) / base.width(),
                        static_cast<float>(base.right - target.right) / base.width(),
                        static_cast<float>(base.top - target.top) / base.height(),
                        static_cast<float>(target.bottom - base.bottom) / base.height()});
    return true;
}
} // namespace FSWorldViewGeometry
#endif
