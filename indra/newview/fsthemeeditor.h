/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_THEME_EDITOR_LAUNCH_H
#define FS_THEME_EDITOR_LAUNCH_H
#include <filesystem>
#include <string>
namespace fs_theme
{
struct EditorLaunch {std::filesystem::path executable;std::wstring arguments;};
std::wstring quoteEditorArgument(const std::wstring& value);
EditorLaunch editorLaunch(const std::filesystem::path& base,const std::filesystem::path& viewer,const std::filesystem::path& host,bool library);
bool launchEditor(const std::filesystem::path& base,const std::filesystem::path& viewer,const std::filesystem::path& host,bool library,std::string& diagnostic);
}
#endif
