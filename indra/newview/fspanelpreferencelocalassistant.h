/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

/** Local assistant preferences, configuration and bounded connection checks. */
#ifndef FS_PANEL_PREFERENCE_LOCAL_ASSISTANT_H
#define FS_PANEL_PREFERENCE_LOCAL_ASSISTANT_H

#include "llfloaterpreference.h"
#include "llprocess.h"
#include "lltimer.h"
#include "llevents.h"
#include "llcontrol.h"
#include "llsdutil.h"
#include "fsassistantsettings.h"
#include <memory>

class FSPanelPreferenceLocalAssistant : public LLPanelPreference
{
public:
    ~FSPanelPreferenceLocalAssistant() override;
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;
    void onVisibilityChange(bool visible) override;
    void saveSettings() override;
    // Called only by the explicit Preferences OK action, before baseline capture.
    void commitPendingSettings();
    void cancel(const std::vector<std::string> settings_to_skip = {}) override;

private:
    struct EqualSettings
    {
        bool operator()(const LLSD& a, const LLSD& b) const { return llsd_equals(a, b); }
    };
    using AssistantEditor = fs_assistant::SettingsEditor<LLControlVariable, EqualSettings>;
    void refreshLocalAssistantControls();
    void refreshLocalAssistantStatus();
    void refreshPreset();
    void onLocalAssistantToggled();
    void onLocalAssistantPermission(const char* key);
    void onPreset();
    void onSessionScope();
    void onIndividualPermissions();
    void previewSettings(const LLSD& access, const LLSD& permissions);
    bool settingsChangedElsewhere();
    void snapshotLocalAssistant();
    void restoreLocalAssistant();
    void refreshConfiguration();
    void copyConfiguration();
    void checkConnection();
    void pollConnection();
    void stopConnection();
    std::string sidecarPath() const;

    std::unique_ptr<AssistantEditor> mEditor;
    boost::signals2::scoped_connection mAccessConnection;
    boost::signals2::scoped_connection mPermissionsConnection;
    bool mWritingSettings = false;
    bool mIndividualPermissions = false;
    bool mSettingsConflict = false;
    LLProcessPtr mDiagnostic;
    LLTimer mDiagnosticTimer;
    LLTimer mStatusTimer;
    LLTempBoundListener mDiagnosticListener;
    bool mStatusObserved = false;
    bool mLastReady = false;
    int mLastPolicyGeneration = 0;
    std::string mDiagnosticOutput;
    std::string mConfiguration;
};
#endif
