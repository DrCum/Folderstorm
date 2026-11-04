/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_theme.h"
#include <algorithm>

namespace fs_host_theme
{
namespace
{
void validate(const Choice& choice)
{
    fs_theme::require(choice.valid(),"host_selection","Invalid host theme identity");
}
bool equal(const std::optional<Choice>& first,const std::optional<Choice>& second)
{return first && second?first->id==second->id && first->revision==second->revision:!first && !second;}
boost::json::value json(const std::optional<Choice>& choice)
{if(!choice)return nullptr;validate(*choice);return boost::json::object{{"theme_id",choice->id},{"revision",choice->revision}};}
std::optional<Choice> readChoice(const boost::json::value& value)
{
    if(value.is_null())return std::nullopt;
    fs_theme::require(value.is_object(),"host_selection","Host choice must be an identity or null");
    const auto& object=value.as_object();const auto* id=object.if_contains("theme_id");const auto* revision=object.if_contains("revision");
    fs_theme::require(object.size()==2 && id && id->is_string() && revision && revision->is_string(),"host_selection","Invalid host choice fields");
    Choice result{std::string(id->as_string()),std::string(revision->as_string())};validate(result);return result;
}
}
Selection selection(const std::string& bytes)
{
    fs_theme::require(bytes.size()<=4096,"size","Host selection exceeds4 KiB");
    const auto document=fs_theme::parse(bytes);fs_theme::require(document.is_object(),"host_selection","Host selection must be an object");
    const auto& object=document.as_object();const auto* version=object.if_contains("format_version");const auto* current=object.if_contains("current");const auto* previous=object.if_contains("previous");
    fs_theme::require(object.size()==3 && version && current && previous,"host_selection","Invalid host selection fields");
    fs_theme::require((version->is_int64() && version->as_int64()==1) || (version->is_uint64() && version->as_uint64()==1),"host_selection_version","Unsupported host selection format");
    return {readChoice(*current),readChoice(*previous)};
}
std::string selectionBytes(const Selection& selected)
{return boost::json::serialize(boost::json::object{{"format_version",1},{"current",json(selected.current)},{"previous",json(selected.previous)}});}
Selection choose(const Selection& before,const std::optional<Choice>& next)
{
    if(next)validate(*next);
    // Preserve the rollback slot on an equivalent choice.
    return equal(before.current,next)?before:Selection{next,before.current};
}
Selection rollback(const Selection& before){return {before.previous,before.current};}
Captured capture(const Choice& choice,const std::filesystem::path& library,const std::filesystem::path& base)
{
    validate(choice);
    const auto selected=fs_theme::containedPath(library,choice.id+"/"+choice.revision);
    const auto loaded=fs_theme::load(selected,base);
    fs_theme::require(loaded.manifest.id==choice.id && loaded.revision==choice.revision,"host_selection","Host theme identity differs from selected revision");
    const auto file=std::find_if(loaded.manifest.files.begin(),loaded.manifest.files.end(),[](const auto& entry){return entry.path=="host/palette.json" && entry.kind=="host_palette";});
    fs_theme::require(file!=loaded.manifest.files.end(),"host_palette","Selected theme has no host palette");
    const auto bytes=fs_theme::readFile(fs_theme::containedPath(loaded.directory,"host/palette.json"),4096);
    // Recheck the exact bytes consumed, including if a file changed after load.
    fs_theme::require(bytes.size()==file->size && fs_theme::hash(bytes)==file->sha256,"integrity","Host palette changed during capture");
    return {choice,fs_theme::hostPalette(bytes)};
}
}
