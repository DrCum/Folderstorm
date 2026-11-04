/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsthemeselection.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <tuple>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs_viewer_theme
{
namespace fs=std::filesystem;
namespace
{
using Object=boost::json::object;
void folder(const std::string& value,bool empty=false)
{
    if(empty && value.empty())return;
    fs_theme::pathName(value);fs_theme::require(value.size()<=64 && value.find('/')==std::string::npos,"viewer_selection","Invalid base folder");
}
void validate(const Choice& choice)
{
    folder(choice.skin);folder(choice.scheme,true);
    fs_theme::require(!choice.custom || choice.custom->valid(),"viewer_selection","Invalid theme identity");
    fs_theme::require(choice.colorsSnapshot.empty() || fs_theme::validHash(choice.colorsSnapshot),"viewer_selection","Invalid colors backup identity");
}
std::string text(const Object& object,const char* field)
{
    const auto* value=object.if_contains(field);fs_theme::require(value && value->is_string(),"viewer_selection",field);return std::string(value->as_string());
}
Object choiceJson(const Choice& choice)
{
    validate(choice);return {{"theme_id",choice.custom?choice.custom->id:""},{"revision",choice.custom?choice.custom->revision:""},
        {"base_skin",choice.skin},{"base_scheme",choice.scheme},{"colors_snapshot",choice.colorsSnapshot}};
}
Choice readChoice(const Object& object,bool top=false)
{
    fs_theme::require(object.size()==(top?7u:5u),"viewer_selection","Unexpected selection fields");
    Choice result{text(object,"base_skin"),text(object,"base_scheme"),{},text(object,"colors_snapshot")};
    const auto id=text(object,"theme_id"),revision=text(object,"revision");
    fs_theme::require(id.empty()==revision.empty(),"viewer_selection","Incomplete theme identity");
    if(!id.empty())result.custom=Reference{id,revision};validate(result);return result;
}
Choice capture(Choice next,const fs::path& library,const fs::path& base)
{
    validate(next);
    if(next.custom)
    {
        const auto loaded=fs_theme::load(fs_theme::containedPath(library,next.custom->id+"/"+next.custom->revision),base);
        fs_theme::require(loaded.manifest.id==next.custom->id && loaded.revision==next.custom->revision,"viewer_selection","Selected revision identity differs");
        next.skin=loaded.manifest.skin;next.scheme=loaded.manifest.scheme;
    }
    else
    {
        auto relative="skins/"+next.skin;if(!next.scheme.empty())relative+="/themes/"+next.scheme;
        fs_theme::require(fs::is_directory(fs_theme::containedPath(base,relative)),"viewer_selection","The selected built-in base is unavailable");
    }
    return next;
}
void atomicFile(const fs::path& profile,const std::string& relative,const std::string& bytes)
{
    const auto destination=fs_theme::containedPath(profile,relative);
    static std::atomic<unsigned long long> counter{0};
    const auto nonce=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++counter);
    const auto temporary=fs_theme::containedPath(profile,"custom-theme-write-"+nonce+".tmp");
    fs_theme::require(!fs::exists(temporary),"viewer_selection","Selection write is already staged");
    try
    {
        {std::ofstream output(temporary,std::ios::binary|std::ios::trunc);output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));output.flush();fs_theme::require(static_cast<bool>(output),"viewer_selection","Could not stage theme settings");}
#if defined(_WIN32)
        fs_theme::require(MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"viewer_selection","Could not replace theme settings");
#else
        fs::rename(temporary,destination);
#endif
    }
    catch(...){std::error_code ignored;fs::remove(temporary,ignored);throw;}
}
class ProfileLock
{
#if defined(_WIN32)
    HANDLE file=INVALID_HANDLE_VALUE;
#else
    int file=-1;
#endif
public:
    explicit ProfileLock(const fs::path& profile)
    {
        const auto path=fs_theme::containedPath(profile,"custom-theme.lock");
#if defined(_WIN32)
        file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        fs_theme::require(file!=INVALID_HANDLE_VALUE,"viewer_selection_busy","This profile's theme settings are busy");
#else
        file=open(path.c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);
        if(file<0 || flock(file,LOCK_EX|LOCK_NB)!=0){if(file>=0)close(file);file=-1;throw fs_theme::Error("viewer_selection_busy","This profile's theme settings are busy");}
#endif
    }
    ~ProfileLock()
    {
#if defined(_WIN32)
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);
#else
        if(file>=0)close(file);
#endif
    }
    ProfileLock(const ProfileLock&)=delete;
    ProfileLock& operator=(const ProfileLock&)=delete;
};
std::string backup(const fs::path& profile,const std::string& directory,const std::string& extension,const std::string& bytes)
{
    const auto id=fs_theme::hash(bytes);const auto folderPath=fs_theme::containedPath(profile,directory);fs::create_directory(folderPath);
    const auto relative=directory+"/"+id+extension;const auto path=fs_theme::containedPath(profile,relative);
    if(fs::exists(path))fs_theme::require(fs_theme::readFile(path,fs_theme::ManifestLimit)==bytes,"viewer_selection","A saved theme backup changed");
    else atomicFile(profile,relative,bytes);
    return id;
}
}
bool Reference::valid() const
{
    if(id.size()!=36 || !fs_theme::validHash(revision))return false;
    for(std::size_t i=0;i<id.size();++i)
    {
        const auto ch=id[i];const bool hex=(ch>='0' && ch<='9') || (ch>='a' && ch<='f');
        if((i==8 || i==13 || i==18 || i==23)?ch!='-':!hex)return false;
    }
    return true;
}
bool sameChoice(const Choice& first,const Choice& second)
{return first.skin==second.skin && first.scheme==second.scheme && first.custom==second.custom;}
Selection selection(const std::string& bytes,const Choice& builtin)
{
    validate(builtin);fs_theme::require(!builtin.custom,"viewer_selection","Expected built-in baseline");
    fs_theme::require(bytes.size()<=4096,"size","Viewer selection exceeds4 KiB");
    const auto document=fs_theme::parse(bytes);if(document.is_null())return {builtin,{}};
    fs_theme::require(document.is_object(),"viewer_selection","Expected viewer selection");const auto& object=document.as_object();
    if(!object.if_contains("selection_version"))
    {
        fs_theme::require(object.size()==2,"viewer_selection","Unexpected legacy selection fields");
        Choice current=builtin;const auto id=text(object,"theme_id"),revision=text(object,"revision");
        fs_theme::require(id.empty()==revision.empty(),"viewer_selection","Incomplete legacy identity");
        if(!id.empty())current.custom=Reference{id,revision};validate(current);return {current,{}};
    }
    const auto& version=object.at("selection_version");
    fs_theme::require((version.is_int64() && version.as_int64()==1) || (version.is_uint64() && version.as_uint64()==1),"viewer_selection_version","Unsupported viewer selection version");
    Selection result{readChoice(object,true),{}};const auto* previous=object.if_contains("previous");
    fs_theme::require(previous && (previous->is_object() || previous->is_null()),"viewer_selection","Invalid previous selection");
    if(previous->is_object())result.previous=readChoice(previous->as_object());return result;
}
std::string selectionBytes(const Selection& selected)
{
    auto result=choiceJson(selected.current);result["selection_version"]=1;result["previous"]=selected.previous?boost::json::value(choiceJson(*selected.previous)):boost::json::value(nullptr);
    return boost::json::serialize(result)+"\n";
}
std::vector<Available> available(const fs::path& library)
{
    std::vector<Available> result;std::error_code error;if(!fs::is_directory(library,error))return result;
    fs_theme::containedPath(library,"theme-library-check");unsigned int scanned=0;std::uint64_t bytes=0;
    for(fs::directory_iterator themes(library,error),end;!error && themes!=end && scanned<4096;themes.increment(error))
    {
        ++scanned;const auto id=fs_theme::toUtf8(themes->path().filename());if(!Reference{id,std::string(64,'0')}.valid())continue;
        fs::path folderPath;try{folderPath=fs_theme::containedPath(library,id);}catch(const fs_theme::Error&){continue;}
        std::error_code itemError;if(!fs::is_directory(folderPath,itemError))continue;
        for(fs::directory_iterator revisions(folderPath,itemError),last;!itemError && revisions!=last && scanned<4096;revisions.increment(itemError))
        {
            ++scanned;const auto revision=fs_theme::toUtf8(revisions->path().filename());if(!Reference{id,revision}.valid())continue;
            try
            {
                const auto directory=fs_theme::containedPath(library,id+"/"+revision),file=fs_theme::containedPath(directory,"theme.json");
                const auto size=fs::file_size(file,itemError);if(itemError){itemError.clear();continue;}
                if(size>fs_theme::ManifestLimit || bytes+size>16u*1024u*1024u)continue;bytes+=size;
                const auto data=fs_theme::readFile(file);const auto header=fs_theme::manifest(data);
                if(header.id!=id || fs_theme::hash(data)!=revision)continue;
                result.push_back({{id,revision},header.name,header.version,header.skin,header.scheme});if(result.size()==256)break;
            }
            catch(const fs_theme::Error&){}
        }
        if(result.size()==256)break;
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return std::tie(a.name,a.version,a.reference.revision)<std::tie(b.name,b.version,b.reference.revision);});return result;
}
State read(const fs::path& profile,const Choice& builtin)
{
    Choice baseline=builtin;if(baseline.skin.empty())baseline.skin="default";
    try{validate(baseline);fs_theme::require(!baseline.custom,"viewer_selection","Expected built-in baseline");}
    catch(const fs_theme::Error&){baseline={"default","",{},{}};}
    State result;result.profile=fs::absolute(profile).lexically_normal();result.accepted.current=baseline;
    try
    {
        const auto path=fs_theme::containedPath(result.profile,"custom-theme.json");result.exists=fs::exists(path);
        if(result.exists){result.bytes=fs_theme::readFile(path,fs_theme::ManifestLimit);result.accepted=selection(result.bytes,baseline);}
    }
    catch(const std::exception& error){result.usable=false;result.diagnostic=error.what();}
    return result;
}
Prepared prepare(const State& before,Choice next,const fs::path& library,const fs::path& base,bool resetInvalid)
{
    if(before.usable && sameChoice(before.accepted.current,next))return {before.accepted,false,false};
    fs_theme::require(before.usable || resetInvalid,"viewer_selection",before.diagnostic);
    next=capture(std::move(next),library,base);
    return {{std::move(next),before.accepted.current},true,!before.usable};
}
Prepared prepareRollback(const State& before,const fs::path& library,const fs::path& base)
{
    fs_theme::require(before.usable && before.accepted.previous.has_value(),"viewer_selection","There is no saved previous theme");
    return prepare(before,*before.accepted.previous,library,base);
}
Prepared recoverStartup(const State& before,const Choice& builtin,const std::optional<Choice>& lastGood,const fs::path& library,const fs::path& base)
{
    fs_theme::require(before.usable && before.accepted.current.custom.has_value(),"startup_recovery","Expected a failed custom theme startup");
    for(const auto* candidate:{&before.accepted.previous,&lastGood})
        if(*candidate && !sameChoice(**candidate,before.accepted.current) && (**candidate).custom!=before.accepted.current.custom)
            try{return prepare(before,**candidate,library,base);}catch(const fs_theme::Error&){}
    // Retain the failed identity in previous, so it can be inspected or restored explicitly.
    return prepare(before,builtin,library,base);
}
State commit(const State& before,const Prepared& next,const fs::path& library,const fs::path& base)
{
    if(!next.changed)return before;
    const auto checked=capture(next.selected.current,library,base);
    fs_theme::require(sameChoice(checked,next.selected.current),"viewer_selection","The required base changed before Apply");
    selectionBytes(next.selected);ProfileLock lock(before.profile);
    const auto path=fs_theme::containedPath(before.profile,"custom-theme.json");const bool exists=fs::exists(path);
    fs_theme::require(exists==before.exists && (!exists || fs_theme::readFile(path,fs_theme::ManifestLimit)==before.bytes),"viewer_selection_changed","Theme settings changed outside this Preferences window; reopen it before applying");
    auto saved=next.selected;
    if(next.resetInvalid && exists)backup(before.profile,"custom-theme-selection-backups",".json",before.bytes);
    if(saved.previous && (saved.current.skin!=before.accepted.current.skin || saved.current.scheme!=before.accepted.current.scheme))
    {
        const auto colors=fs_theme::containedPath(before.profile,"colors.xml");
        if(fs::is_regular_file(colors))saved.previous->colorsSnapshot=backup(before.profile,"custom-theme-color-snapshots",".xml",fs_theme::readFile(colors,fs_theme::ManifestLimit));
    }
    const auto bytes=selectionBytes(saved);atomicFile(before.profile,"custom-theme.json",bytes);
    State result=before;result.accepted=std::move(saved);result.bytes=bytes;result.exists=true;result.usable=true;result.diagnostic.clear();return result;
}
}
