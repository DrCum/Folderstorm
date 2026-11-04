/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_theme.h"
#include <iostream>
#include <functional>

namespace
{
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void reject(const std::function<void()>& action)
{try{action();}catch(const fs_theme::Error&){return;}throw std::runtime_error("Invalid host choice/selection was accepted");}
}
int main(int count,char** args)
{
    try
    {
        check(count==5,"Pass library root, base root, theme id and revision");
        const fs_host_theme::Choice choice{args[3],args[4]};
        const auto captured=fs_host_theme::capture(choice,fs_theme::fromUtf8(args[1]),fs_theme::fromUtf8(args[2]));
        check(captured.palette.colors.size()==7 && captured.choice.revision==choice.revision,"Captured host palette changed identity");
        const auto active=fs_host_theme::choose({},choice);const auto bytes=fs_host_theme::selectionBytes(active);
        check(fs_host_theme::selectionBytes(fs_host_theme::selection(bytes))==bytes,"Selection round trip changed captured identity");
        const auto reset=fs_host_theme::choose(active,std::nullopt);
        check(!reset.current && reset.previous && reset.previous->revision==choice.revision,"Reset did not preserve rollback revision");
        check(fs_host_theme::selectionBytes(fs_host_theme::choose(reset,std::nullopt))==fs_host_theme::selectionBytes(reset),"Equivalent Reset discarded rollback slot");
        const auto restored=fs_host_theme::rollback(reset);
        check(fs_host_theme::selectionBytes(restored)==bytes && fs_host_theme::selectionBytes(fs_host_theme::rollback(restored))==fs_host_theme::selectionBytes(reset),"Rollback/roll-forward lost built-in or custom choice");
        check(fs_host_theme::selectionBytes(fs_host_theme::choose(restored,choice))==bytes,"Equivalent custom choice changed history");
        for(const auto* invalid:{"{}","null","[]","{\"format_version\":2,\"current\":null,\"previous\":null}","{\"format_version\":1.0,\"current\":null,\"previous\":null}","{\"format_version\":1,\"current\":false,\"previous\":null}","{\"format_version\":1,\"current\":{},\"previous\":null}","{\"format_version\":1,\"current\":null,\"previous\":null,\"extra\":true}","{\"format_version\":1,\"current\":null,\"current\":null,\"previous\":null}"})
            reject([&]{fs_host_theme::selection(invalid);});
        reject([&]{fs_host_theme::selection(std::string(4097,' '));});
        for(const auto& invalid:{fs_host_theme::Choice{"../outside",choice.revision},fs_host_theme::Choice{choice.id,"../outside"},fs_host_theme::Choice{choice.id,std::string(64,'A')}})
        {reject([&]{fs_host_theme::choose(active,invalid);});reject([&]{fs_host_theme::capture(invalid,fs_theme::fromUtf8(args[1]),fs_theme::fromUtf8(args[2]));});}
        auto missing=choice;missing.revision=std::string(64,'0');reject([&]{fs_host_theme::capture(missing,fs_theme::fromUtf8(args[1]),fs_theme::fromUtf8(args[2]));});
        std::cout<<"PASS immutable host capture; strict selection JSON/identity/bounds; no-op, Reset, rollback/roll-forward; invalid/missing choice rejection\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
