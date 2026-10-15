/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_PREFERENCES_LISTENER_H
#define FS_PREFERENCES_LISTENER_H
#include "fsassistantfeature.h"
#include "lleventapi.h"

class FSPreferencesListener : public LLEventAPI, public FSAssistantFeature
{
public:
    FSPreferencesListener();
    const char* assistantCapability() const override { return "preferences_tools"; }
    LLSD assistantLimits() const override;
    bool prepareAssistant(FSAssistantPreparedOperation& operation, std::string& error) const override;
    bool validateAssistant(const FSAssistantPreparedOperation& operation, std::string& error) const override;
private:
    void dispatch(const LLSD& request);
};
void fs_initialize_preferences_api();
void fs_preferences_tick();
bool fs_preferences_busy();
// Call at the start of native onOpen(), before panels/saveSettings snapshot.
void fs_preferences_native_opening();
void fs_preferences_native_opened();
bool fs_preferences_native_opening_in_progress();
class FSPreferencesNativeOpening
{
public:
    FSPreferencesNativeOpening() { fs_preferences_native_opening(); }
    ~FSPreferencesNativeOpening() { fs_preferences_native_opened(); }
    FSPreferencesNativeOpening(const FSPreferencesNativeOpening&) = delete;
    FSPreferencesNativeOpening& operator=(const FSPreferencesNativeOpening&) = delete;
};
// The two native preset invalidation callbacks use this only in our write scope.
// True means the callback's metadata write has been handled; normal UI writes
// retain their native saved-layer behavior outside that scope.
bool fs_preferences_preset_write(const std::string& control, const std::string& value);
// Native preset-control changes invalidate a preview's dependent label even
// when it is already runtime-empty. Returns true only when a camera preview
// hid a nonempty native label and normal native invalidation must still run.
bool fs_preferences_preset_invalidated(bool camera);
#endif
