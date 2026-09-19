/**
 * @file tests/test_fschromelayout.cpp
 * @brief Standalone geometry tests for FSChromeLayout
 */

#include "../fschromelayout.h"
#include "../../llui/lltoolbaravoidance.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace FSChromeLayout;

namespace
{

int g_failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << std::endl;
        ++g_failures;
    }
}

void expectEq(int actual, int expected, const std::string& message)
{
    if (actual != expected)
    {
        std::cerr << "FAIL: " << message << " actual=" << actual
                  << " expected=" << expected << std::endl;
        ++g_failures;
    }
}

Rect windowRect(int width = 3000, int height = 1000)
{
    return Rect{0, 0, width, height};
}

Rect worldFromInsets(const Rect& window, float left_pct, float right_pct)
{
    Rect world = window;
    world.left = static_cast<int>(window.width() * left_pct);
    world.right = static_cast<int>(window.width() * (1.f - right_pct));
    return world;
}

} // namespace

int main()
{
    const int toolbar = 40;
    const Rect window = windowRect();

    {
        const Rect world = window;
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, smart, smart);
        expect(layout.left_visible && layout.right_visible, "full window smart keeps both toolbars");
        expectEq(layout.left_outer_spacer, 0, "full window smart left spacer");
        expectEq(layout.right_outer_spacer, 0, "full window smart right spacer");
        expectEq(layout.left_toolbar_width, toolbar, "full window smart left width");
        expectEq(layout.right_toolbar_width, toolbar, "full window smart right width");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, smart, smart);
        expectEq(layout.left_outer_spacer, world.left - toolbar, "centered smart left sits outside");
        expectEq(layout.right_outer_spacer, window.right - world.right - toolbar, "centered smart right sits outside");
        expect(!layout.left_used_fallback && !layout.right_used_fallback, "centered smart has room");
    }

    {
        const Rect world = worldFromInsets(window, 0.5f, 0.f); // utility on left
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, smart, smart);
        expectEq(layout.left_outer_spacer, world.left - toolbar, "utility left: left toolbar outside");
        expectEq(layout.right_outer_spacer, 0, "utility left: right toolbar inside window edge");
    }

    {
        const Rect world = worldFromInsets(window, 0.f, 0.5f); // utility on right
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, smart, smart);
        expectEq(layout.left_outer_spacer, 0, "utility right: left toolbar inside");
        expectEq(layout.right_outer_spacer, window.right - world.right - toolbar, "utility right: right toolbar outside");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SideRequest hidden{SidePlacement::Hidden, toolbar, 0};
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, hidden, smart);
        expect(!layout.left_visible, "hidden left is invisible");
        expectEq(layout.left_toolbar_width, 0, "hidden left has zero width");
        expectEq(layout.left_outer_spacer, 0, "hidden left spacer is zero");
        expect(layout.right_visible, "right remains visible when left is hidden");
    }

    {
        const Rect world = worldFromInsets(window, 0.01f, 0.01f);
        SideRequest outside{SidePlacement::OutsideViewport, 80, 0};
        const SideLayout layout = computeSideLayout(window, world, outside, outside);
        expect(layout.left_used_fallback && layout.right_used_fallback, "tiny gutters fallback for outside");
        expectEq(layout.left_outer_spacer, 0, "outside fallback clamps to window left");
        expectEq(layout.right_outer_spacer, 0, "outside fallback clamps to window right");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SpanRequest viewport{Region::Viewport};
        const Span span = computeHorizontalSpan(window, world, viewport, kMinInteractiveWidth);
        expectEq(span.left, world.left, "viewport span left");
        expectEq(span.right, world.right, "viewport span right");
        expect(!span.used_fallback, "viewport span does not fall back");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SpanRequest left_util{Region::LeftUtility};
        const Span span = computeHorizontalSpan(window, world, left_util, kMinInteractiveWidth);
        expectEq(span.left, window.left, "left utility starts at window");
        expectEq(span.right, world.left, "left utility ends at viewport");
    }

    {
        const Rect world = worldFromInsets(window, 0.01f, 0.4f);
        SpanRequest left_util{Region::LeftUtility};
        const Span span = computeHorizontalSpan(window, world, left_util, kMinInteractiveWidth);
        expect(span.used_fallback, "narrow left utility falls back to viewport");
        expectEq(span.left, world.left, "fallback span uses viewport left");
        expectEq(span.right, world.right, "fallback span uses viewport right");
    }

    {
        SpanRequest custom{Region::CustomSpan, 10.f, 40.f, 0, 0};
        const Span span = computeHorizontalSpan(window, window, custom, kMinInteractiveWidth);
        expectEq(span.left, 300, "custom span 10%");
        expectEq(span.right, 1200, "custom span 40%");
    }

    {
        SpanRequest inverted{Region::CustomSpan, 80.f, 20.f, 0, 0};
        const Span span = computeHorizontalSpan(window, window, inverted, kMinInteractiveWidth);
        expect(span.used_fallback, "inverted custom span falls back");
    }

    {
        expect(isBuiltinProfileId("three_monitors_centered"), "centered profile exists");
        const Snapshot snapshot = builtinSnapshot("three_monitors_centered");
        expect(snapshot.viewport_enabled, "centered profile enables viewport");
        expect(snapshot.left_placement == SidePlacement::Smart, "centered profile uses smart left");
        expect(snapshot.bottom_dock.region == Region::Viewport, "centered profile docks to viewport");
        expect(snapshot.inset_left > 33.f && snapshot.inset_left < 34.f, "centered left inset");
        expect(snapshot.inset_right > 33.f && snapshot.inset_right < 34.f, "centered right inset");
    }

    {
        Snapshot dirty;
        dirty.left_placement = static_cast<SidePlacement>(99);
        dirty.left_offset = -12;
        dirty.inset_left = 150.f;
        dirty.bottom_dock.custom_start_percent = 90.f;
        dirty.bottom_dock.custom_end_percent = 10.f;
        const Snapshot clean = sanitizedSnapshot(dirty);
        expect(clean.left_placement == SidePlacement::Smart, "invalid placement defaults to smart");
        expectEq(clean.left_offset, 0, "negative offset clamps to zero");
        expect(clean.inset_left == 100.f, "inset clamps to 100");
        expect(clean.bottom_dock.custom_start_percent <= clean.bottom_dock.custom_end_percent,
               "span percents are ordered");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SideRequest edge{SidePlacement::WindowEdge, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, edge, edge);
        expectEq(layout.left_outer_spacer, 0, "window-edge left spacer");
        expectEq(layout.right_outer_spacer, 0, "window-edge right spacer");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SideRequest inside{SidePlacement::InsideViewport, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, inside, inside);
        expectEq(layout.left_outer_spacer, world.left, "inside left sits on viewport edge");
        expectEq(layout.right_outer_spacer, window.right - world.right, "inside right sits on viewport edge");
        expect(layout.left_outer_spacer + layout.left_toolbar_width +
                   layout.right_toolbar_width + layout.right_outer_spacer <= window.width(),
               "inside placement does not overflow holder");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SideRequest outside{SidePlacement::OutsideViewport, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, outside, outside);
        expectEq(layout.left_outer_spacer, world.left - toolbar, "outside left touches viewport");
        expectEq(layout.right_outer_spacer, window.right - world.right - toolbar, "outside right touches viewport");
    }

    {
        const Rect world = worldFromInsets(window, 0.2f, 0.1f);
        SideRequest inside{SidePlacement::InsideViewport, toolbar, 12};
        const SideLayout layout = computeSideLayout(window, world, inside, inside);
        expectEq(layout.left_outer_spacer, world.left + 12, "inside left offset");
        expectEq(layout.right_outer_spacer, window.right - world.right + 12, "inside right offset");
    }

    {
        const Rect world = worldFromInsets(window, 0.2f, 0.1f);
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout layout = computeSideLayout(window, world, smart, smart);
        expectEq(layout.left_outer_spacer, world.left - toolbar, "unequal insets: left smart outside");
        expectEq(layout.right_outer_spacer, window.right - world.right - toolbar, "unequal insets: right smart outside");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SpanRequest full{Region::FullWindow};
        const Span span = computeHorizontalSpan(window, world, full, kMinInteractiveWidth);
        expectEq(span.left, window.left, "full window span left");
        expectEq(span.right, window.right, "full window span right");
        expect(span.resolved_region == Region::FullWindow, "full window keeps region");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        SpanRequest right_util{Region::RightUtility};
        const Span span = computeHorizontalSpan(window, world, right_util, kMinInteractiveWidth);
        expectEq(span.left, world.right, "right utility starts at viewport");
        expectEq(span.right, window.right, "right utility ends at window");
        expect(!span.used_fallback, "wide right utility does not fall back");
    }

    {
        const Rect world = worldFromInsets(window, 0.4f, 0.01f);
        SpanRequest right_util{Region::RightUtility};
        const Span span = computeHorizontalSpan(window, world, right_util, kMinInteractiveWidth);
        expect(span.used_fallback, "narrow right utility falls back to viewport");
        expectEq(span.left, world.left, "right utility fallback left");
        expectEq(span.right, world.right, "right utility fallback right");
        expect(span.valid() && span.width() > 0, "fallback span stays interactive");
    }

    {
        SpanRequest custom{Region::CustomSpan, 10.f, 40.f, 8, 12};
        const Span span = computeHorizontalSpan(window, window, custom, kMinInteractiveWidth);
        expectEq(span.left, 308, "custom span applies left margin");
        expectEq(span.right, 1188, "custom span applies right margin");
        expect(!span.used_fallback, "custom span with margins stays valid");
    }

    {
        SpanRequest custom{Region::CustomSpan, 10.f, 12.f, 40, 40};
        const Span span = computeHorizontalSpan(window, window, custom, kMinInteractiveWidth);
        expect(span.used_fallback, "margins that invert a custom span fall back");
        expect(span.valid(), "inverted custom span fallback remains valid");
    }

    {
        const std::vector<std::string> ids = builtinProfileIds();
        expectEq(static_cast<int>(ids.size()), 4, "four built-in profiles");
        expect(builtinProfileLabel("standard_window") == "Standard Window", "standard label");
        expect(builtinProfileLabel("two_monitors_utility_left") == "Two Monitors — Utility Left",
               "utility left label");
        expect(builtinProfileLabel("two_monitors_utility_right") == "Two Monitors — Utility Right",
               "utility right label");
        expect(builtinProfileLabel("three_monitors_centered") == "Three Monitors — Centered Viewport",
               "centered label");

        const Snapshot standard = builtinSnapshot("standard_window");
        expect(!standard.viewport_enabled, "standard window leaves viewport disabled");
        expectEq(static_cast<int>(standard.left_placement), static_cast<int>(SidePlacement::Smart),
                 "standard uses smart left");
        expect(standard.nav_favorites.region == Region::Viewport, "standard nav uses viewport");
        expect(standard.menu_status.region == Region::Viewport, "standard menu uses viewport");

        const Snapshot left = builtinSnapshot("two_monitors_utility_left");
        expect(left.viewport_enabled, "utility left enables viewport");
        expect(left.inset_left == 50.f && left.inset_right == 0.f, "utility left inset");

        const Rect dual = windowRect(3840, 1080);
        const Rect world_right = worldFromInsets(dual, 0.5f, 0.f);
        SideRequest smart{SidePlacement::Smart, toolbar, 0};
        const SideLayout dual_layout = computeSideLayout(dual, world_right, smart, smart);
        const int left_bar_left = dual.left + dual_layout.left_outer_spacer;
        const int left_bar_right = left_bar_left + dual_layout.left_toolbar_width;
        expect(left_bar_left > dual.left, "utility-left toolbar sits off the window edge");
        expectEq(computeVerticalToolbarClearanceX(80, 480, left_bar_left, left_bar_right,
                                                  dual.left, dual.right, true),
                 0, "inventory in the left utility monitor is not yanked back");

        const Snapshot right = builtinSnapshot("two_monitors_utility_right");
        expect(right.viewport_enabled, "utility right enables viewport");
        expect(right.inset_left == 0.f && right.inset_right == 50.f, "utility right inset");
    }

    {
        expect(clampSidePlacement(-1) == SidePlacement::Smart, "invalid side placement defaults smart");
        expect(clampRegion(99) == Region::Viewport, "invalid region defaults viewport");
        expectEq(clampNonNegative(-4), 0, "negative clamp");
        expect(clampPercent(150.f) == 100.f, "percent clamps high");
        expect(clampPercent(-8.f) == 0.f, "percent clamps low");
        const std::string text = describeFallback("bottom dock", Region::LeftUtility, Region::Viewport);
        expect(text.find("bottom dock") != std::string::npos, "fallback diagnostic names the group");
    }

    {
        // Window-edge left toolbar: floater hanging fully off the left is pulled inward.
        expectEq(computeVerticalToolbarClearanceX(-200, -5, 0, 30, 0, 3000, true),
                 35, "window-edge left toolbar keeps header inside");
        // Mid-window left toolbar (viewport edge): floater in the left gutter stays put.
        expectEq(computeVerticalToolbarClearanceX(100, 500, 1890, 1920, 0, 3840, true),
                 0, "left gutter floater is not yanked to the viewport");
        // Overlap nearer the gutter: push fully into the gutter.
        expectEq(computeVerticalToolbarClearanceX(1860, 1910, 1890, 1920, 0, 3840, true),
                 -20, "overlap nearer the gutter exits into the gutter");
        // Overlap nearer the viewport: push fully into the viewport.
        expectEq(computeVerticalToolbarClearanceX(1900, 1960, 1890, 1920, 0, 3840, true),
                 20, "overlap nearer the viewport exits into the viewport");
        // Completely on the inner side: no move.
        expectEq(computeVerticalToolbarClearanceX(1930, 2300, 1890, 1920, 0, 3840, true),
                 0, "viewport-side floater is left alone");

        // Window-edge right toolbar: floater hanging fully off the right is pulled inward.
        expectEq(computeVerticalToolbarClearanceX(3010, 3300, 2970, 3000, 0, 3000, false),
                 -40, "window-edge right toolbar keeps header inside");
        // Mid-window right toolbar: floater in the right gutter stays put.
        expectEq(computeVerticalToolbarClearanceX(2000, 2400, 1920, 1950, 0, 3840, false),
                 0, "right gutter floater is not yanked to the viewport");
        // Overlap nearer the right gutter.
        expectEq(computeVerticalToolbarClearanceX(1930, 1980, 1920, 1950, 0, 3840, false),
                 20, "overlap nearer the right gutter exits into the gutter");
        // Overlap nearer the viewport.
        expectEq(computeVerticalToolbarClearanceX(1880, 1940, 1920, 1950, 0, 3840, false),
                 -20, "overlap nearer the viewport exits into the viewport");
    }

    {
        const Rect world = worldFromInsets(window, 1.f / 3.f, 1.f / 3.f);
        for (int placement = 0; placement < kPlacementCount; ++placement)
        {
            SideRequest request{clampSidePlacement(placement), toolbar, 0};
            const SideLayout layout = computeSideLayout(window, world, request, request);
            expect(layout.left_outer_spacer >= 0 && layout.right_outer_spacer >= 0,
                   "placement never yields negative spacers");
            expect(layout.left_outer_spacer + layout.left_toolbar_width +
                       layout.right_toolbar_width + layout.right_outer_spacer <= window.width(),
                   "placement fits the holder");
        }
    }

    if (g_failures)
    {
        std::cerr << g_failures << " test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "FSChromeLayout geometry tests passed" << std::endl;
    return EXIT_SUCCESS;
}
