/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsimcontactpolicy.h"
#include <cassert>
#include <iostream>
int main()
{
    using FSIMContactPolicy::canSend;
    assert(canSend(true, false, false)); // Ordinary account -> ordinary contact.
    assert(canSend(true, true, false));  // Ordinary account -> support contact.
    assert(canSend(true, false, true)); // NO_SUPPORT account -> ordinary contact.
    assert(!canSend(true, true, true)); // NO_SUPPORT account -> support contact.
    assert(canSend(false, true, true)); // Existing policy does not block group/conference sends.
    std::cout << "Native/shared contact policy: flagged/unflagged senders, support/ordinary contacts and group scope passed.\n";
}
