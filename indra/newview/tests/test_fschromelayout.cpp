/**
 * @file tests/test_fschromelayout.cpp
 * @brief Standalone geometry tests for FSChromeLayout
 */

#include "../fschromelayout.h"

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

    if (g_failures)
    {
        std::cerr << g_failures << " test(s) failed" << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "FSChromeLayout geometry tests passed" << std::endl;
    return EXIT_SUCCESS;
}
