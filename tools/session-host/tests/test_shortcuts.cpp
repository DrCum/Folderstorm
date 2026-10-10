/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionshortcuts.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    const auto ready = [](int slot) { return slot == 0 || slot == 2 || slot == 4; };
    assert(characterShortcutTarget(5,0,ready) == 2 && characterShortcutTarget(6,0,ready) == 4);
    assert(characterShortcutTarget(5,4,ready) == 0 && characterShortcutTarget(1,0,ready) == -1);
    assert(characterShortcutTarget(4,0,ready) == 4 && characterShortcutTarget(7,0,ready) == -1);
    assert(characterShortcutTarget(5,0,[](int slot) { return slot == 0; }) == -1);
    Message current; current.worker[0] = 1; current.state = State::Ready; current.mode = Mode::Active; current.account = "01234567-89ab-cdef-0123-456789abcdef"; current.grid = "secondlife";
    Message event = current; event.eventType = EventType::SwitchIntent; event.eventAt = 1000; event.unread = 5;
    assert(validSwitchIntent(event,current,0,0,1200,true,false));
    assert(!validSwitchIntent(event,current,0,0,1200,false,false) && !validSwitchIntent(event,current,0,0,1200,true,true));
    assert(!validSwitchIntent(event,current,1,0,1200,true,false) && !validSwitchIntent(event,current,0,0,4001,true,false));
    auto shell=event;shell.unread=7;
    assert(validShellIntent(shell,current,0,0,1200,true));
    assert(!validSwitchIntent(shell,current,0,0,1200,true,false));
    assert(!validShellIntent(shell,current,1,0,1200,true) && !validShellIntent(shell,current,0,0,1200,false));
    shell.unread=10;assert(!validShellIntent(shell,current,0,0,1200,true));
    shell.unread=8;shell.generation++;assert(!validShellIntent(shell,current,0,0,1200,true));
    ++event.generation; assert(!validSwitchIntent(event,current,0,0,1200,true,false));
    std::cout << "Ready-only shortcut cycling and fresh source/owner-bound intent guards passed.\n";
}
