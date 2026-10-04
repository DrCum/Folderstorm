/**
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
/** @file test_fsworkspacelayout.cpp @brief Standalone workspace geometry/name tests. */
#include "../fsworkspacelayout.h"
#include <cmath>
#include <iostream>
#include <limits>

using namespace FSWorkspaceLayout;
namespace
{
int failures = 0;
void expect(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}
bool near(float a, float b) { return std::abs(a - b) <= .001f; }
}
int main()
{
    for (int i = 0; i < MAX_ROLES; ++i)
    {
        Role role = Role::MiniMap;
        expect(parseRole(roleId(static_cast<Role>(i)), role) && static_cast<int>(role) == i,
               "Allowlisted roles round trip");
    }
    Role role = Role::MiniMap;
    expect(!parseRole("inventory", role) && role == Role::MiniMap, "Arbitrary registry names are rejected");
    expect(!parseRole("inventory_primary/42", role), "Arbitrary keys cannot select a window");
    expect(std::string(roleId(static_cast<Role>(99))).empty(), "Invalid enum has no role ID");
    expect(isSafeProfileName("Inventory sorting 2.0"), "Common names allowed");
    expect(!isSafeProfileName("") && !isSafeProfileName("   "), "Empty labels rejected");
    expect(!isSafeProfileName("../x/y") && !isSafeProfileName("x\ny"), "Path/control characters rejected");
    expect(!isSafeProfileName("builtin:driving") && isBuiltinProfileId("builtin:future"), "Builtin namespace reserved");
    expect(isSafeProfileName(std::string(64, 'a')) && !isSafeProfileName(std::string(65, 'a')), "Name length bounded");

    const Rect frame{100.f, 40.f, 2100.f, 1040.f};
    const Rect original{1200.f, 100.f, 1550.f, 700.f};
    const Window window = capture(original, frame, true, false);
    const auto roundtrip = fit(window, frame, 100.f, 100.f);
    expect(window.visible && !window.minimized && window.has_geometry, "Capture records normal state");
    expect(near(roundtrip.rect.left, original.left) && near(roundtrip.rect.bottom, original.bottom) &&
           near(roundtrip.rect.right, original.right) && near(roundtrip.rect.top, original.top) &&
           !roundtrip.adjusted, "Nonzero origin capture/fit round trip");
    const auto resized = fit(window, {10.f, 20.f, 1010.f, 820.f}, 100.f, 100.f);
    expect(near(resized.rect.width(), 350.f) && near(resized.rect.height(), 600.f), "UI-unit size survives frame/DPI change");
    expect(near((resized.rect.left + resized.rect.width() * .5f - 10.f) / 1000.f, window.center_x),
           "Normalized horizontal center survives unequal frame resizing");
    Window offscreen = window;
    offscreen.center_x = 5.f;
    offscreen.center_y = -2.f;
    const auto fitted = fit(offscreen, frame, 100.f, 100.f);
    expect(near(fitted.rect.right, frame.right) && near(fitted.rect.bottom, frame.bottom) && fitted.adjusted,
           "Positions outside new client become reachable");
    const auto tiny = fit(window, {20.f, 30.f, 100.f, 70.f}, 200.f, 150.f);
    expect(near(tiny.rect.width(), 200.f) && near(tiny.rect.height(), 150.f) &&
           near(tiny.rect.right, 100.f) && near(tiny.rect.top, 70.f) && tiny.adjusted,
           "Tiny client retains actual minima and reachable title controls");
    const auto shrunk = fit(window, {0.f, 0.f, 200.f, 300.f}, 100.f, 100.f);
    expect(near(shrunk.rect.width(), 200.f) && near(shrunk.rect.height(), 300.f), "Large saved size clamps when minima permit");
    expect(!capture({0.f, 0.f, 0.f, 3.f}, frame, false, false).has_geometry, "Empty geometry is not invented");
    Window invalid = window;
    invalid.center_x = std::numeric_limits<float>::quiet_NaN();
    const auto rejected = fit(invalid, frame, 100.f, 100.f);
    expect(rejected.rect.width() == 0.f && rejected.adjusted, "Invalid raw geometry rejected before placement");
    expect(fit(window, {0.f, 0.f, 0.f, 0.f}, 100.f, 100.f).rect.height() == 0.f, "Unavailable client frame rejected");
    if (!failures) std::cout << "Workspace geometry/name tests passed\n";
    return failures ? 1 : 0;
}
