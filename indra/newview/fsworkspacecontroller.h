/**
 * @file fsworkspacecontroller.h
 * @brief Account-local manual workspace preview transaction
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 The Phoenix Firestorm Project, Inc.
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
#ifndef FSWORKSPACECONTROLLER_H
#define FSWORKSPACECONTROLLER_H

#include "fsworkspacelayout.h"
#include "llhandle.h"
#include "llfloater.h"
#include "llsd.h"
#include "lluuid.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class LLFloater;
class LLView;

// Main-thread only. Profiles are pending until explicit Preferences OK.
// Generic Preferences apply/saveSettings calls are baseline capture only.
class FSWorkspaceController
{
public:
    static FSWorkspaceController& instance();
    bool available() const;
    void beginPreferencesSession();
    void commitPendingSettings();
    void cancelPreferencesSession();
    bool preview(const std::string& id);
    bool saveCurrent(const std::string& name, bool overwrite = false);
    bool rename(const std::string& old_name, const std::string& new_name);
    bool remove(const std::string& name);
    bool addInventoryWindow();
    std::vector<std::string> names() const;
    bool isCustom(const std::string& id) const;
    const std::string& activeId() const { return mActive; }
    unsigned long revision() const { return mRevision; }
    const std::string& status() const { return mStatus; }
    int appliedCount() const { return mApplied; }
    int adjustedCount() const { return mAdjusted; }
    int skippedCount() const { return mSkipped; }
    bool modified() const;

private:
    using Role = FSWorkspaceLayout::Role;
    struct ControlBaseline { bool existed = false; bool unsaved = false; LLSD saved, effective; };
    struct RuntimeBaseline
    {
        LLHandle<LLFloater> handle;
        FSWorkspaceLayout::Window window;
        bool existed = false;
        LLFloater::WorkspacePositioning positioning{};
    };
    FSWorkspaceLayout::Workspace capture() const;
    FSWorkspaceLayout::Workspace startingArrangement(const std::string& id) const;
    void rememberRole(Role role);
    void rememberControls(Role role);
    void placePending(unsigned long generation, const LLUUID& account, const LLUUID& session);
    void abandon();
    void finishPlacement();
    bool startPreview(const FSWorkspaceLayout::Workspace& workspace, const std::string& id);
    bool sameSession() const;

    bool mTransaction = false;
    bool mPendingPlacement = false;
    unsigned long mRevision = 0;
    unsigned long mGeneration = 0;
    LLUUID mAccount, mSession;
    LLSD mProfiles;
    std::string mActive;
    std::string mStatus = "ready";
    FSWorkspaceLayout::Workspace mBaseline, mPending;
    std::map<Role, RuntimeBaseline> mRuntime;
    std::map<std::string, ControlBaseline> mControls;
    std::map<Role, LLHandle<LLFloater>> mPendingHandles;
    std::set<Role> mTouched;
    std::set<Role> mCreated;
    LLHandle<LLView> mFocus;
    bool mChatSkipped = false;
    bool mDependentSkipped = false;
    bool mInboxTouched = false;
    bool mInboxBaselineValid = false;
    bool mInboxBaselineExpanded = false;
    S32 mInboxBaselineHeight = 0;
    int mApplied = 0, mAdjusted = 0, mSkipped = 0;
};
#endif
