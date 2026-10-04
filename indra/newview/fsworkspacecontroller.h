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
#include "lltimer.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class LLFloater;
class LLView;

// Main-thread only. Preferences previews require explicit OK. Quick switching
// accepts only an existing workspace and is blocked while Preferences is open.
class FSWorkspaceController
{
public:
    static FSWorkspaceController& instance();
    bool available() const;
    void beginPreferencesSession();
    void commitPendingSettings();
    void cancelPreferencesSession();
    bool preview(const std::string& id);
    bool canQuickSwitch() const;
    bool quickSwitchWorkspace(const std::string& id);
    bool quickSwitchLayout(const std::string& id);
    using Utility = std::pair<LLHandle<LLFloater>, std::string>;
    std::vector<Utility> utilityWindows() const;
    bool arrange(const std::vector<LLHandle<LLFloater>>& selection, int operation);
    static bool snap(LLFloater* floater, S32& edge, LLView::ESnapEdge snap_edge);
    void scheduleStartupRestore();
    void saveLastArrangement();
    bool hasPrevious() const;
    bool returnPrevious();
    bool saveCurrentNow(const std::string& name, bool remember_folders = false, int components = FSWorkspaceLayout::All, bool remember_toolbars = false);
    bool saveCurrent(const std::string& name, bool overwrite = false, bool remember_folders = false, int components = FSWorkspaceLayout::All, bool remember_toolbars = false);
    bool remembersInventoryFolders(const std::string& name) const;
    bool rename(const std::string& old_name, const std::string& new_name);
    bool remove(const std::string& name);
    bool addInventoryWindow();
    std::vector<std::string> names() const;
    bool isCustom(const std::string& id) const;
    std::string activeId() const;
    unsigned long revision() const { return mRevision; }
    const std::string& status() const { return mStatus; }
    int appliedCount() const { return mApplied; }
    int adjustedCount() const { return mAdjusted; }
    int skippedCount() const { return mSkipped; }
    bool modified() const;
    bool readProfile(const std::string& id, FSWorkspaceLayout::Workspace& workspace) const;
    void requestUpdateCurrent();

private:
    using Role = FSWorkspaceLayout::Role;
    struct ControlBaseline { bool existed = false; bool unsaved = false; LLSD saved, effective; };
    struct RuntimeBaseline
    {
        LLHandle<LLFloater> handle;
        FSWorkspaceLayout::Window window;
        bool existed = false;
        LLSD folder_state; // Live rollback only; never serialized.
        bool folder_touched = false;
        LLFloater::WorkspacePositioning positioning{};
    };
    struct ExtraInventoryBaseline
    {
        RuntimeBaseline state;
        std::string registry;
        LLSD key;
        bool touched = false;
    };
    struct PendingExtraInventory
    {
        LLHandle<LLFloater> handle;
        FSWorkspaceLayout::Window window;
    };
    FSWorkspaceLayout::Workspace capture(bool remember_folders = false, int components = FSWorkspaceLayout::All, bool remember_toolbars = false) const;
    void applyToolbars(const FSWorkspaceLayout::Workspace& workspace);
    bool mToolbarTouched = false;
    void applyInventoryFolder(LLFloater* floater, const FSWorkspaceLayout::Window& saved, RuntimeBaseline& baseline);
    void rememberRole(Role role);
    void rememberControls(Role role);
    void rememberExtraInventory(LLFloater* floater, bool created = false);
    void rememberExtraInventoryControls(const std::string& registry, const LLSD& key = LLSD());
    void restoreExtraInventories();
    bool placeWindow(LLFloater* floater, const FSWorkspaceLayout::Window& saved,
                     bool primary_inventory, std::vector<LLRect>& placed);
    void placePending(unsigned long generation, const LLUUID& account, const LLUUID& session);
    void abandon();
    void finishPlacement();
    void finishQuickSwitch();
    bool startPreview(const FSWorkspaceLayout::Workspace& workspace, const std::string& id);
    bool sameSession() const;

    static void lifecycleIdle(void*);
    void rememberPrevious();
    LLTimer mLifecycleTimer, mStartupTimer;
    bool mStartupScheduled = false;
    unsigned long mStartupGeneration = 0;
    LLUUID mStartupAccount, mStartupSession;
    FSWorkspaceLayout::Workspace mPrevious;
    std::string mPreviousWorkspace, mPreviousLayout, mRestoredLayout;
    LLUUID mPreviousAccount, mPreviousSession;
    bool mHasPrevious = false, mRestoreLayout = false;

    bool mTransaction = false;
    bool mPendingPlacement = false;
    bool mQuickSwitch = false;
    unsigned long mRevision = 0;
    unsigned long mGeneration = 0;
    LLUUID mAccount, mSession;
    LLSD mProfiles;
    std::string mActive;
    std::string mStatus = "ready";
    FSWorkspaceLayout::Workspace mBaseline, mPending, mExpected;
    LLSD mExpectedDefinition;
    std::string mExpectedId;
    LLUUID mExpectedAccount, mExpectedSession;
    mutable LLTimer mComparisonTimer;
    mutable bool mModifiedCache = false;
    std::vector<std::pair<std::string, std::string>> mRenamed;
    std::map<Role, RuntimeBaseline> mRuntime;
    std::map<std::string, ControlBaseline> mControls;
    std::map<std::string, ControlBaseline> mExtraControls;
    std::vector<ExtraInventoryBaseline> mExtraRuntime;
    std::vector<PendingExtraInventory> mPendingExtraInventory;
    std::map<Role, LLHandle<LLFloater>> mPendingHandles;
    std::set<Role> mTouched;
    std::set<Role> mCreated;
    LLHandle<LLView> mFocus;
    bool mFolderSkipped = false;
    bool mChatSkipped = false;
    bool mDependentSkipped = false;
    bool mInboxTouched = false;
    bool mInboxBaselineValid = false;
    bool mInboxBaselineExpanded = false;
    S32 mInboxBaselineHeight = 0;
    int mApplied = 0, mAdjusted = 0, mSkipped = 0;
};
#endif
