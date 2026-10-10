/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionmonitormodel.h"
#include "fssessionpreview.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    std::array<Message, MaxCharacters> sources{};
    for (int i = 0; i < MaxCharacters; ++i)
    {
        auto& source = sources[i]; source.worker[0] = static_cast<std::uint8_t>(i + 1);
        source.pid = static_cast<std::uint32_t>(i + 100); source.generation = static_cast<std::uint64_t>(i + 1);
        source.account = "account" + std::to_string(i); source.grid = "secondlife"; source.state = State::Ready;
    }
    const auto at = [&](int i) -> const Message* { return validSlot(i) ? &sources[i] : nullptr; };
    MonitorSet monitors;
    for (int i = 1; i < MaxCharacters; ++i) assert(monitors.open(i, sources[i], 0, 1) == i - 1);
    assert(monitors.open(0, sources[0], 0, 1) < 0); // Bound four assignments.
    assert(monitors.open(1, sources[1], 0, 1) == 0); // Same session deduplicated.
    auto different = sources[1]; ++different.generation;
    assert(monitors.open(1, different, 0, 1) < 0); // A new login is not silently adopted.
    assert(monitors.open(-1, sources[1], 0, 1) < 0);
    const auto others = monitors.bindings;
    monitors.beginExchange(0, 1, &sources[0], sources[1]);
    assert(monitors.bindings[0].slot == 1); // No eager swap before acknowledged promotion.
    assert(monitors.finish(false, 0, at) == MonitorSet::Exchange::Kept);
    assert(monitors.bindings[0].slot == 1);
    monitors.beginExchange(0, 1, &sources[0], sources[1]);
    assert(monitors.finish(true, 1, at) == MonitorSet::Exchange::Exchanged);
    assert(monitors.bindings[0].slot == 0 && monitors.bindings[0].key.owns(sources[0]));
    for (int i = 1; i < MaxMonitors; ++i)
        assert(monitors.bindings[i].slot == others[i].slot && monitors.bindings[i].key.owns(sources[others[i].slot]));
    monitors.beginExchange(1, 0, &sources[1], sources[0]);
    ++sources[1].generation;
    assert(monitors.finish(true, 0, at) == MonitorSet::Exchange::Stopped); // Previous session changed.
    assert(!monitors.bindings[0].enabled && monitors.find(0) < 0);
    assert(monitors.open(1, sources[1], 2, 3) == 0);
    monitors.beginExchange(0, 1, &sources[0], sources[1]); monitors.stop(0);
    assert(monitors.finish(true, 1, at) == MonitorSet::Exchange::None); // Explicit close wins.
    assert(monitors.open(1, sources[1], 2, 3) == 0);
    monitors.beginExchange(-1, 1, nullptr, sources[1]);
    assert(monitors.finish(true, 1, at) == MonitorSet::Exchange::Stopped); // No old login to reuse.
    assert(monitors.open(1, sources[1], 0, 1) == 0);
    monitors.beginExchange(0, 1, &sources[0], sources[1]);
    sources[0].state = State::Disconnected;
    assert(monitors.finish(true, 1, at) == MonitorSet::Exchange::Stopped);
    sources[0].state = State::Ready;
    monitors.stopAll(); for (const auto& binding : monitors.bindings) assert(!binding.enabled);
    // Every combination of requested rates stays within the shared cap, with
    // conservative equal shares rounded down to an actual wire-supported rate.
    for (unsigned int count = 1; count <= static_cast<unsigned int>(MaxMonitors); ++count)
    {
        unsigned int combinations = 1; for (unsigned int i = 0; i < count; ++i) combinations *= 4;
        for (unsigned int pattern = 0; pattern < combinations; ++pattern)
        {
            auto value = pattern; std::uint32_t sum = 0;
            for (unsigned int i = 0; i < count; ++i)
            {
                const auto target = value % 4; value /= 4; const auto effective = previewBudget(target, count);
                assert(effective <= previewTarget(target) && previewPolicy(320, 180, effective)); sum += effective;
            }
            assert(sum <= PreviewAggregateRate);
        }
    }
    assert(previewBudget(3, 0) == 0 && previewBudget(3, MaxMonitors + 1) == 0 && previewBudget(9, 1) == 0);
    std::array<std::uint32_t, MaxCharacters> applied{}; applied[1] = applied[2] = 10;
    auto rates = [&](int i) { return applied[i]; };
    assert(!previewAdmission(3, 4, rates)); // Existing 5+5 FPS lanes must lower first.
    applied[1] = 4; assert(previewAdmission(3, 4, rates));
    applied[3] = 4; assert(!previewAdmission(4, 4, rates));
    applied[2] = 4; assert(previewAdmission(4, 4, rates));
    assert(!previewAdmission(-1, 0, rates));
    // All-pairs monitored swaps, preserving the other inactive assignments.
    for (int old = 0; old < MaxCharacters; ++old) for (int next = 0; next < MaxCharacters; ++next) if (old != next)
    {
        MonitorSet pair;
        for (int i = 0; i < MaxCharacters; ++i) if (i != old) assert(pair.open(i, sources[i], 0, 1) >= 0);
        const int origin = pair.find(next); const auto before = pair.bindings;
        pair.beginExchange(old, next, &sources[old], sources[next]);
        assert(pair.finish(true, next, at) == MonitorSet::Exchange::Exchanged);
        assert(pair.bindings[origin].slot == old && pair.find(next) < 0);
        for (int i = 0; i < MaxMonitors; ++i) if (i != origin) assert(pair.bindings[i].slot == before[i].slot);
    }
    std::cout << "Bounded independent monitors, all-pairs successful swaps, stale/cancel/close guards and aggregate admission passed.\n";
}
