/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionlifecycle.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>
#ifdef NDEBUG
#error Session regressions require assertions, including Release configurations.
#endif
using namespace fs_session;
struct TestSlot
{
    Restart restart;
    Message snapshot, accepted;
};
Message source(int index)
{
    Message result; result.worker[0] = static_cast<std::uint8_t>(index + 1);
    result.pid = static_cast<std::uint32_t>(100 + index); result.generation = 1;
    result.grid = "secondlife"; result.account = "account" + std::to_string(index);
    result.kind = Kind::Status; result.state = State::Ready; result.mode = Mode::Warm;
    return result;
}
int main()
{
    HostLifecycle controller;
    std::array<std::unique_ptr<TestSlot>, MaxCharacters> slots{};
    for (int i = 0; i < MaxCharacters; ++i)
    {
        slots[i] = std::make_unique<TestSlot>(); slots[i]->snapshot = source(i);
    }
    int active = 0;
    controller.completedHandoff(active);
    slots[1]->snapshot.state = State::Disconnected;
    assert(controller.observeSelection(1) == 1);
    // Polls/failed promotion do not overwrite the management target with A.
    for (int tick = 0; tick < 5; ++tick) assert(controller.observeSelection(controller.selection()) == 1);
    assert(active == 0 && controller.selection() == 1);
    assert(controller.observeSelection(-1) == 1 && controller.observeSelection(MaxCharacters) == 1);
    assert(sessionRenderAllowed(true, true, slots[1]->snapshot.mode));
    assert(slots[1]->restart.begin(slots[1]->snapshot, 100));
    assert(slots[1]->restart.sent(10)); // Native Quit accepted; old process still alive.
    slots[1]->accepted.kind = Kind::Quit; slots[1]->accepted.sequence = 10;
    controller.observeSelection(2);
    Handoff handoff; assert(handoff.begin(active, 2, 1, 1));
    auto reply = slots[active]->snapshot; assert(handoff.accept(active, reply));
    reply = slots[2]->snapshot; reply.mode = Mode::Active; assert(handoff.accept(2, reply));
    active = handoff.target(); controller.completedHandoff(active);
    assert(controller.selection() == 2);
    assert(controller.observeSelection(1) == 1); // Can still reach B's Cancel/Restart.
    assert(controller.pollRestart(slots[1]->restart, true, false, 200, true) == Restart::Result::None);
    assert(slots[3]->restart.begin(slots[3]->snapshot, 100)); // Queued, not sent.
    slots[4].reset(); // Partially filled registry is valid.
    controller.stopManaging(slots);
    for (const auto& slot : slots) if (slot) assert(slot->restart.phase() == Restart::Phase::Idle);
    assert(slots[1]->accepted.kind == Kind::Quit && slots[1]->accepted.sequence == 10);
    assert(controller.pollRestart(slots[1]->restart, false, false, 300, true) == Restart::Result::None);
    assert(controller.pollRestart(slots[3]->restart, false, false, 300, true) == Restart::Result::None);
    // Defensive stop guard also cancels accidentally rearmed state after detach.
    assert(slots[3]->restart.begin(slots[3]->snapshot, 300));
    assert(controller.pollRestart(slots[3]->restart, false, false, 400, true) == Restart::Result::None);
    assert(slots[3]->restart.phase() == Restart::Phase::Idle);
    controller.beginManagement(); // Explicit new login is allowed; old restarts stay cancelled.
    assert(controller.pollRestart(slots[1]->restart, false, false, 500, true) == Restart::Result::None);
    assert(slots[3]->restart.begin(slots[3]->snapshot, 500)); assert(slots[3]->restart.sent(11));
    assert(controller.pollRestart(slots[3]->restart, false, false, 600, false) == Restart::Result::None);
    assert(controller.pollRestart(slots[3]->restart, false, false, 700, true) == Restart::Result::Launch);
    assert(controller.pollRestart(slots[3]->restart, false, false, 800, true) == Restart::Result::None);
    // A failed promotion rolls back and synchronizes management to its real owner.
    assert(handoff.begin(active, 0, 1, 1)); reply = slots[active]->snapshot;
    assert(handoff.accept(active, reply)); handoff.fail(); reply.mode = Mode::Active;
    assert(handoff.accept(active, reply)); controller.completedHandoff(handoff.original());
    assert(controller.selection() == active);
    for (Mode mode : {Mode::Warm, Mode::Economy, Mode::Active})
    {
        assert(sessionRenderAllowed(true, true, mode)); // Recovery UI may draw.
        assert(sessionRenderAllowed(false, false, mode)); // Native login UI.
        assert(sessionRenderAllowed(false, true, mode) == (mode == Mode::Active));
    }
    // Exercise the real routing helper with controls migrating between panels.
    const std::array<int, 3> panels{1, 2, 3};
    std::array<int, 4> owner{1, 1, 1, 1}; // Account, conversation, compose, Send.
    int focus = 10, called = 0;
    auto navigate = [&](bool reverse)
    {
        const auto target = focus;
        return routePanelDialog(target, panels,
            [&](int panel, int child) { return child >= 10 && child < 14 && owner[static_cast<std::size_t>(child - 10)] == panel; },
            [&](int panel)
            {
                called = panel;
                focus = 10 + ((target - 10 + (reverse ? 3 : 1)) % 4);
                return true;
            });
    };
    assert(navigate(false) && focus == 11 && called == 1);
    owner.fill(2); // Chat popped out.
    assert(navigate(false) && focus == 12 && called == 2);
    assert(navigate(false) && focus == 13 && called == 2);
    assert(navigate(true) && focus == 12 && called == 2);
    owner.fill(1); // Dock again without changing target/focus identity.
    assert(navigate(true) && focus == 11 && called == 1);
    owner.fill(3); // Character panel routing is independent too.
    assert(navigate(false) && called == 3);
    int calls = 0;
    assert(!routePanelDialog(99, panels, [](int, int) { return false; }, [&](int) { ++calls; return true; }));
    assert(calls == 0); // Unrelated windows are normally dispatched.
    assert(!routePanelDialog(12, panels, [](int panel, int) { return panel == 2; }, [](int) { return false; }));
    std::cout << "Controller management selection, handoff/restart/detach/relaunch, recovery render policy and docked/pop-out navigation routing passed.\n";
}
