/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_theme_state.h"
#include <algorithm>
#include <tuple>

namespace fs_host_theme
{
std::vector<Available> available(const std::filesystem::path& library)
{
    namespace fs=std::filesystem;std::vector<Available> result;std::error_code error;
    if(!fs::is_directory(library,error))return result;
    fs_theme::containedPath(library,"theme-library-check");
    unsigned int scanned=0;std::uint64_t bytes=0;
    for(fs::directory_iterator themes(library,error),end;!error && themes!=end && scanned<4096;themes.increment(error))
    {
        ++scanned;const auto id=fs_theme::toUtf8(themes->path().filename());if(!Choice{id,std::string(64,'0')}.valid())continue;
        std::error_code itemError;fs::path folder;
        try{folder=fs_theme::containedPath(library,id);}catch(const fs_theme::Error&){continue;}
        if(!fs::is_directory(folder,itemError))continue;
        for(fs::directory_iterator revisions(folder,itemError),last;!itemError && revisions!=last && scanned<4096;revisions.increment(itemError))
        {
            ++scanned;const auto revision=fs_theme::toUtf8(revisions->path().filename());const Choice choice{id,revision};if(!choice.valid())continue;
            try
            {
                const auto selected=fs_theme::containedPath(library,id+"/"+revision),file=fs_theme::containedPath(selected,"theme.json");
                const auto size=fs::file_size(file,itemError);if(itemError){itemError.clear();continue;}
                if(size>fs_theme::ManifestLimit || bytes+size>16u*1024u*1024u)continue;bytes+=size;
                const auto data=fs_theme::readFile(file);const auto header=fs_theme::manifest(data);
                if(header.id!=id || fs_theme::hash(data)!=revision)continue;
                if(std::none_of(header.files.begin(),header.files.end(),[](const auto& payload){return payload.path=="host/palette.json" && payload.kind=="host_palette";}))continue;
                result.push_back({choice,header.name,header.version});if(result.size()==256)break;
            }
            catch(const fs_theme::Error&){}
        }
        if(result.size()==256)break;
    }
    std::sort(result.begin(),result.end(),[](const auto& first,const auto& second){return std::tie(first.name,first.version,first.choice.revision)<std::tie(second.name,second.version,second.choice.revision);});return result;
}
State restore(const Selection& selection,const std::filesystem::path& library,const std::filesystem::path& base)
{
    fs_theme::require(!selection.encode().empty(),"host_selection","Invalid saved host selection");State result{selection,{},{}};
    if(selection.current)try{result.loaded=capture(*selection.current,library,base);}catch(const std::exception& error){result.diagnostic=error.what();}
    return result;
}
State prepare(const State& before,const std::optional<Choice>& next,const std::filesystem::path& library,const std::filesystem::path& base)
{
    const auto selected=choose(before.selected,next);
    if(before.selected.current==next)return before;
    State result{selected,{},{}};if(next)result.loaded=capture(*next,library,base);return result;
}
State prepareRollback(const State& before,const std::filesystem::path& library,const std::filesystem::path& base)
{
    State result{rollback(before.selected),{},{}};if(result.selected.current)result.loaded=capture(*result.selected.current,library,base);return result;
}
}
