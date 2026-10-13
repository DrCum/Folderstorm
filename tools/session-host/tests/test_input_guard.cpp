/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessioninputguard.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    InputQuarantine guard;
    std::array<bool,256> physical{};
    physical[0] = physical[1] = physical[20] = physical['W'] = physical[160] = true;
    guard.capture([&](unsigned int key) { return physical[key]; });
    assert(guard.size() == 3 && guard.allows(0) && guard.allows(1));
    assert(!guard.allows('W') && guard.allows('A')); // Fresh text is not globally vetoed.
    assert(!guard.allows(20)); // A physically held lock key, not its toggle.
    physical[20] = physical['W'] = false;
    guard.refresh([&](unsigned int key) { return physical[key]; });
    assert(guard.allows(20) && guard.allows('W')); // No native key-up/logical state required.
    assert(!guard.allows(160)); // Real held modifiers remain quarantined without timeout.
    guard.released(160); assert(guard.size() == 0);
    physical['W'] = true; guard.capture([&](unsigned int key) { return physical[key]; });
    const auto aliases = [](unsigned int native,unsigned int logical)
    { return logical == '0' ? native == '0' || native == 96 : native == logical; };
    physical.fill(false); physical[96] = true; guard.capture([&](unsigned int key) { return physical[key]; });
    assert(!guard.allowsLogical('0',aliases) && guard.allowsLogical('A',aliases));
    // A later H event must never change whether an inherited W/keypad key is
    // blocked in a per-frame scan. Forward aliases include both digit origins.
    assert(guard.allows('0') && !guard.allows(96));
    std::array<bool,256> logical{}; logical[96] = logical['A'] = true;
    physical[96] = false;
    unsigned int releases = 0;
    guard.refresh([&](unsigned int key) { return physical[key]; },[&](unsigned int key) { logical[key] = false; ++releases; });
    assert(releases == 1 && !logical[96] && logical['A']); // Selective stale cleanup keeps fresh state intact.
    guard.reset(); assert(guard.allows('W') && guard.allows(999));
    std::cout << "Physical key quarantine, fresh text, lock keys and missed-release recovery passed.\n";
}
