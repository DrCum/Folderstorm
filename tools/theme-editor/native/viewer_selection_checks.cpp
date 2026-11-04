/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemeselection.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> void rejects(Action action,const char* message)
{try{action();}catch(const fs_theme::Error&){return;}throw std::runtime_error(message);}
void write(const std::filesystem::path& path,const std::string& bytes)
{std::ofstream output(path,std::ios::binary|std::ios::trunc);output<<bytes;check(static_cast<bool>(output),"Fixture write failed");}
}
int main(int count,char** args)
{
    try
    {
        using namespace fs_viewer_theme;namespace fs=std::filesystem;
        check(count==6,"Pass isolated library, retained base, fixture profile, id and revision");
        const auto library=fs_theme::fromUtf8(args[1]),base=fs_theme::fromUtf8(args[2]),profile=fs_theme::fromUtf8(args[3]);
        const Reference reference{args[4],args[5]};const Choice builtin{"firestorm","dark",{},{}},custom{"ansastorm","folderstorm",reference,{}};
        fs::create_directories(profile);const std::string colors="<colors><color name=\"LinkColor\" value=\"0 1 0 1\"/></colors>\n";
        write(profile/"colors.xml",colors);const auto catalog=available(library);
        check(std::any_of(catalog.begin(),catalog.end(),[&](const auto& row){return row.reference==reference && row.skin=="ansastorm";}),"Catalog omitted a valid viewer revision");
        const auto initial=read(profile,builtin);const auto prepared=prepare(initial,custom,library,base);
        check(read(profile.parent_path()/"blank-baseline",Choice{}).accepted.current.skin=="default","An empty saved base did not use the built-in fallback");
        check(!initial.exists && !initial.accepted.current.custom && prepared.changed && prepared.selected.current.custom==reference && !fs::exists(profile/"custom-theme.json"),"Draft/Cancel changed the accepted profile");
        auto accepted=commit(initial,prepared,library,base);
        check(accepted.accepted.current.custom==reference && accepted.accepted.previous && sameChoice(*accepted.accepted.previous,builtin),"Apply lost the previous built-in base");
        check(fs_theme::readFile(profile/"colors.xml")==colors && !accepted.accepted.previous->colorsSnapshot.empty(),"Base switch lost explicit colors or its backup");
        check(fs_theme::readFile(profile/"custom-theme-color-snapshots"/(accepted.accepted.previous->colorsSnapshot+".xml"))==colors,"Personal colors backup differs");
        const auto reopened=read(profile,builtin);const auto noop=prepare(reopened,custom,library,base);
        check(!noop.changed && commit(reopened,noop,library,base).bytes==accepted.bytes,"Unrelated Apply rewrote the selection or rollback slot");
        const auto recovery=recoverStartup(reopened,builtin,{},library,base);
        check(!recovery.selected.current.custom && sameChoice(recovery.selected.current,builtin) && recovery.selected.previous->custom==reference,"Incomplete-startup recovery did not preserve the failed identity and prior base");
        auto noHistory=reopened;noHistory.accepted.previous.reset();
        const auto fallback=recoverStartup(noHistory,builtin,custom,library,base);
        check(!fallback.selected.current.custom && fallback.selected.previous->custom==reference,"Startup recovery reselected the failed last-good revision");
        auto sameFailed=custom;sameFailed.scheme="";
        check(!recoverStartup(noHistory,builtin,sameFailed,library,base).selected.current.custom,"Startup recovery reselected the failed revision through different base metadata");
        const auto rolled=commit(reopened,prepareRollback(reopened,library,base),library,base);
        check(!rolled.accepted.current.custom && sameChoice(rolled.accepted.current,builtin) && rolled.accepted.previous->custom==reference,"Rollback lost custom identity or original built-in base");
        check(selection(rolled.bytes,Choice{"ansastorm","folderstorm",{},{}}).current.skin=="firestorm","Saved built-in base did not survive restart");
        std::cout<<"PASS catalog, pure draft/Cancel, Apply/reopen/no-op, prior built-in rollback and preserved colors backup\n";

        const auto legacy=selection("{\"theme_id\":\""+reference.id+"\",\"revision\":\""+reference.revision+"\"}",builtin);
        check(legacy.current.custom==reference && !legacy.previous,"Legacy two-field selection did not migrate");
        rejects([&]{selection("{\"selection_version\":2}",builtin);},"Future selection accepted");
        rejects([&]{selection("{\"theme_id\":\"\",\"theme_id\":\"\",\"revision\":\"\"}",builtin);},"Duplicate selection field accepted");
        rejects([&]{selection(std::string(4097,' '),builtin);},"Oversized selection accepted");
        std::cout<<"PASS legacy/null records and strict future, duplicate and size rejection\n";

        auto missing=custom;missing.custom->revision=std::string(64,'0');
        const auto beforeFailure=fs_theme::readFile(profile/"custom-theme.json");
        rejects([&]{prepare(rolled,missing,library,base);},"Missing revision accepted");
        check(fs_theme::readFile(profile/"custom-theme.json")==beforeFailure,"Failed validation changed the accepted bytes");
        const auto pending=prepare(rolled,custom,library,base);
        const auto payload=library/fs_theme::fromUtf8(reference.id)/fs_theme::fromUtf8(reference.revision)/"host/palette.json";
        const auto original=fs_theme::readFile(payload);write(payload,"{}");
        rejects([&]{commit(rolled,pending,library,base);},"Changed package accepted at commit");write(payload,original);
        check(fs_theme::readFile(profile/"custom-theme.json")==beforeFailure,"Package race changed the accepted bytes");
        write(profile/"custom-theme.json","null\n");
        rejects([&]{commit(rolled,pending,library,base);},"External profile edit was overwritten");
        check(fs_theme::readFile(profile/"custom-theme.json")=="null\n","Compare-before-write did not preserve the external edit");
        std::cout<<"PASS invalid revision, package mutation and external profile edits fail before replacement\n";

        write(profile/"custom-theme.json","{\"selection_version\":9}\n");const auto future=read(profile,builtin);
        check(!future.usable && !future.diagnostic.empty(),"Bad saved state was hidden");
        rejects([&]{prepare(future,builtin,library,base);},"Invalid saved state reset without an explicit action");
        const auto reset=commit(future,prepare(future,builtin,library,base,true),library,base);
        check(reset.usable && !reset.accepted.current.custom,"Explicit reset failed");
        check(fs_theme::readFile(profile/"custom-theme-selection-backups"/(fs_theme::hash(future.bytes)+".json"))==future.bytes,"Reset lost the recoverable rejected record");
        const auto other=profile.parent_path()/"other-profile";fs::create_directory(other);
        check(!read(other,builtin).exists && !fs::exists(other/"custom-theme.json"),"Applying one profile changed another");
        std::cout<<"PASS recoverable explicit reset and independent profile records\n";
#if defined(_WIN32)
        const auto lock=CreateFileW((profile/"custom-theme.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(lock!=INVALID_HANDLE_VALUE,"Could not own fixture profile lock");
        try{rejects([&]{commit(reset,prepare(reset,custom,library,base),library,base);},"Busy profile was overwritten");}catch(...){CloseHandle(lock);throw;}
        CloseHandle(lock);check(fs_theme::readFile(profile/"custom-theme.json")==reset.bytes,"Busy profile changed selection bytes");
        std::cout<<"PASS real Windows exclusive profile lock refuses a concurrent writer\n";
#endif
        return 0;
    }
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
