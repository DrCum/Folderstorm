/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_THEME_PALETTE_H
#define FS_HOST_THEME_PALETTE_H
#include "fsthemepackage.h"
#include <optional>

namespace fs_host_theme
{
struct Choice {std::string id,revision;};
struct Captured {Choice choice;fs_theme::HostPalette palette;};
struct Selection {std::optional<Choice> current,previous;};
Selection selection(const std::string& bytes);
std::string selectionBytes(const Selection& selected);
Selection choose(const Selection& before,const std::optional<Choice>& next);
Selection rollback(const Selection& before);
// Immutable selection identity, independent of account/viewer-profile settings.
Captured capture(const Choice& choice,const std::filesystem::path& library,const std::filesystem::path& base);
}
#endif
