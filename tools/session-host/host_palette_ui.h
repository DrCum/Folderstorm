/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_PALETTE_UI_H
#define FS_HOST_PALETTE_UI_H
#include "host_theme.h"
#include "theme.h"

namespace fs_host_theme
{
inline fs_host_ui::Palette uiPalette(const fs_theme::HostPalette& palette)
{
    const auto color=[&](const char* role)
    {
        const auto value=palette.colors.at(role);
        return RGB((value>>16)&255,(value>>8)&255,value&255);
    };
    return {color("background"),color("panel"),color("input"),color("text"),color("muted"),color("accent"),color("border")};
}
}
#endif
