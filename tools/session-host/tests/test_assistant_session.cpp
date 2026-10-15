/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantsession.h"
#include <cassert>
#include <iostream>
int main()
{
    using namespace fs_assistant;
    assert(sessionMatches(false,"new","",false)); // Existing ordinary clients.
    assert(!sessionMatches(true,"new","",false));
    assert(!sessionMatches(true,"new","old",true));
    assert(!sessionMatches(false,"new","old",true));
    assert(!sessionMatches(true,"","",true));
    assert(sessionMatches(true,"new","new",true));
    assert(!needsActiveCharacter("LLInventory","rename"));
    assert(!needsActiveCharacter("LLAppearance","wearOutfit"));
    assert(!needsActiveCharacter("LLCamera","get"));
    assert(!needsActiveCharacter("LLWorkspace","preview"));
    assert(needsActiveCharacter("LLWorkspace","apply"));
    assert(needsActiveCharacter("LLWorkspace","previous"));
    assert(needsActiveCharacter("LLCamera","snapshot"));
    assert(needsActiveCharacter("LLInventory","snapshotUpload"));
    std::cout<<"Assistant session targeting and active-character eligibility passed.\n";
}
