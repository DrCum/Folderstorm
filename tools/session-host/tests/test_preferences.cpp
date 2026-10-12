/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionpreferences.h"
#include <cassert>
#include <iostream>
#ifdef NDEBUG
#error Preferences regressions require assertions in Release too.
#endif
using namespace fs_session;
int main()
{
    HostPreferences saved;saved.options.chrome=1;saved.options.chatDetached=true;
    saved.options.standby[2]=Mode::Economy;saved.presentation.volume=63;saved.experience.theme=1;
    saved.chat.profileNames[2]="Sodie";saved.chat.showMask&=~CategoryGroup;
    auto bytes=saved.encode();HostPreferences restored;
    assert(!bytes.empty() && HostPreferences::decode(bytes,restored));
    assert(restored.options.chatDetached && restored.options.standby[2]==Mode::Economy);
    assert(restored.presentation.volume==63 && restored.experience.theme==1);
    assert(restored.chat.profileNames[2]=="Sodie" && !(restored.chat.showMask&CategoryGroup));
    for(std::size_t i=0;i<bytes.size();++i)
    {
        HostPreferences original=restored;
        assert(!HostPreferences::decode(bytes.substr(0,i),original));
        assert(original.encode()==restored.encode());
    }
    assert(!HostPreferences::decode(bytes+"x",restored));
    auto oversized=bytes;oversized[4]=static_cast<char>(255);oversized[5]=static_cast<char>(255);oversized[6]=static_cast<char>(255);
    assert(!HostPreferences::decode(oversized,restored));
    auto invalid=saved;invalid.presentation.shortcuts=true;invalid.experience.keys[0]=invalid.presentation.bindings[0];
    assert(invalid.encode().empty());
    invalid=saved;invalid.chat.profileNames[0]=std::string(65,'x');assert(invalid.encode().empty());
    ChoiceAutosave autosave;autosave.committed(bytes);autosave.observe(bytes,100);
    assert(!autosave.dirty() && !autosave.due(10000));
    auto edited=saved;edited.chat.profileNames[2]="New name";const auto changed=edited.encode();
    autosave.observe(changed,1000);assert(autosave.dirty() && !autosave.due(1749) && autosave.due(1750));
    autosave.failed(1750);assert(autosave.dirty() && !autosave.due(31749) && autosave.due(31750));
    autosave.committed(changed);assert(!autosave.dirty());
    autosave.observe(bytes,32000);autosave.observe(changed,32001);assert(!autosave.dirty());
    autosave.observe(bytes,33000);autosave.observe(edited.encode()+"movement",33300);
    assert(!autosave.due(34049) && autosave.due(34050));
    std::cout<<"Bounded transactional preferences and autosave debounce/retry passed.\n";
}
