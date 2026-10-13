/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionchatbehavior.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    ComposerReturn key; key.down(false,false);
    assert(key.character(false) == ReturnAction::Send);
    assert(key.character(false) == ReturnAction::Ignore); // Duplicate Unicode/default-button delivery.
    key.down(true,false); assert(key.character(true) == ReturnAction::Ignore);
    key.up(); key.down(false,true); assert(key.character(false) == ReturnAction::Newline);
    key.up(); key.ime = true; key.down(false,false); key.ime = false;
    assert(key.character(false) == ReturnAction::Ignore); // Enter commits IME even if composition already ended.
    key.up(); key.down(false,false); assert(key.character(false) == ReturnAction::Send);
    const std::vector<std::pair<std::uint64_t,int>> old{{1,0},{2,20},{3,50}};
    const std::vector<std::pair<std::uint64_t,int>> appended{{1,0},{2,20},{3,50},{4,80}};
    const std::vector<std::pair<std::uint64_t,int>> trimmed{{2,0},{3,30},{4,60}};
    assert(fitTranscriptAnchor(26,old,appended,100) == 26);
    assert(fitTranscriptAnchor(26,old,trimmed,100) == 6);
    assert(fitTranscriptAnchor(5,old,trimmed,100) == 0);
    const std::vector<std::pair<std::uint64_t,int>> shortened{{2,0},{3,30}};
    assert(fitTranscriptAnchor(99,old,shortened,44) == 44); // Wrapped anchor remains bounded.
    std::cout << "Composer Enter, repeats, IME commits and message-anchored history restoration passed.\n";
}
