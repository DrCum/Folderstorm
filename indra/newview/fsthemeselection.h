/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_THEME_SELECTION_H
#define FS_THEME_SELECTION_H
#include "fsthemepackage.h"
#include <optional>

namespace fs_viewer_theme
{
struct Reference
{
    std::string id,revision;
    bool valid() const;
    bool operator==(const Reference& other) const {return id==other.id && revision==other.revision;}
    bool operator!=(const Reference& other) const {return !(*this==other);}
};
struct Choice
{
    std::string skin,scheme;
    std::optional<Reference> custom;
    std::string colorsSnapshot;
};
bool sameChoice(const Choice& first,const Choice& second);
struct Selection {Choice current;std::optional<Choice> previous;};
Selection selection(const std::string& bytes,const Choice& builtin);
std::string selectionBytes(const Selection& selected);
struct Available {Reference reference;std::string name,version,skin,scheme;};
std::vector<Available> available(const std::filesystem::path& library);
struct State
{
    std::filesystem::path profile;
    Selection accepted;
    std::string bytes,diagnostic;
    bool exists=false,usable=true;
};
State read(const std::filesystem::path& profile,const Choice& builtin);
struct Prepared {Selection selected;bool changed=false,resetInvalid=false;};
// Draft preparation never writes profile files or changes the running revision.
Prepared prepare(const State& before,Choice next,const std::filesystem::path& library,const std::filesystem::path& base,bool resetInvalid=false);
Prepared prepareRollback(const State& before,const std::filesystem::path& library,const std::filesystem::path& base);
Prepared recoverStartup(const State& before,const Choice& builtin,const std::optional<Choice>& lastGood,const std::filesystem::path& library,const std::filesystem::path& base);
// Revalidate, acquire the profile lock and compare accepted bytes before replacing.
State commit(const State& before,const Prepared& next,const std::filesystem::path& library,const std::filesystem::path& base);
}
#endif
