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

class FSPanelPreferenceLocalAssistant : public LLPanelPreference
{
public:
    ~FSPanelPreferenceLocalAssistant() override;
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;
    void onVisibilityChange(bool visible) override;
    void saveSettings() override;
    void cancel(const std::vector<std::string> settings_to_skip = {}) override;

private:
    struct LocalAssistantRow { const char* key; const char* widget; const char* fallback; bool ask; };
    static const LocalAssistantRow kLocalAssistantRows[9];
    static bool levelAllowed(const LocalAssistantRow& row, const std::string& level);
    void refreshLocalAssistantControls();
    void refreshLocalAssistantStatus();
    void onLocalAssistantToggled();
    void onLocalAssistantPermission(const char* key);
    void snapshotLocalAssistant();
    void restoreSavedControl(const char* name, const LLSD& saved, const LLSD& runtime, bool hadUnsaved);
    void restoreLocalAssistant();
    void refreshConfiguration();
    void copyConfiguration();
    void checkConnection();
    void pollConnection();
    void stopConnection();
    std::string sidecarPath() const;

    bool mLocalAssistantSnapshotted = false;
    bool mBridgeRuntime = false;
    bool mBridgeSaved = false;
    bool mBridgeUnsaved = false;
    LLSD mPermsRuntime;
    LLSD mPermsSaved;
    bool mPermsUnsaved = false;
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
