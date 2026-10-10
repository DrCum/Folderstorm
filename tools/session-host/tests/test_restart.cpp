/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionrestart.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    Message source; source.worker[0] = 7; source.pid = 123; source.generation = 4;
    source.account = "01234567-89ab-cdef-0123-456789abcdef"; source.grid = "secondlife";
    source.state = State::Disconnected;
    Restart restart;
    assert(restart.begin(source, 100)); assert(!restart.begin(source, 100));
    auto changed = source; ++changed.generation; assert(!restart.owns(changed));
    changed = source; ++changed.pid; assert(!restart.owns(changed));
    changed = source; changed.worker[0] = 8; assert(!restart.owns(changed));
    changed = source; changed.grid = "other"; assert(!restart.owns(changed));
    changed = source; changed.account.clear(); assert(!restart.owns(changed));
    assert(restart.owns(source)); assert(restart.sent(9)); assert(!restart.sent(10));
    assert(restart.poll(true, false, 1000) == Restart::Result::None);
    assert(restart.poll(false, false, 1001) == Restart::Result::Launch);
    assert(restart.poll(false, false, 1002) == Restart::Result::None);
    assert(restart.begin(source, 2000)); assert(restart.sent(11));
    assert(restart.poll(false, true, 2001) == Restart::Result::Cancelled);
    assert(restart.poll(false, false, 2002) == Restart::Result::None);
    assert(restart.begin(source, 3000));
    assert(restart.poll(true, false, 63000) == Restart::Result::TimedOut);
    assert(restart.poll(false, false, 63001) == Restart::Result::None);
    assert(restart.begin(source, 64000)); restart.cancel();
    assert(restart.poll(false, false, 64001) == Restart::Result::None);
    source.pid = 0; assert(!restart.begin(source, 65000));
    std::cout << "Source-bound restart, delayed exit, cancellation, timeout and one-shot launch passed.\n";
}
