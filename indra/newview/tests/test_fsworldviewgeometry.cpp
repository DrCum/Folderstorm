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

#include "../fsworldviewgeometry.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace FSWorldViewGeometry;
namespace
{
int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
void same(const Rect& actual, const Rect& expected, const char* message)
{
    check(std::abs(actual.left - expected.left) <= 1 && std::abs(actual.right - expected.right) <= 1 &&
          std::abs(actual.top - expected.top) <= 1 && std::abs(actual.bottom - expected.bottom) <= 1, message);
}
}
int main()
{
    Insets fitted;
    const Rect unequal{0, 0, 4480, 1440};
    check(fit(unequal, {0, 0, 1920, 1080}, fitted), "unequal monitors fit");
    same(apply(unequal, fitted), {0, 0, 1920, 1080}, "unequal width and height fit exactly");
    check(fit(unequal, {1920, 0, 4480, 1440}, fitted), "larger right monitor fits");
    same(apply(unequal, fitted), {1920, 0, 4480, 1440}, "larger monitor is not half window");

    const Rect base{20, 30, 3020, 1530};
    check(fit(base, {-1800, 100, 1200, 1800}, fitted), "negative-origin partially covered monitor fits");
    same(apply(base, fitted), {20, 100, 1200, 1530}, "offset base and partial monitor are intersected");
    check(fit(base, {1100, -300, 2100, 1800}, fitted), "center monitor fits");
    same(apply(base, fitted), {1100, 30, 2100, 1530}, "center monitor preserves vertical base");
    check(!fit(base, {4000, 0, 5000, 1000}, fitted), "no intersection rejected");
    check(!fit(base, {3010, 30, 5000, 1530}, fitted), "less than five percent width rejected");
    check(!fit(base, {20, 1520, 3020, 2000}, fitted), "less than five percent height rejected");
    check(!fit({0, 0, 0, 0}, base, fitted), "empty base rejected");
    check(fit(base, {-500, -500, 4000, 2000}, fitted), "full coverage fits");
    same(apply(base, fitted), base, "full monitor coverage gives no insets");

    const Insets normalized = normalize({.9f, .9f, .8f, .4f});
    check(std::abs(normalized.horizontal() - .95f) < .00001f, "horizontal normalization leaves five percent");
    check(std::abs(normalized.top / normalized.bottom - 2.f) < .00001f, "normalization preserves margin proportions");
    const Insets nonfinite = normalize({std::numeric_limits<float>::quiet_NaN(), -.2f, 2.f, 0.f});
    check(nonfinite.left == 0.f && nonfinite.right == 0.f && nonfinite.top == .95f, "nonfinite/debug values safely normalized");
    same(apply(base, {.1f, .2f, .3f, .4f}), {320, 630, 2420, 1080}, "asymmetric four-edge inset rounding");
    const Rect narrow{0, 0, 2000, 1000};
    check(fit(narrow, {1900, 0, 2000, 1000}, fitted), "exact five percent slice allowed");
    same(apply(narrow, fitted), {1900, 0, 2000, 1000}, "ninety-five percent numeric margin representable");
    check(sameRelativeMonitorRect({-500, -100, 1420, 980}, {-500, -100, 1420, 980},
                                  {-800, -300, 1120, 780}, {-800, -300, 1120, 780}),
          "window translation does not change monitor topology");
    check(sameRelativeMonitorRect({1420, -100, 3980, 1340}, {-500, -100, 1420, 980},
                                  {1120, -300, 3680, 1140}, {-800, -300, 1120, 780}),
          "unequal monitor topology is translation independent");
    check(!sameRelativeMonitorRect({1420, -100, 3980, 1340}, {-500, -100, 1420, 980},
                                   {1120, -200, 3680, 1240}, {-800, -300, 1120, 780}),
          "relative vertical display rearrangement changes topology");
    check(!sameRelativeMonitorRect({1420, -100, 3980, 1340}, {-500, -100, 1420, 980},
                                   {1120, -300, 3040, 780}, {-800, -300, 1120, 780}),
          "reused display index with different dimensions changes topology");
    if (!failures) std::cout << "World viewport geometry tests passed\n";
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
