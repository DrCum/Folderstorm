/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_THEME_STATE_H
#define FS_HOST_THEME_STATE_H
#include "host_theme.h"

namespace fs_host_theme
{
struct Available {Choice choice;std::string name,version;};
std::vector<Available> available(const std::filesystem::path& library);
struct State
{
    Selection selected;
    std::optional<Captured> loaded;
    std::string diagnostic;
};
// Loading failure preserves saved identities and the rollback slot.
State restore(const Selection& selection,const std::filesystem::path& library,const std::filesystem::path& base);
// Prepare only; the owner commits this together with its other preferences.
State prepare(const State& before,const std::optional<Choice>& next,const std::filesystem::path& library,const std::filesystem::path& base);
State prepareRollback(const State& before,const std::filesystem::path& library,const std::filesystem::path& base);
}
#endif
