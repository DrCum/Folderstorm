/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsinstallpolicy.h"
#include <cassert>
#include <string>
int main()
{
    using namespace fs_install;
    for(std::uint32_t choices=0;choices<=AllShortcuts;++choices)
    {
        const auto saved=decode(encode(choices));assert(saved&&*saved==choices);
        for(unsigned bit=0;bit<4;++bit)assert(((*saved>>bit)&1)==((choices>>bit)&1));
    }
    for(const auto invalid:{0u,15u,SettingsVersion+16,0xffffffffu})assert(!decode(invalid));
    assert(action(true,false,false)==LinkAction::Write);
    assert(action(true,true,true)==LinkAction::Write);
    assert(action(false,true,true)==LinkAction::Remove);
    assert(action(false,false,false)==LinkAction::Keep);
    assert(action(true,true,false)==LinkAction::Conflict);
    assert(action(false,true,false)==LinkAction::Keep);
    assert(legacyRegistrationOwned(L"VlpkFolderstormOSNightly",true));
    assert(legacyRegistrationOwned(L"VLPKFOLDERSTORMNightly",true));
    assert(!legacyRegistrationOwned(L"VlpkFolderstormOSNightly",false));
    assert(!legacyRegistrationOwned(L"OtherApp",true));
    assert(!legacyRegistrationOwned(L"Folderstorm.opensim.nightly.x86_64.avx2",true));
    assert(simpleExeName("FolderstormOS-Nightly.exe"));
    assert(simpleExeName("folderstorm-session-host.exe"));
    for(const auto* invalid:{"", ".exe", "../viewer.exe", "C:viewer.exe", "sub\\viewer.exe", "viewer.exe --arg", "viewer.EXE"})assert(!simpleExeName(invalid));
    assert(!simpleExeName(std::string(185,'a')+".exe"));
}
