/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_THEME_PREVIEW_H
#define FS_HOST_THEME_PREVIEW_H
#include "host_theme.h"
#include <windows.h>

namespace fs_host_theme
{
struct PreviewRequest
{
    std::string id;
    std::int64_t generation=0;
    std::optional<Choice> choice;
    unsigned int baseline=0;
};
PreviewRequest previewRequest(const std::string& bytes);
// The production host calls this before initializing accounts, profiles or IPC.
std::optional<int> dispatchPreview(HINSTANCE instance,int show);
}
#endif
