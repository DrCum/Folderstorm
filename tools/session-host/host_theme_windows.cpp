/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "host_theme_windows.h"
#include "fsthemepackage.h"
#include <shlobj.h>
namespace fs_host_theme
{
std::filesystem::path libraryRoot()
{
    PWSTR value=nullptr;const auto status=SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&value);
    if(FAILED(status) || !value){if(value)CoTaskMemFree(value);throw fs_theme::Error("host_selection","Windows user theme library is unavailable");}
    const auto folder=std::filesystem::path(value)/L"Folderstorm"/L"theme-library";CoTaskMemFree(value);return folder;
}
}
