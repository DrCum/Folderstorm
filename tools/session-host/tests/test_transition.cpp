/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessiontransition.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace fs_session;
int main()
{
    TransitionClock clock;
    assert(!clock.active() && clock.finished(100, 1, true));
    clock.begin(100, 7);
    assert(clock.active() && !clock.finished(100, 7, true) && clock.fraction(100) == 0.f);
    assert(std::abs(clock.fraction(650) - 0.5f) < 0.0001f);
    assert(clock.fraction(1200) == 1.f && clock.fraction(999999) == 1.f);
    assert(clock.finished(1200, 7, true) && clock.finished(700, 8, true));
    assert(clock.finished(99, 7, true) && clock.finished(700, 7, false));
    float previous = 0.f;
    for (std::uint64_t t = 100; t <= 1200; ++t)
    { const auto weight = clock.fraction(t); assert(weight >= previous && weight >= 0.f && weight <= 1.f); previous = weight; }
    clock.cancel(); assert(!clock.active()); clock.begin(0, 7); assert(clock.active());
    clock.begin(100, 0); assert(!clock.active());
    clock.begin(100,7,2000); assert(!clock.finished(1200,7,true));
    assert(std::abs(clock.fraction(1100)-0.5f) < 0.0001f); assert(clock.finished(2100,7,true));
    clock.begin(0,7,249); assert(!clock.active()); clock.begin(0,7,2001); assert(!clock.active());
    clock.begin(0,7,250); assert(clock.finished(250,7,true) && !clock.finished(249,7,true));
    Message pending; pending.worker[0] = 42; pending.kind = Kind::SetMode; pending.sequence = 9;
    pending.generation = 2; pending.account = "01234567-89ab-cdef-0123-456789abcdef"; pending.grid = "secondlife";
    Message cancel = pending; cancel.kind = Kind::CancelTransition; cancel.cursor = 9; cancel.sequence = 10;
    Frame bytes{}; Message decoded;
    assert(encode(cancel, bytes) && decode(bytes, decoded) && transitionCancelMatches(decoded, pending));
    auto bad = cancel; ++bad.cursor; assert(!transitionCancelMatches(bad, pending));
    bad = cancel; ++bad.generation; assert(!transitionCancelMatches(bad, pending));
    bad = cancel; bad.sequence = pending.sequence; assert(!transitionCancelMatches(bad, pending));
    bad = cancel; ++bad.worker[0]; assert(!transitionCancelMatches(bad, pending));
    bad = cancel; bad.account.clear(); assert(!transitionCancelMatches(bad, pending));
    bad = cancel; bad.grid = "other"; assert(!transitionCancelMatches(bad, pending));
    bad = pending; assert(!transitionCancelMatches(bad, pending));
    pending.sequence = 11; assert(!transitionCancelMatches(cancel, pending)); // Never cancel a later switch.
    std::cout << "Finite transition clock, monotone easing and exact source/request cancellation guards passed.\n";
}
