/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_THEME_VIEWER_H
#define FS_THEME_VIEWER_H
#include <memory>
#include <string>
#include <utility>
#include <vector>
namespace FSThemeViewer
{
struct Preferences;
struct PreferenceOption {std::string token,label;};
struct PreferenceCommit {bool changed=false,preservePersonal=false;};
std::shared_ptr<Preferences> preferences(const std::string& skin,const std::string& scheme);
std::vector<PreferenceOption> preferenceOptions(const Preferences& state);
std::string preferenceToken(const Preferences& state);
std::string preferenceDiagnostic(const Preferences& state);
std::pair<std::string,std::string> preferenceBase(const Preferences& state);
bool preferenceCustom(const Preferences& state);
bool preferenceCanRollback(const Preferences& state);
void preferenceSelect(Preferences& state,const std::string& token);
void preferenceBuiltin(Preferences& state,const std::string& skin,const std::string& scheme);
void preferenceRollback(Preferences& state);
void preferenceRefresh(Preferences& state);
void preferenceCancel(Preferences& state);
PreferenceCommit preferenceCommit(Preferences& state);
bool preservePersonalColors();
bool customThemeActive();
bool configure();
bool preview();
bool idlePreview();
void captureFrame();
void openEditor(bool library=false);
}
#endif
