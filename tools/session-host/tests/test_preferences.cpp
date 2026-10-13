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
    const AccountKey account{"01234567-89ab-cdef-0123-456789abcdef","secondlife"};
    HostPreferences full;
    for(unsigned int i=0;i<PresentationStore::MaxProfiles;++i)
    {
        const AccountKey key{account.account,"grid-"+std::to_string(i)};
        assert(full.presentation.set(key,{"Saved",0x123456,2}));
        full.chat.sounds[key]={SoundPrivate,{}};
    }
    const auto originalAppearance=full.presentation.appearance(account);
    const auto originalSound=full.chat.sound(account,originalAppearance.alerts);
    AccountPreferenceDraft draft{originalAppearance,originalAppearance,originalSound,originalSound,false};
    full.experience.theme=1;
    // An untouched disconnected/replaced account neither blocks global edits
    // nor inserts default records when both customization stores are full.
    assert(!draft.edited() && draft.apply(full,account,false));
    assert(full.presentation.accounts.size()==32 && !full.presentation.accounts.count(account));
    assert(full.chat.sounds.size()==32 && !full.chat.sounds.count(account));
    assert(!full.encode().empty() && full.experience.theme==1);
    draft.appearance.alias="Builder";
    const auto unchanged=full.encode();
    assert(draft.edited() && !draft.apply(full,account,false) && full.encode()==unchanged);
    assert(!draft.apply(full,account,true) && full.encode()==unchanged);
    draft.appearance=originalAppearance;
    draft.sound.categories=SoundPrivate;
    assert(!draft.apply(full,account,true) && full.encode()==unchanged);
    // Sound-only changes do not consume an appearance record, and vice versa.
    full.chat.sounds.clear();
    assert(draft.apply(full,account,true) && full.chat.sound(account,0).categories==SoundPrivate);
    assert(!full.presentation.accounts.count(account));
    draft.originalSound=draft.sound;
    assert(!draft.edited() && draft.apply(full,account,false)); // Subsequent Apply after logout.
    draft.importedWav=true;assert(draft.soundChanged() && !draft.apply(full,account,false));
    draft.importedWav=false;
    draft.sound.name="New name.wav";assert(draft.soundChanged());draft.sound=draft.originalSound;
    draft.sound.asset=std::string(32,'a')+".wav";assert(draft.soundChanged());draft.sound=draft.originalSound;
    HostPreferences appearanceOnly;
    appearanceOnly.chat.sounds=full.chat.sounds;
    for(unsigned int i=1;appearanceOnly.chat.sounds.size()<32;++i)
        appearanceOnly.chat.sounds[{account.account,"sound-grid-"+std::to_string(i)}]={SoundPrivate,{}};
    const AccountKey newAccount{account.account,"another-account-grid"};
    const auto newAppearance=appearanceOnly.presentation.appearance(newAccount);
    const auto newSound=appearanceOnly.chat.sound(newAccount,newAppearance.alerts);
    AccountPreferenceDraft aliasOnly{newAppearance,newAppearance,newSound,newSound,false};aliasOnly.appearance.alias="Builder";
    assert(aliasOnly.apply(appearanceOnly,newAccount,true));
    assert(appearanceOnly.presentation.appearance(newAccount).alias=="Builder" && appearanceOnly.chat.sounds.size()==32);
    assert(!appearanceOnly.chat.sounds.count(newAccount));
    const AccountKey another{account.account,"yet-another-account-grid"};
    auto combined=aliasOnly;combined.sound.categories=SoundPrivate;
    const auto beforeCombined=appearanceOnly.encode();
    assert(!combined.apply(appearanceOnly,another,true) && appearanceOnly.encode()==beforeCombined);
    // Editing color alone must preserve the account's existing alias and
    // legacy alert policy; a later global Apply must not rewrite the color.
    auto storedAppearance=appearanceOnly.presentation.appearance(newAccount);storedAppearance.alerts=2;
    assert(appearanceOnly.presentation.set(newAccount,storedAppearance));
    auto colorOnly=aliasOnly;colorOnly.originalAppearance=colorOnly.appearance=storedAppearance;colorOnly.appearance.color=0xaabbcc;
    assert(colorOnly.apply(appearanceOnly,newAccount,true));
    const auto recolored=appearanceOnly.presentation.appearance(newAccount);
    assert(recolored.alias=="Builder" && recolored.color==0xaabbcc && recolored.alerts==2);
    colorOnly.originalAppearance=colorOnly.appearance;assert(!colorOnly.edited() && colorOnly.apply(appearanceOnly,newAccount,false));
    draft.originalAppearance.alias="Alice";draft.appearance.alias="Builder";
    assert(draft.profileLabel("Alice","Alice","Alice","Alice")=="Builder");
    assert(draft.profileLabel("","","","Alice")=="Builder");
    assert(draft.profileLabel("Driving","Driving","Driving","Alice")=="Driving");
    assert(draft.profileLabel("Alice","Driving","Driving","Alice")=="Driving");
    assert(draft.profileLabel("Alice","Alice","New login","Alice")=="New login");
    // Repeated Apply/alias changes use the latest accepted alias and label.
    draft.originalAppearance.alias="Builder";draft.appearance.alias="Crafter";
    assert(draft.profileLabel("Builder","Builder","Builder","Alice")=="Crafter");
    draft.appearance.alias.clear();
    assert(draft.profileLabel("Builder","Builder","Builder","Alice")=="Alice");
    draft.originalAppearance.alias.clear();
    assert(draft.profileLabel("Alice","Alice","Alice","Alice")=="Alice");
    std::cout<<"Bounded transactional preferences and autosave debounce/retry passed.\n";
}
