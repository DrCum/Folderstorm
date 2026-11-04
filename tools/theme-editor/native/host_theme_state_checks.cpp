/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_theme_state.h"
#include "fssessionpreferences.h"
#include <iostream>
#include <algorithm>

namespace
{
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
}
int main(int count,char** args)
{
    try
    {
        check(count==5,"Pass library root, base root, theme id and revision");
        const auto library=fs_theme::fromUtf8(args[1]),base=fs_theme::fromUtf8(args[2]);const fs_host_theme::Choice choice{args[3],args[4]};
        const auto catalog=fs_host_theme::available(library);
        check(std::any_of(catalog.begin(),catalog.end(),[&](const auto& item){return item.choice==choice && !item.name.empty();}),"Library catalog missed a valid host revision");
        const auto builtin=fs_host_theme::restore({},library,base);const auto prepared=fs_host_theme::prepare(builtin,choice,library,base);
        check(builtin.selected.empty() && !builtin.loaded && prepared.loaded && prepared.loaded->choice==choice,"Preview preparation changed the accepted state or lost capture");
        fs_session::HostPreferences saved;saved.experience.theme=2;saved.presentation.volume=63;saved.chat.profileNames[1]="Keep this name";
        const auto unchanged=saved.encode();auto draft=saved;draft.hostTheme=prepared.selected;
        check(saved.encode()==unchanged && draft.encode().substr(0,4)=="FHP2","Palette preview changed accepted preference bytes");
        check(fs_session::HostPreferences::decode(draft.encode(),saved) && saved.hostTheme.current==choice && saved.experience.theme==2 && saved.presentation.volume==63 && saved.chat.profileNames[1]=="Keep this name","Applying/reopening lost palette, built-in index or unrelated settings");
        const auto reset=fs_host_theme::prepare(prepared,std::nullopt,library,base),restored=fs_host_theme::prepareRollback(reset,library,base);
        check(!reset.loaded && !reset.selected.current && reset.selected.previous==choice && restored.loaded && restored.selected.current==choice && !restored.selected.previous,"Reset/rollback did not restore captured or built-in choice");
        auto missing=choice;missing.revision=std::string(64,'0');fs_host_theme::Selection broken{missing,choice};
        const auto fallback=fs_host_theme::restore(broken,library,base);
        check(!fallback.loaded && !fallback.diagnostic.empty() && fallback.selected.encode()==broken.encode(),"Missing selection did not preserve both saved identities");
        check(fs_host_theme::prepare(fallback,missing,library,base).selected.encode()==broken.encode(),"Unrelated Preferences Apply discarded a missing selection");
        const auto recovered=fs_host_theme::prepareRollback(fallback,library,base);
        check(recovered.loaded && recovered.selected.current==choice && recovered.selected.previous==missing,"Rollback from failed startup lost its recoverable reference");
        try{fs_host_theme::prepare(prepared,missing,library,base);throw std::runtime_error("Invalid new palette was accepted");}catch(const fs_theme::Error&){}
        check(prepared.selected.current==choice && prepared.loaded->choice==choice,"Failed new selection changed accepted state");
        std::cout<<"PASS host catalog/capture, pure preview/Cancel, transactional preferences Apply/reopen, reset/rollback and recoverable startup fallback\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
