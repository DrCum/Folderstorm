/**
 * @file fsworkspacecontroller.cpp
 * @brief Manual workspace adapters and reversible Preferences previews
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
#include "llviewerprecompiledheaders.h"
#include "fsworkspacecontroller.h"
#include "fsworkspacecontextadapter.h"
#include "llpresetsmanager.h"
#include "llfeaturemanager.h"
#include "llfloaterpreference.h"
#include "fschromelayoutcontroller.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "llfocusmgr.h"
#include "llpanelmaininventory.h"
#include "llinventorymodel.h"
#include "llnotificationsutil.h"
#include "llsidepanelinventory.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llagentcamera.h"
#include "llkeyboard.h"
#include "llcallbacklist.h"
#include "lltoolbarview.h"
#include "llcommandmanager.h"
#include "lluictrlfactory.h"
#include "llxmlnode.h"
#include "llwindow.h"
#include "fsworldviewgeometry.h"
#include "llvoavatarself.h"

#include <algorithm>
#include <cmath>

namespace
{
using Role = FSWorkspaceLayout::Role;
const Role ROLES[] = {Role::InventoryPrimary, Role::MiniMap, Role::WorldMap,
    Role::NearbyChat, Role::ConversationsGeometry, Role::InventoryExtra1};
const char* registryName(Role role)
{
    switch (role)
    {
    case Role::InventoryPrimary: return "inventory";
    case Role::MiniMap: return "mini_map";
    case Role::WorldMap: return "world_map";
    case Role::NearbyChat: return "fs_nearby_chat";
    case Role::ConversationsGeometry: return "fs_im_container";
    case Role::InventoryExtra1: return "fs_workspace_inventory";
    }
    return "";
}
LLFloater* find(Role role) { return LLFloaterReg::findInstance(registryName(role)); }
bool standalone(LLFloater* floater)
{
    return floater && !floater->isDead() && !floater->getHost() && floater->getParent() == gFloaterView;
}
FSWorkspaceLayout::Rect rect(const LLRect& value)
{
    return {float(value.mLeft), float(value.mBottom), float(value.mRight), float(value.mTop)};
}
FSWorkspaceLayout::Rect frame()
{
    return gFloaterView ? rect(gFloaterView->getLocalRect()) : FSWorkspaceLayout::Rect{};
}
FSWorkspaceLayout::Window window(LLFloater* floater)
{
    // Nearby Chat's virtual getVisible() constructs an IM host. Capture must
    // inspect the view itself without invoking that presentation machinery.
    return FSWorkspaceLayout::capture(rect(floater->getWorkspaceRect()), frame(),
        floater->LLView::getVisible(), floater->isMinimized());
}
LLSidepanelInventory* inbox(LLFloater* floater)
{
    return floater ? floater->findChild<LLSidepanelInventory>("main_panel") : nullptr;
}
LLPanelMainInventory* inventoryPanel(LLFloater* floater)
{
    return floater ? floater->findChild<LLPanelMainInventory>("panel_main_inventory") : nullptr;
}
bool chatCompatible(LLFloater* floater)
{
    return standalone(floater) && gSavedSettings.getBOOL("ChatHistoryTornOff");
}
std::vector<std::string> inventoryControls(const std::string& registry, const LLSD& key = LLSD())
{
    const std::string name = LLFloaterReg::getBaseControlName(LLFloater::getControlName(registry, key));
    return {"floater_rect_" + name, "floater_pos_" + name + "_x", "floater_pos_" + name + "_y"};
}
std::vector<std::string> controlsFor(Role role)
{
    const std::string name = LLFloaterReg::getBaseControlName(registryName(role));
    auto result = inventoryControls(registryName(role));
    if (role != Role::ConversationsGeometry) result.push_back("floater_vis_" + name);
    if (role == Role::InventoryPrimary)
    {
        result.push_back("InventoryInboxToggleState");
        result.push_back("InventoryInboxHeight");
    }
    return result;
}
std::vector<LLFloater*> extraInventoryFloaters()
{
    std::vector<LLFloater*> result;
    LLFloater* primary = find(Role::InventoryPrimary);
    for (const char* registry : {"inventory", "secondary_inventory"})
        for (LLFloater* floater : LLFloaterReg::getFloaterList(registry))
            if (floater != primary && standalone(floater)) result.push_back(floater);
    return result;
}
bool equalWindow(const FSWorkspaceLayout::Window& a, const FSWorkspaceLayout::Window& b, bool host)
{
    const auto& af = a.inventory_folder;
    const auto& bf = b.inventory_folder;
    const bool equal_folder = !af.present || (bf.present && af.single_folder == bf.single_folder &&
        af.view_mode == bf.view_mode && af.folder_id == bf.folder_id &&
        (!af.has_expanded_folders || (bf.has_expanded_folders && af.expanded_folders == bf.expanded_folders)));
    return equal_folder && (host || (a.visible == b.visible && a.minimized == b.minimized)) &&
        (!a.has_geometry || (b.has_geometry &&
        (std::fabs(a.center_x - b.center_x) < .002f && std::fabs(a.center_y - b.center_y) < .002f &&
         std::fabs(a.width_ui - b.width_ui) <= 1.f && std::fabs(a.height_ui - b.height_ui) <= 1.f)));
}
}

FSWorkspaceController& FSWorkspaceController::instance()
{
    static FSWorkspaceController controller;
    static const bool installed = []() { LLFloater::setWorkspaceSnap(FSWorkspaceController::snap); gIdleCallbacks.addFunction(lifecycleIdle, &controller); return true; }();
    (void)installed;
    return controller;
}
bool FSWorkspaceController::available() const
{
    return LLStartUp::getStartupState() == STATE_STARTED && gAgent.getID().notNull() &&
        gFloaterView && FSChromeLayoutController::instanceExists() &&
        FSChromeLayoutController::instance().isReady() &&
        gSavedPerAccountSettings.controlExists("FSWorkspaceProfiles");
}
bool FSWorkspaceController::sameSession() const
{
    return mAccount == gAgent.getID() && mSession == gAgent.getSessionID();
}
FSWorkspaceController::RestoreReport FSWorkspaceController::restoreReport() const
{
    return available() && mReportAccount == gAgent.getID() && mReportSession == gAgent.getSessionID() ? mReport : RestoreReport{};
}
void FSWorkspaceController::beginReport(const std::string& name)
{
    mReport = RestoreReport{}; mReport.name = name; mReportPreview = false; mReportNoticeShown = false;
    mReportAccount = gAgent.getID(); mReportSession = gAgent.getSessionID();
    ++mReportGeneration; ++mReportRevision;
}
void FSWorkspaceController::report(const std::string& subject, const std::string& reason, int count)
{
    if (mReportAccount != gAgent.getID() || mReportSession != gAgent.getSessionID()) return;
    for (auto& line : mReport.lines)
        if (line.subject == subject && line.reason == reason) { line.count += count; ++mReportRevision; return; }
    if (mReport.lines.size() < 128) mReport.lines.push_back({subject, reason, count});
    else if (mReport.lines.back().reason == "report_limit") mReport.lines.back().count += count;
    else mReport.lines.push_back({"arrangement", "report_limit", count});
    ++mReportRevision;
}
void FSWorkspaceController::offerRestoreReport()
{
    if (mReportNoticeShown || !mReport.complete || (mReportPreview && !mQuickSwitch) ||
        mReportAccount != gAgent.getID() || mReportSession != gAgent.getSessionID()) return;
    mReportNoticeShown = true;
    const auto generation = mReportGeneration;
    const auto account = mReportAccount, session = mReportSession;
    LLNotificationsUtil::add("WorkspaceRestoreIssues", LLSD(), LLSD(),
        [this, generation, account, session](const LLSD& notification, const LLSD& response)
        {
            if (LLNotificationsUtil::getSelectedOption(notification, response) == 0 && generation == mReportGeneration &&
                account == gAgent.getID() && session == gAgent.getSessionID() && available()) LLFloaterReg::showInstance("workspace_report");
            return false;
        });
}
void FSWorkspaceController::abandon()
{
    ++mGeneration;
    ++mRevision;
    mPendingPlacement = false;
    mRestoreLayout = false; mPreserveCoordinates = false;
    mQuickSwitch = false;
    mTransaction = false;
    mHUDReviewed = mHUDPreviewPending = false; mHUDAllowed = {};
    mContextControls.clear(); mContextTouched = mCameraTouched = false;
    mRuntime.clear(); mControls.clear(); mPendingHandles.clear(); mTouched.clear(); mCreated.clear();
    mExtraControls.clear(); mExtraRuntime.clear(); mPendingExtraInventory.clear();
    mFocus.markDead();
    mProfiles = LLSD(); mActive.clear(); mRenamed.clear();
    mInboxTouched = mInboxBaselineValid = false; mToolbarTouched = false;
}
FSWorkspaceLayout::Workspace FSWorkspaceController::capture(bool remember_folders, int components, bool remember_toolbars, const FSWorkspaceContext::Options& context) const
{
    FSWorkspaceLayout::Workspace result;
    result.components = components;
    result.context = context;
    for (bool camera : {false, true})
    {
        auto& group = camera ? result.context.camera : result.context.graphics;
        if (group.mode == FSWorkspaceContext::Mode::Current) group = FSWorkspaceContext::captureGroup(camera);
        if (group.mode != FSWorkspaceContext::Mode::Off) result.components |= camera ? FSWorkspaceLayout::Camera : FSWorkspaceLayout::Graphics;
    }
    if (!context.huds.empty()) result.components |= FSWorkspaceLayout::HUDs;
    result.remember_toolbars = remember_toolbars;
    if (remember_toolbars && gToolBarView)
        for (int i = LLToolBarEnums::TOOLBAR_FIRST; i <= LLToolBarEnums::TOOLBAR_LAST; ++i)
            if (auto* toolbar = gToolBarView->getToolbar(static_cast<LLToolBarEnums::EToolBarLocation>(i)))
            {
                FSWorkspaceLayout::Toolbar saved; saved.location = i;
                saved.display_mode = static_cast<int>(toolbar->getButtonType());
                for (const auto& id : toolbar->getCommandsList())
                    if (auto* command = LLCommandManager::instance().getCommand(id)) saved.commands.push_back(command->name());
                result.toolbars.push_back(saved);
            }
    remember_folders = remember_folders && (components & FSWorkspaceLayout::Inventory);
    result.remember_inventory_folders = remember_folders;
    if (components & FSWorkspaceLayout::Chrome) result.chrome = FSChromeLayoutController::instance().captureSnapshot();
    if (components & FSWorkspaceLayout::Chrome) result.world_view_in_mouselook = gSavedSettings.getBOOL("FSWorldViewInMouselook");
    result.frame_width = frame().width(); result.frame_height = frame().height();
    // Capture registered utility roles and additional Inventory instances only.
    // Preferences, the workspace switcher and other management windows are excluded.
    for (Role role : ROLES)
    {
        if (!(components & FSWorkspaceLayout::componentForRole(role))) continue;
        LLFloater* floater = find(role);
        if (role == Role::NearbyChat && !chatCompatible(floater)) continue;
        if (role == Role::ConversationsGeometry && !standalone(floater)) continue;
        if (floater && !standalone(floater)) continue;
        result.windows[role] = floater ? window(floater) : FSWorkspaceLayout::Window{};
        if (remember_folders && result.windows[role].visible &&
            (role == Role::InventoryPrimary || role == Role::InventoryExtra1))
            if (auto* panel = inventoryPanel(floater))
                result.windows[role].inventory_folder = panel->captureWorkspaceFolder();
    }
    if (components & FSWorkspaceLayout::Inventory)
    if (auto* panel = inbox(find(Role::InventoryPrimary)))
    {
        S32 height = 0;
        result.has_inbox = panel->captureWorkspaceInbox(result.inbox_expanded, height);
        if (result.has_inbox) result.inbox_height = static_cast<F32>(height);
    }
    if (components & FSWorkspaceLayout::Inventory)
    for (LLFloater* floater : extraInventoryFloaters())
        if (floater->LLView::getVisible())
        {
            auto saved = window(floater);
            if (remember_folders)
                if (auto* panel = inventoryPanel(floater)) saved.inventory_folder = panel->captureWorkspaceFolder();
            result.extra_inventory.push_back(saved);
        }
    return result;
}
void FSWorkspaceController::rememberExtraInventoryControls(const std::string& registry, const LLSD& key)
{
    auto names = inventoryControls(registry, key);
    names.push_back("floater_vis_" + LLFloaterReg::getBaseControlName(LLFloater::getControlName(registry, key)));
    for (const auto& name : names)
    {
        if (mExtraControls.count(name)) continue;
        ControlBaseline baseline;
        if (LLControlVariable* control = gSavedPerAccountSettings.getControl(name))
        {
            baseline.existed = true; baseline.unsaved = control->hasUnsavedValue();
            baseline.saved = control->getSaveValue(); baseline.effective = control->getValue();
        }
        mExtraControls.emplace(name, baseline);
    }
}
void FSWorkspaceController::rememberExtraInventory(LLFloater* floater, bool created)
{
    for (const auto& entry : mExtraRuntime)
        if (entry.state.handle.get() == floater) return;
    ExtraInventoryBaseline baseline;
    baseline.registry = floater->getInstanceName(); baseline.key = floater->getKey();
    baseline.state.existed = !created;
    baseline.state.handle = floater->getDerivedHandle<LLFloater>();
    baseline.state.window = window(floater);
    baseline.state.positioning = floater->getWorkspacePositioning();
    if (auto* panel = inventoryPanel(floater)) baseline.state.folder_state = panel->captureWorkspaceFolderState();
    rememberExtraInventoryControls(baseline.registry, baseline.key);
    mExtraRuntime.push_back(baseline);
}
void FSWorkspaceController::rememberControls(Role role)
{
    for (const std::string& name : controlsFor(role))
    {
        ControlBaseline baseline;
        if (LLControlVariable* control = gSavedPerAccountSettings.getControl(name))
        {
            baseline.existed = true; baseline.unsaved = control->hasUnsavedValue();
            baseline.saved = control->getSaveValue(); baseline.effective = control->getValue();
        }
        mControls[name] = baseline;
    }
}
void FSWorkspaceController::rememberRole(Role role)
{
    RuntimeBaseline baseline;
    if (LLFloater* floater = find(role))
    {
        if (!standalone(floater)) return;
        baseline.existed = true;
        baseline.handle = floater->getDerivedHandle<LLFloater>();
        baseline.window = window(floater);
        baseline.positioning = floater->getWorkspacePositioning();
        if (role == Role::InventoryPrimary || role == Role::InventoryExtra1)
            if (auto* panel = inventoryPanel(floater)) baseline.folder_state = panel->captureWorkspaceFolderState();
    }
    mRuntime[role] = baseline;
    rememberControls(role);
    if (role == Role::InventoryPrimary && !mInboxTouched)
    {
        if (auto* panel = inbox(find(role)))
            mInboxBaselineValid = panel->captureWorkspaceInbox(mInboxBaselineExpanded, mInboxBaselineHeight);
    }
}
void FSWorkspaceController::beginPreferencesSession()
{
    if (mTransaction && (!sameSession() || !available())) abandon();
    // Opening Preferences during the deferred placement must capture the
    // accepted quick switch, rather than adopt its temporary rollback state.
    if (mQuickSwitch) finishQuickSwitch();
    if (!available() || mTransaction) return;
    mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
    mProfiles = gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    mActive = gSavedPerAccountSettings.getString("FSActiveWorkspace");
    mBaseline = capture(false, FSWorkspaceLayout::All, true, FSWorkspaceContext::liveOptions());
    // Capture the saved/effective stacks at Preferences open, before another
    // panel or a camera setting callback can change them during a preview.
    auto remember_context = [&](const std::string& name)
    {
        if (LLControlVariable* control = gSavedSettings.getControl(name))
        {
            ControlBaseline baseline;
            baseline.existed = true; baseline.unsaved = control->hasUnsavedValue();
            baseline.saved = control->getSaveValue(); baseline.effective = control->getValue();
            mContextControls[name] = baseline;
        }
    };
    for (bool camera : {false, true})
    {
        remember_context(camera ? "PresetCameraActive" : "PresetGraphicActive");
        for (const auto& control : FSWorkspaceContext::controls(camera)) remember_context(control.name);
    }
    for (Role role : ROLES) rememberRole(role);
    for (LLFloater* floater : extraInventoryFloaters()) rememberExtraInventory(floater);
    mTransaction = true;
    ++mRevision;
}
std::vector<std::string> FSWorkspaceController::names() const
{
    std::vector<std::string> result;
    if (!available()) return result;
    const LLSD profiles = mTransaction && sameSession() ? mProfiles : gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    if (profiles.isMap())
        for (auto it = profiles.beginMap(); it != profiles.endMap() && result.size() < FSWorkspaceLayout::MAX_PROFILES; ++it)
            if (FSWorkspaceLayout::isSafeProfileName(it->first)) result.push_back(it->first);
    return result;
}
bool FSWorkspaceController::isCustom(const std::string& id) const
{
    if (!available()) return false;
    const LLSD profiles = mTransaction && sameSession() ? mProfiles : gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    return profiles.isMap() && profiles.has(id) && !FSWorkspaceLayout::isBuiltinProfileId(id);
}
std::string FSWorkspaceController::activeId() const
{
    if (!available()) return "";
    const auto id = mTransaction && sameSession() ? mActive : gSavedPerAccountSettings.getString("FSActiveWorkspace");
    // Starting arrangements are retired; previously accepted template IDs
    // are not named workspaces. Saved user definitions remain usable.
    return FSWorkspaceLayout::isBuiltinProfileId(id) ? "" : id;
}
bool FSWorkspaceController::canQuickSwitch() const
{
    // Includes minimized Preferences: no pending settings are accepted by a
    // quick switch, and later Cancel must not undo a switch made elsewhere.
    return available() && !LLFloaterReg::instanceVisible("preferences");
}
bool FSWorkspaceController::quickSwitchWorkspace(const std::string& id)
{
    if (!canQuickSwitch()) return false;
    if (mQuickSwitch) finishQuickSwitch();
    FSWorkspaceLayout::Workspace parsed;
    std::string error;
    const LLSD profiles = gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    if (!profiles.has(id) || !FSWorkspaceLayout::fromLLSD(profiles[id], parsed, error)) return false;
    if (!rememberPrevious(parsed.context)) return false;
    // Discard any baseline captured during construction of a hidden panel.
    // This does not write profile definitions or unrelated preferences.
    abandon();
    if (!preview(id)) { abandon(); return false; }
    mQuickSwitch = true;
    // The quick-switch list closes after selection. Its controls stay alive,
    // so the deferred placement must not restore focus to that hidden window.
    mFocus.markDead();
    return true;
}
bool FSWorkspaceController::quickSwitchLayout(const std::string& id)
{
    if (!canQuickSwitch()) return false;
    if (mQuickSwitch) finishQuickSwitch();
    const auto ids = FSChromeLayoutController::instance().profileNames();
    if (std::find(ids.begin(), ids.end(), id) == ids.end() || !rememberPrevious()) return false;
    abandon();
    if (!FSChromeLayoutController::instance().applyProfile(id)) return false;
    beginReport(id); report("chrome", "restored"); mReport.complete = true;
    gSavedPerAccountSettings.setString("FSActiveWorkspace", "");
    return true;
}
void FSWorkspaceController::finishQuickSwitch()
{
    if (!mQuickSwitch) return;
    if (!sameSession() || !available()) { abandon(); return; }
    finishPlacement();
    acceptHUDs();
    if (!mRestoreLayout && !mActive.empty() && mProfiles.has(mActive))
    {
        const auto live = capture(mPending.remember_inventory_folders, mPending.components & FSWorkspaceLayout::All, mPending.remember_toolbars, mPending.context);
        mExpected = mPending;
        if (mPending.components & FSWorkspaceLayout::Chrome)
        { mExpected.chrome = live.chrome; mExpected.world_view_in_mouselook = live.world_view_in_mouselook; }
        for (auto it = mExpected.windows.begin(); it != mExpected.windows.end();)
        {
            const auto current = live.windows.find(it->first);
            if (current == live.windows.end() || (!mPendingHandles.count(it->first) && it->second.visible))
            { it = mExpected.windows.erase(it); continue; }
            const bool geometry = it->second.has_geometry, folder = it->second.inventory_folder.present;
            const bool expanded = it->second.inventory_folder.has_expanded_folders;
            it->second = current->second;
            if (!geometry) it->second.has_geometry = false;
            if (!folder) it->second.inventory_folder = FSWorkspaceLayout::InventoryFolder{};
            else if (!expanded) { it->second.inventory_folder.has_expanded_folders = false; it->second.inventory_folder.expanded_folders.clear(); }
            ++it;
        }
        mExpected.extra_inventory = live.extra_inventory;
        for (size_t i = 0; i < mExpected.extra_inventory.size(); ++i)
            if (i >= mPending.extra_inventory.size() || !mPending.extra_inventory[i].inventory_folder.present)
                mExpected.extra_inventory[i].inventory_folder = FSWorkspaceLayout::InventoryFolder{};
            else if (!mPending.extra_inventory[i].inventory_folder.has_expanded_folders)
            { mExpected.extra_inventory[i].inventory_folder.has_expanded_folders = false; mExpected.extra_inventory[i].inventory_folder.expanded_folders.clear(); }
        if (mExpected.has_inbox)
        { mExpected.has_inbox = live.has_inbox; mExpected.inbox_expanded = live.inbox_expanded; mExpected.inbox_height = live.inbox_height; }
        if (mExpected.remember_toolbars) mExpected.toolbars = live.toolbars;
        mExpectedDefinition = mProfiles[mActive]; mExpectedId = mActive;
        mExpectedAccount = mAccount; mExpectedSession = mSession;
        mComparisonDirty = true;
    }
    // Save only the chosen identifier. Definitions remain unchanged.
    gSavedPerAccountSettings.setString("FSActiveWorkspace", mActive);
    if (mRestoreLayout) gSavedSettings.setString("FSChromeActiveProfile", mRestoredLayout);
    mReportPreview = false;
    abandon();
}
bool FSWorkspaceController::rememberPrevious(const FSWorkspaceContext::Options& context)
{
    FSWorkspaceContext::Options options;
    if (context.graphics.mode != FSWorkspaceContext::Mode::Off) options.graphics.mode = FSWorkspaceContext::Mode::Current;
    if (context.camera.mode != FSWorkspaceContext::Mode::Off) options.camera.mode = FSWorkspaceContext::Mode::Current;
    const auto current = capture(true, FSWorkspaceLayout::All, true, options);
    FSWorkspaceLayout::Workspace validated; std::string error;
    if (!FSWorkspaceLayout::fromLLSD(FSWorkspaceLayout::toLLSD(current), validated, error))
    { mStatus = current.extra_inventory.size() > FSWorkspaceLayout::MAX_EXTRA_INVENTORY_WINDOWS ? "too_many_inventory_windows" : "invalid_data"; return false; }
    mPrevious = current;
    mPreviousWorkspace = activeId();
    mPreviousLayout = gSavedSettings.getString("FSChromeActiveProfile");
    mPreviousAccount = gAgent.getID(); mPreviousSession = gAgent.getSessionID();
    mHasPrevious = true;
    return true;
}
bool FSWorkspaceController::hasPrevious() const
{
    return canQuickSwitch() && mHasPrevious && mPreviousAccount == gAgent.getID() &&
        mPreviousSession == gAgent.getSessionID();
}
bool FSWorkspaceController::returnPrevious()
{
    if (!hasPrevious()) return false;
    if (mQuickSwitch) finishQuickSwitch();
    const auto target = mPrevious;
    const auto workspace = mPreviousWorkspace, layout = mPreviousLayout;
    if (!rememberPrevious(target.context)) return false; // Swap, including unsaved positions; never change named definitions.
    abandon();
    beginPreferencesSession();
    if (!startPreview(target, workspace)) return false;
    mQuickSwitch = true; mPreserveCoordinates = true; mRestoreLayout = true; mRestoredLayout = layout;
    mFocus.markDead();
    return true;
}
bool FSWorkspaceController::saveCurrentNow(const std::string& name, bool remember_folders, int components, bool remember_toolbars, const FSWorkspaceContext::Options& context)
{
    if (!canQuickSwitch()) return false;
    if (mQuickSwitch) finishQuickSwitch();
    abandon();
    // Reuse strict capture/validation, allowing only creation from this UI.
    // Pending Preferences edits are excluded by the guard above.
    if (!saveCurrent(name, false, remember_folders, components, remember_toolbars, context)) { abandon(); return false; }
    gSavedPerAccountSettings.setLLSD("FSWorkspaceProfiles", mProfiles);
    gSavedPerAccountSettings.setString("FSActiveWorkspace", mActive);
    mStatus = "saved_immediately";
    abandon();
    return true;
}
bool FSWorkspaceController::saveCurrent(const std::string& name, bool overwrite, bool remember_folders, int components, bool remember_toolbars, const FSWorkspaceContext::Options& context)
{
    beginPreferencesSession();
    if (!available() || !mTransaction) { mStatus = "unavailable"; return false; }
    finishPlacement();
    remember_folders = remember_folders && (components & FSWorkspaceLayout::Inventory);
    if (remember_folders && !gInventory.isInventoryUsable()) { mStatus = "inventory_not_ready"; return false; }
    std::string error;
    if (!FSWorkspaceLayout::canSaveProfile(mProfiles, name, error)) { mStatus = "invalid_name_or_data"; return false; }
    if (mProfiles.has(name) && !overwrite) { mStatus = "exists"; return false; }
    const auto current = capture(remember_folders, components & FSWorkspaceLayout::All, remember_toolbars, context);
    const auto too_many_branches = [](const FSWorkspaceLayout::Window& window)
    { return window.inventory_folder.expanded_folders.size() > FSWorkspaceLayout::MAX_EXPANDED_INVENTORY_FOLDERS; };
    if (std::any_of(current.windows.begin(), current.windows.end(), [&](const std::pair<const Role, FSWorkspaceLayout::Window>& entry) { return too_many_branches(entry.second); }) ||
        std::any_of(current.extra_inventory.begin(), current.extra_inventory.end(), too_many_branches))
    { mStatus = "too_many_expanded_folders"; return false; }
    if (current.extra_inventory.size() > static_cast<size_t>(FSWorkspaceLayout::MAX_EXTRA_INVENTORY_WINDOWS))
    { mStatus = "too_many_inventory_windows"; return false; }
    FSWorkspaceLayout::Workspace validated;
    const LLSD data = FSWorkspaceLayout::toLLSD(current);
    if (!FSWorkspaceLayout::fromLLSD(data, validated, error)) { mStatus = "invalid_data"; return false; }
    mProfiles[name] = data; mActive = name; mStatus = "saved"; ++mRevision;
    return true;
}
bool FSWorkspaceController::remembersInventoryFolders(const std::string& name) const
{
    if (!available()) return false;
    const LLSD profiles = mTransaction && sameSession() ? mProfiles : gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    FSWorkspaceLayout::Workspace workspace;
    std::string error;
    return profiles.isMap() && profiles.has(name) &&
        FSWorkspaceLayout::fromLLSD(profiles[name], workspace, error) && workspace.remember_inventory_folders;
}
bool FSWorkspaceController::rename(const std::string& old_name, const std::string& new_name)
{
    beginPreferencesSession();
    if (!available() || !isCustom(old_name)) return false;
    std::string error;
    FSWorkspaceLayout::Workspace parsed;
    if (!FSWorkspaceLayout::isSafeProfileName(new_name) || mProfiles.has(new_name)) { mStatus = "invalid_name_or_data"; return false; }
    if (!FSWorkspaceLayout::fromLLSD(mProfiles[old_name], parsed, error)) { mStatus = "invalid_data"; return false; }
    mProfiles[new_name] = mProfiles[old_name]; mProfiles.erase(old_name);
    mRenamed.emplace_back("workspace:" + old_name, "workspace:" + new_name);
    if (mActive == old_name) mActive = new_name;
    mStatus = "renamed"; ++mRevision; return true;
}
bool FSWorkspaceController::remove(const std::string& name)
{
    beginPreferencesSession();
    if (!available() || !isCustom(name)) return false;
    mProfiles.erase(name); if (mActive == name) mActive.clear();
    mRenamed.emplace_back("workspace:" + name, "");
    mStatus = "deleted"; ++mRevision; return true;
}
bool FSWorkspaceController::preview(const std::string& id)
{
    beginPreferencesSession();
    if (!available() || !mTransaction) { mStatus = "unavailable"; return false; }
    FSWorkspaceLayout::Workspace workspace;
    std::string error;
    if (FSWorkspaceLayout::isBuiltinProfileId(id))
    { mStatus = "invalid_data"; return false; }
    const LLSD& profiles = mProfiles; // Lookup must not insert a missing definition.
    const LLSD data = profiles[id];
    if (!FSWorkspaceLayout::fromLLSD(data, workspace, error)) { mStatus = "invalid_data"; return false; }
    return startPreview(workspace, id);
}
bool FSWorkspaceController::addInventoryWindow()
{
    beginPreferencesSession();
    if (!available() || !mTransaction) return false;
    auto workspace = capture();
    auto& extra = workspace.windows[Role::InventoryExtra1];
    if (!extra.has_geometry) { extra.has_geometry = true; extra.width_ui = 370.f; extra.height_ui = 600.f; extra.center_x = .25f; }
    extra.visible = true; extra.minimized = false;
    return startPreview(workspace, "");
}
bool FSWorkspaceController::startPreview(const FSWorkspaceLayout::Workspace& workspace, const std::string& id)
{
    ++mGeneration; ++mRevision;
    mPendingPlacement = false; mPendingHandles.clear();
    mPendingExtraInventory.clear();
    beginReport(id); mReportPreview = true;
    for (const auto& group : std::vector<std::pair<int, std::string>>{{FSWorkspaceLayout::Chrome, "chrome"}, {FSWorkspaceLayout::Inventory, "inventory"}, {FSWorkspaceLayout::Maps, "maps"}, {FSWorkspaceLayout::Chat, "chat"}})
        if (!(workspace.components & group.first)) report(group.second, "omitted");
    if (workspace.ignored_details) report("arrangement", "unknown_details", workspace.ignored_details);
    mHUDPreviewPending = !workspace.context.huds.empty();
    mPending = workspace; mFolderSkipped = mChatSkipped = mDependentSkipped = false; mApplied = mAdjusted = 0; mSkipped = workspace.ignored_details;
    applyContext(workspace);
    mFocus.markDead();
    if (auto* focus = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus())) mFocus = focus->getDerivedHandle<LLView>();
    if (workspace.components & FSWorkspaceLayout::Chrome)
    {
    FSChromeLayoutController::instance().applySnapshot(workspace.chrome);
    gSavedSettings.setBOOL("FSWorldViewInMouselook", workspace.world_view_in_mouselook);
    gSavedSettings.setString("FSChromeActiveProfile", "");
    if (gViewerWindow) gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
    FSChromeLayoutController::instance().apply();
    report("chrome", "restored");
    }
    for (const auto& entry : workspace.windows)
    {
        const Role role = entry.first;
        const std::string subject = FSWorkspaceLayout::roleId(role);
        LLFloater* floater = find(role);
        if ((role == Role::NearbyChat && (!chatCompatible(floater) ||
             !find(Role::ConversationsGeometry))) ||
            (role == Role::ConversationsGeometry && (!standalone(floater) || floater->isMinimized())) ||
            (floater && !standalone(floater)))
        { ++mSkipped; report(subject, role == Role::NearbyChat || role == Role::ConversationsGeometry ? "chat_incompatible" : "hosted"); if (role == Role::NearbyChat) mChatSkipped = true; continue; }
        // Moving/minimizing/closing utility parents would propagate to
        // untracked Filters/other windows. Keep those arrangements intact.
        if (floater && (floater->hasWorkspaceDependents() || floater->isDependent()))
        { ++mSkipped; report(subject, "dependents"); mDependentSkipped = true; continue; }
        // Adopt a newly opened allowed utility's current state as its rollback
        // baseline. Additional Inventory instances have their own baseline below.
        if (!mRuntime[role].existed && floater && !mCreated.count(role)) rememberRole(role);
        const bool needs_open = role != Role::ConversationsGeometry && entry.second.visible &&
            (!floater || !floater->LLView::getVisible());
        if (needs_open && !LLFloaterReg::canShowInstance(registryName(role))) { ++mSkipped; report(subject, "restricted"); continue; }
        if (needs_open)
        {
            const bool created = !floater;
            mTouched.insert(role);
            floater = LLFloaterReg::showInstance(registryName(role), LLSD(), false);
            if (created && floater) mCreated.insert(role);
        }
        if (!floater) { if (entry.second.visible) ++mSkipped; report(subject, entry.second.visible ? "unavailable" : "already_closed"); continue; } // hidden windows never construct anything
        if (!standalone(floater) || (role == Role::NearbyChat && !chatCompatible(floater))) { ++mSkipped; report(subject, role == Role::NearbyChat ? "chat_incompatible" : "hosted"); continue; }
        mTouched.insert(role);
        mPendingHandles[role] = floater->getDerivedHandle<LLFloater>();
    }
    auto extras = extraInventoryFloaters();
    // Reuse registered instances in the same order used by capture, including
    // hidden windows from earlier switches. Folder restoration is optional.
    const size_t count = (workspace.components & FSWorkspaceLayout::Inventory) ? std::max(extras.size(), workspace.extra_inventory.size()) : 0;
    for (size_t i = 0; i < count; ++i)
    {
        const auto subject = "extra_inventory:" + std::to_string(i + 1);
        const auto saved = i < workspace.extra_inventory.size() ? workspace.extra_inventory[i] : FSWorkspaceLayout::Window{};
        LLFloater* floater = i < extras.size() ? extras[i] : nullptr;
        if (floater && (floater->hasWorkspaceDependents() || floater->isDependent()))
        { ++mSkipped; report(subject, "dependents"); mDependentSkipped = true; continue; }
        if (saved.visible && !LLFloaterReg::canShowInstance(floater ? floater->getInstanceName() : "inventory",
                                                         floater ? floater->getKey() : LLSD()))
        { ++mSkipped; report(subject, "restricted"); continue; }
        const bool created = !floater;
        if (!floater && saved.visible)
        {
            rememberExtraInventoryControls("inventory");
            floater = LLPanelMainInventory::newWindow();
            if (!floater) { ++mSkipped; report(subject, "unavailable"); continue; }
        }
        if (!floater) continue;
        rememberExtraInventory(floater, created);
        for (auto& baseline : mExtraRuntime)
            if (baseline.state.handle.get() == floater) { baseline.touched = true; break; }
        mPendingExtraInventory.push_back({floater->getDerivedHandle<LLFloater>(), saved, subject});
    }
    if (workspace.remember_toolbars) { applyToolbars(workspace); mToolbarTouched = true; }
    mActive = id;
    mPendingPlacement = true; mStatus = "placing";
    const auto generation = mGeneration;
    const auto account = mAccount, session = mSession;
    LLAppViewer::instance()->addOnIdleCallback([generation, account, session]()
    { FSWorkspaceController::instance().placePending(generation, account, session); });
    // Showing utilities must not steal the Preferences keyboard focus.
    if (auto* focus = dynamic_cast<LLFocusableElement*>(mFocus.get())) gFocusMgr.setKeyboardFocus(focus);
    return true;
}
void FSWorkspaceController::placePending(unsigned long generation, const LLUUID& account, const LLUUID& session)
{
    if (generation != mGeneration || !mPendingPlacement) return;
    if (!available() || account != gAgent.getID() || session != gAgent.getSessionID()) { abandon(); return; }
    finishPlacement();
    if (mQuickSwitch) finishQuickSwitch();
}
bool FSWorkspaceController::placeWindow(LLFloater* floater, const FSWorkspaceLayout::Window& saved,
    bool primary_inventory, std::vector<LLRect>& placed, const FSWorkspaceLayout::Rect* recovery_frame)
{
    if (!saved.has_geometry) return true;
    S32 min_width, min_height; floater->getResizeLimits(&min_width, &min_height);
    std::vector<FSWorkspaceLayout::Rect> prior;
    if (!mPreserveCoordinates) for (const auto& value : placed) prior.push_back(rect(value));
    const auto placement = FSWorkspaceLayout::fitWithNeighbors(saved, recovery_frame ? *recovery_frame : frame(), static_cast<F32>(min_width), static_cast<F32>(min_height), prior);
    const LLRect target(ll_round(placement.rect.left), ll_round(placement.rect.top),
                        ll_round(placement.rect.right), ll_round(placement.rect.bottom));
    // The shared fitter already respects minima and title reachability. The
    // ordinary fitter would squeeze utility gutters back into the snap region.
    if (!floater->applyWorkspaceRect(target, primary_inventory)) return false;
    if (placement.adjusted || floater->getRect() != target) ++mAdjusted;
    placed.push_back(floater->getRect());
    return true;
}
void FSWorkspaceController::applyInventoryFolder(LLFloater* floater, const FSWorkspaceLayout::Window& saved,
    RuntimeBaseline& baseline, const std::string& subject)
{
    auto* panel = inventoryPanel(floater);
    // A later switch must cancel expansions queued by the earlier workspace,
    // including when the destination omits folder capture or hides this window.
    if (panel && panel->getAllItemsPanel()) panel->getAllItemsPanel()->cancelWorkspaceExpandedFolders();
    if (!saved.visible || !saved.inventory_folder.present) return;
    const auto generation = mReportGeneration;
    std::string failure = "unavailable";
    const auto expansion = [this, generation, subject](const LLUUID& id, const std::string& reason)
    {
        if (generation != mReportGeneration || mReportAccount != gAgent.getID() || mReportSession != gAgent.getSessionID()) return;
        if (reason == "branches_finished")
        {
            mReport.lines.erase(std::remove_if(mReport.lines.begin(), mReport.lines.end(), [&](const ReportLine& line)
            { return line.subject == subject && line.reason == "branches_pending"; }), mReport.lines.end());
            ++mReportRevision; return;
        }
        report(reason == "branch_restored" ? subject : "folder:" + id.asString(), reason);
        if (reason != "branch_restored")
        {
            // A branch skipped by this restore is not a later user edit. Keep
            // only the accepted transient comparison; never rewrite the save.
            if (mExpectedId == mReport.name && mExpectedAccount == mReportAccount && mExpectedSession == mReportSession)
            {
                const auto omit = [&id](FSWorkspaceLayout::Window& window)
                {
                    auto& ids = window.inventory_folder.expanded_folders;
                    ids.erase(std::remove(ids.begin(), ids.end(), id.asString()), ids.end());
                };
                for (auto& entry : mExpected.windows)
                    if (subject == FSWorkspaceLayout::roleId(entry.first)) omit(entry.second);
                for (size_t i = 0; i < mExpected.extra_inventory.size(); ++i)
                    if (subject == "extra_inventory:" + std::to_string(i + 1)) omit(mExpected.extra_inventory[i]);
                mComparisonDirty = true;
            }
            offerRestoreReport();
        }
    };
    if (saved.inventory_folder.has_expanded_folders && !saved.inventory_folder.expanded_folders.empty()) report(subject, "branches_pending");
    if (!panel || !panel->applyWorkspaceFolder(saved.inventory_folder, &failure, expansion))
    {
        ++mSkipped;
        mFolderSkipped = true; report(subject, failure);
        mReport.lines.erase(std::remove_if(mReport.lines.begin(), mReport.lines.end(), [&](const ReportLine& line) { return line.subject == subject && line.reason == "branches_pending"; }), mReport.lines.end());
    }
    else { baseline.folder_touched = true; report(subject, "folder_restored"); }
}
void FSWorkspaceController::finishPlacement()
{
    if (!mPendingPlacement || !sameSession() || !available()) return;
    mPendingPlacement = false;
    std::vector<LLRect> placed;
    for (const auto& entry : mPendingHandles)
    {
        Role role = entry.first;
        const std::string subject = FSWorkspaceLayout::roleId(role);
        LLFloater* floater = entry.second.get();
        const auto& saved = mPending.windows.at(role);
        if (!standalone(floater) || (role == Role::NearbyChat && !chatCompatible(floater)) ||
            (role == Role::ConversationsGeometry && floater->isMinimized())) { ++mSkipped; report(subject, "unavailable"); continue; }
        if (floater->hasWorkspaceDependents() || floater->isDependent())
        { ++mSkipped; report(subject, "dependents"); mDependentSkipped = true; continue; }
        const bool host = role == Role::ConversationsGeometry;
        // Restrictions may change between opening the window and this idle pass.
        if (!host && saved.visible && !LLFloaterReg::canShowInstance(registryName(role)))
        { ++mSkipped; report(subject, "restricted"); continue; }
        if (!host) floater->setMinimized(false);
        if (role == Role::InventoryPrimary || role == Role::InventoryExtra1)
            applyInventoryFolder(floater, saved, mRuntime[role], subject);
        const int adjusted_before = mAdjusted;
        if (!placeWindow(floater, saved, role == Role::InventoryPrimary, placed)) { ++mSkipped; report(subject, "placement_failed"); continue; }
        report(subject, adjusted_before != mAdjusted ? "adjusted" : "restored");
        if (!host)
        {
            if (!saved.visible) floater->closeFloater(false);
            else if (role != Role::NearbyChat) floater->setMinimized(saved.minimized);
        }
        ++mApplied;
    }
    for (const auto& entry : mPendingExtraInventory)
    {
        const auto& subject = entry.subject;
        LLFloater* floater = entry.handle.get();
        if (!standalone(floater)) { ++mSkipped; report(subject, "unavailable"); continue; }
        if (floater->hasWorkspaceDependents() || floater->isDependent())
        { ++mSkipped; report(subject, "dependents"); mDependentSkipped = true; continue; }
        const auto& saved = entry.window;
        if (saved.visible && !LLFloaterReg::canShowInstance(floater->getInstanceName(), floater->getKey()))
        { ++mSkipped; report(subject, "restricted"); continue; }
        floater->setMinimized(false);
        for (auto& baseline : mExtraRuntime)
            if (baseline.state.handle.get() == floater)
            {
                applyInventoryFolder(floater, saved, baseline.state, subject);
                break;
            }
        const int adjusted_before = mAdjusted;
        if (!placeWindow(floater, saved, false, placed)) { ++mSkipped; report(subject, "placement_failed"); continue; }
        report(subject, adjusted_before != mAdjusted ? "adjusted" : "restored");
        // closeFloater destroys ordinary extra Inventory instances. Hiding
        // preserves their views for reuse and exact Preferences Cancel rollback.
        floater->setVisible(saved.visible);
        if (saved.visible) floater->setMinimized(saved.minimized);
        ++mApplied;
    }
    if (mPending.has_inbox)
    {
        LLFloater* primary = find(Role::InventoryPrimary);
        // A panel-only record can change Inbox without moving Inventory. Adopt
        // a primary opened manually after the initial baseline before touching
        // its pane, without making its geometry part of workspace rollback.
        if (!mRuntime[Role::InventoryPrimary].existed && primary && !mCreated.count(Role::InventoryPrimary))
            rememberRole(Role::InventoryPrimary);
        auto* panel = inbox(primary);
        if (!mInboxBaselineValid && panel)
            mInboxBaselineValid = panel->captureWorkspaceInbox(mInboxBaselineExpanded, mInboxBaselineHeight);
        if (!panel || !panel->applyWorkspaceInbox(mPending.inbox_expanded, ll_round(mPending.inbox_height))) { ++mSkipped; report("inbox", "unavailable"); }
        else { mInboxTouched = true; report("inbox", "restored"); }
    }
    if (auto* focus = dynamic_cast<LLFocusableElement*>(mFocus.get())) gFocusMgr.setKeyboardFocus(focus);
    mStatus = mFolderSkipped ? "applied_folders_skipped" : mDependentSkipped ? "applied_dependents_skipped" : mChatSkipped ? "applied_chat_skipped" : "applied";
    mReport.complete = true; mReport.applied = mApplied; mReport.adjusted = mAdjusted; mReport.skipped = mSkipped; ++mReportRevision;
    if (mSkipped) offerRestoreReport();
}
void FSWorkspaceController::commitPendingSettings()
{
    if (!mTransaction) return;
    if (!sameSession() || !available()) { abandon(); return; }
    // Explicit OK settles its one pending placement before refreshing baseline.
    finishPlacement(); mReportPreview = false;
    acceptHUDs();
    ++mGeneration;
    gSavedPerAccountSettings.setLLSD("FSWorkspaceProfiles", mProfiles);
    gSavedPerAccountSettings.setString("FSActiveWorkspace", mActive);
    for (const auto& change : mRenamed)
    {
        if (gSavedPerAccountSettings.getString("FSWorkspaceShortcutTarget") == change.first)
            gSavedPerAccountSettings.setString("FSWorkspaceShortcutTarget", change.second);
        if (change.first.compare(0, 10, "workspace:") == 0 &&
            gSavedPerAccountSettings.getString("FSWorkspaceStartupName") == change.first.substr(10))
            gSavedPerAccountSettings.setString("FSWorkspaceStartupName", change.second.empty() ? "" : change.second.substr(10));
        const auto old = gSavedPerAccountSettings.getLLSD("FSWorkspaceQuickSwitchFavorites");
        LLSD updated = LLSD::emptyArray();
        if (old.isArray()) for (S32 i = 0; i < static_cast<S32>(std::min<size_t>(old.size(), 128)); ++i)
        {
            if (old[i].asString() != change.first) updated.append(old[i]);
            else if (!change.second.empty()) updated.append(change.second);
        }
        gSavedPerAccountSettings.setLLSD("FSWorkspaceQuickSwitchFavorites", updated);
    }
    abandon();
    beginPreferencesSession();
}
void FSWorkspaceController::restoreExtraInventories()
{
    for (const auto& entry : mExtraRuntime)
    {
        if (!entry.touched) continue;
        const auto& baseline = entry.state;
        LLFloater* floater = baseline.handle.get();
        if (!baseline.existed)
        {
            if (standalone(floater))
                floater->restoreWorkspaceState(floater->getWorkspaceRect(), false, false,
                                              floater->getWorkspacePositioning());
            continue;
        }
        if (!floater && baseline.window.visible && LLFloaterReg::canShowInstance(entry.registry, entry.key))
            floater = LLFloaterReg::showInstance(entry.registry, entry.key, false);
        if (!standalone(floater)) continue;
        if (baseline.folder_touched && LLFloaterReg::canShowInstance(entry.registry, entry.key))
            if (auto* panel = inventoryPanel(floater)) panel->restoreWorkspaceFolderState(baseline.folder_state);
        S32 min_width, min_height; floater->getResizeLimits(&min_width, &min_height);
        const auto placement = FSWorkspaceLayout::fit(baseline.window, frame(),
            static_cast<F32>(min_width), static_cast<F32>(min_height));
        const bool may_show = !baseline.window.visible || LLFloaterReg::canShowInstance(entry.registry, entry.key);
        floater->restoreWorkspaceState(LLRect(ll_round(placement.rect.left), ll_round(placement.rect.top),
            ll_round(placement.rect.right), ll_round(placement.rect.bottom)),
            baseline.window.visible && may_show, baseline.window.minimized, baseline.positioning);
    }
}
void FSWorkspaceController::cancelPreferencesSession()
{
    if (!mTransaction) return;
    ++mGeneration; mPendingPlacement = false;
    if (mReportPreview && sameSession())
    {
        ++mReportGeneration; report("arrangement", "cancelled"); mReportPreview = false;
        mReport.lines.erase(std::remove_if(mReport.lines.begin(), mReport.lines.end(), [](const ReportLine& line) { return line.reason == "branches_pending"; }), mReport.lines.end());
        mReport.complete = true;
    }
    if (!sameSession() || !available()) { abandon(); return; }
    if (mContextTouched)
    {
        auto restore_context = [&]()
        {
            auto restore = [&](const std::string& name, const ControlBaseline& baseline)
            {
                if (LLControlVariable* control = gSavedSettings.getControl(name))
                { control->setValue(baseline.saved, true); if (baseline.unsaved) control->setValue(baseline.effective, false); }
            };
            for (const auto& entry : mContextControls)
                if (entry.first != "PresetCameraActive" && entry.first != "PresetGraphicActive") restore(entry.first, entry.second);
            // Setting signals can clear the metadata; restore it last.
            for (const char* name : {"PresetCameraActive", "PresetGraphicActive"})
                if (mContextControls.count(name)) restore(name, mContextControls.at(name));
        };
        restore_context();
        if (mCameraTouched && FSWorkspaceContext::cameraAllowed())
        {
            gAgentCamera.switchCameraPreset(static_cast<ECameraPreset>(gSavedSettings.getU32("CameraPresetType")));
            const auto zoom = mBaseline.context.camera.values.find("CameraZoomFraction");
            if (zoom != mBaseline.context.camera.values.end()) gAgentCamera.setWorkspaceCameraZoom(static_cast<F32>(zoom->second[0]));
        }
        // switchCameraPreset persists its mode and settings callbacks clear
        // preset metadata. Restore the original value stacks after these APIs.
        restore_context();
        LLFloaterPreference::refreshEnabledGraphics();
        LLPresetsManager::instance().triggerChangeSignal(); LLPresetsManager::instance().triggerChangeCameraSignal();
    }
    if (mToolbarTouched) applyToolbars(mBaseline);
    FSChromeLayoutController::instance().applySnapshot(mBaseline.chrome);
    gSavedSettings.setBOOL("FSWorldViewInMouselook", mBaseline.world_view_in_mouselook);
    if (gViewerWindow) gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
    FSChromeLayoutController::instance().apply();
    for (Role role : mTouched)
    {
        auto& baseline = mRuntime[role];
        LLFloater* floater = baseline.handle.get();
        if (!baseline.existed)
        {
            if (mCreated.count(role))
                if (LLFloater* created = find(role))
                    if (standalone(created))
                    {
                        if (created->hasWorkspaceDependents())
                            created->restoreWorkspaceState(created->getWorkspaceRect(), false, false,
                                created->getWorkspacePositioning(), role == Role::InventoryPrimary);
                        else created->closeFloater(false);
                    }
            continue;
        }
        if (!floater && baseline.window.visible && role != Role::ConversationsGeometry && role != Role::NearbyChat &&
            LLFloaterReg::canShowInstance(registryName(role)))
            floater = LLFloaterReg::showInstance(registryName(role), LLSD(), false);
        if (!standalone(floater) || (role == Role::NearbyChat && (!chatCompatible(floater) || !find(Role::ConversationsGeometry)))) continue;
        if (role == Role::ConversationsGeometry && floater->isMinimized()) continue;
        if (baseline.folder_touched && LLFloaterReg::canShowInstance(registryName(role)))
            if (auto* panel = inventoryPanel(floater)) panel->restoreWorkspaceFolderState(baseline.folder_state);
        S32 min_width, min_height; floater->getResizeLimits(&min_width, &min_height);
        auto placement = FSWorkspaceLayout::fit(baseline.window, frame(), static_cast<F32>(min_width), static_cast<F32>(min_height));
        const bool host = role == Role::ConversationsGeometry;
        const bool may_show = host || !baseline.window.visible || LLFloaterReg::canShowInstance(registryName(role));
        floater->restoreWorkspaceState(LLRect(ll_round(placement.rect.left), ll_round(placement.rect.top),
            ll_round(placement.rect.right), ll_round(placement.rect.bottom)),
            host ? floater->LLView::getVisible() : baseline.window.visible && may_show,
            host ? floater->isMinimized() : baseline.window.minimized, baseline.positioning, role == Role::InventoryPrimary, host);
    }
    restoreExtraInventories();
    if (mInboxTouched && mInboxBaselineValid)
        if (auto* panel = inbox(find(Role::InventoryPrimary)))
            panel->applyWorkspaceInbox(mInboxBaselineExpanded, mInboxBaselineHeight);
    // Restore controls last: close/destroy and Inbox destructors also persist.
    if (std::any_of(mExtraRuntime.begin(), mExtraRuntime.end(),
                    [](const ExtraInventoryBaseline& entry) { return entry.touched; }))
        for (const auto& entry : mExtraControls)
            if (LLControlVariable* control = gSavedPerAccountSettings.getControl(entry.first))
            {
                const auto& baseline = entry.second;
                if (!baseline.existed) control->resetToDefault(true);
                else
                {
                    control->setValue(baseline.saved, true);
                    if (baseline.unsaved) control->setValue(baseline.effective, false);
                }
            }
    // Primary/owned role baselines take precedence for shared Inventory controls.
    for (Role role : mTouched)
        for (const auto& name : controlsFor(role))
        {
            const auto& baseline = mControls.at(name);
            if (LLControlVariable* control = gSavedPerAccountSettings.getControl(name))
            {
                if (!baseline.existed) control->resetToDefault(true);
                else
                {
                    control->setValue(baseline.saved, true);
                    if (baseline.unsaved) control->setValue(baseline.effective, false);
                }
            }
        }
    if (mInboxTouched)
        for (const char* name : {"InventoryInboxToggleState", "InventoryInboxHeight"})
            if (LLControlVariable* control = gSavedPerAccountSettings.getControl(name))
            {
                const auto& baseline = mControls.at(name);
                if (!baseline.existed) control->resetToDefault(true);
                else
                {
                    control->setValue(baseline.saved, true);
                    if (baseline.unsaved) control->setValue(baseline.effective, false);
                }
            }
    abandon();
}
bool FSWorkspaceController::readProfile(const std::string& id, FSWorkspaceLayout::Workspace& workspace) const
{
    if (!available()) return false;
    const LLSD profiles = mTransaction && sameSession() ? mProfiles : gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    std::string error;
    return profiles.has(id) && FSWorkspaceLayout::fromLLSD(profiles[id], workspace, error);
}
bool FSWorkspaceController::modified() const
{
    if (!available() || mPendingPlacement || activeId().empty()) return false;
    const auto id = activeId();
    if (!mComparisonDirty && mComparedId == id && mComparedAccount == gAgent.getID() && mComparedSession == gAgent.getSessionID() &&
        mComparisonTimer.getElapsedTimeF32() < .5f) return mModifiedCache;
    mComparedId = id; mComparedAccount = gAgent.getID(); mComparedSession = gAgent.getSessionID();
    mComparisonTimer.reset(); mModifiedCache = false; mComparisonDirty = false;
    FSWorkspaceLayout::Workspace saved;
    if (!readProfile(id, saved)) return false;
    const LLSD profiles = mTransaction && sameSession() ? mProfiles : gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    if (id == mExpectedId && mExpectedAccount == gAgent.getID() && mExpectedSession == gAgent.getSessionID() &&
        mExpectedDefinition == profiles[id]) saved = mExpected; // Accepted fitted geometry, not original screen sizes.
    else
    {
        // Refit the saved definition for this display. Returning to Previous
        // must compare its unsaved positions with the named definition, rather
        // than bless the returned live snapshot as a new clean workspace.
        std::vector<FSWorkspaceLayout::Rect> placed;
        auto fit_saved = [&](FSWorkspaceLayout::Window& window, LLFloater* floater)
        {
            if (!window.has_geometry) return;
            S32 width = 1, height = 1;
            if (floater) floater->getResizeLimits(&width, &height);
            const auto fitted = FSWorkspaceLayout::fitWithNeighbors(window, frame(), static_cast<F32>(width), static_cast<F32>(height), placed);
            placed.push_back(fitted.rect);
            const auto geometry = FSWorkspaceLayout::capture(fitted.rect, frame(), window.visible, window.minimized);
            window.center_x = geometry.center_x; window.center_y = geometry.center_y;
            window.width_ui = geometry.width_ui; window.height_ui = geometry.height_ui;
        };
        for (auto& entry : saved.windows) fit_saved(entry.second, find(entry.first));
        const auto extras = extraInventoryFloaters();
        for (size_t i = 0; i < saved.extra_inventory.size(); ++i) fit_saved(saved.extra_inventory[i], i < extras.size() ? extras[i] : nullptr);
    }
    const auto current = capture(saved.remember_inventory_folders, saved.components & FSWorkspaceLayout::All, saved.remember_toolbars, saved.context);
    bool changed = (saved.components & FSWorkspaceLayout::Chrome) && (FSChromeLayoutController::snapshotToLLSD(saved.chrome) != FSChromeLayoutController::snapshotToLLSD(current.chrome) ||
        saved.world_view_in_mouselook != current.world_view_in_mouselook);
    for (const auto& entry : saved.windows)
    {
        auto it = current.windows.find(entry.first);
        if (it != current.windows.end() && !equalWindow(entry.second, it->second, entry.first == Role::ConversationsGeometry)) changed = true;
    }
    if (saved.remember_toolbars && FSWorkspaceLayout::toLLSD(saved)["toolbars"] != FSWorkspaceLayout::toLLSD(current)["toolbars"]) changed = true;
    if (saved.extra_inventory.size() != current.extra_inventory.size()) changed = true;
    else for (size_t i = 0; i < saved.extra_inventory.size(); ++i)
        if (!equalWindow(saved.extra_inventory[i], current.extra_inventory[i], false)) changed = true;
    if (saved.has_inbox && (!current.has_inbox || saved.inbox_expanded != current.inbox_expanded ||
        std::fabs(saved.inbox_height - current.inbox_height) > 1.f)) changed = true;
    for (bool camera : {false, true})
    {
        const auto& group = camera ? saved.context.camera : saved.context.graphics;
        FSWorkspaceContext::Values expected; std::string error;
        if (group.mode != FSWorkspaceContext::Mode::Off)
        {
            if (!FSWorkspaceContext::resolveGroup(group, camera, expected, error)) changed = true;
            else
            {
                const auto live = FSWorkspaceContext::captureGroup(camera).values;
                for (const auto& entry : expected)
                {
                    auto found = live.find(entry.first);
                    if (found == live.end() || found->second.size() != entry.second.size()) { changed = true; continue; }
                    for (size_t index = 0; index < entry.second.size(); ++index)
                        if (std::fabs(found->second[index] - entry.second[index]) > .001) changed = true;
                }
            }
        }
    }
    if (!saved.context.huds.empty())
    {
        const auto worn = FSWorkspaceContext::attachedHUDs();
        for (const auto& hud : saved.context.huds)
            if (std::none_of(worn.begin(), worn.end(), [&](const FSWorkspaceContext::HUD& item) { return item.item == hud.item && item.point == hud.point; })) changed = true;
    }
    return mModifiedCache = changed;
}
void FSWorkspaceController::requestUpdateCurrent()
{
    if (!canQuickSwitch()) return;
    if (mQuickSwitch) finishQuickSwitch();
    const auto name = activeId();
    FSWorkspaceLayout::Workspace saved;
    if (!readProfile(name, saved) || saved.ignored_details != 0 || !isCustom(name) || (saved.remember_inventory_folders && !gInventory.isInventoryUsable())) return;
    const auto reviewed = FSWorkspaceLayout::toLLSD(capture(saved.remember_inventory_folders, saved.components & FSWorkspaceLayout::All, saved.remember_toolbars, saved.context));
    const auto originals = gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    FSWorkspaceLayout::Workspace validated; std::string error;
    if (!FSWorkspaceLayout::fromLLSD(reviewed, validated, error)) return;
    const auto account = gAgent.getID(), session = gAgent.getSessionID();
    const auto revision = mRevision;
    LLSD args; args["NAME"] = name;
    LLNotificationsUtil::add("ConfirmWorkspaceOverwrite", args, LLSD(),
        [account, session, revision, name, reviewed, originals](const LLSD& notification, const LLSD& response)
        {
            auto& controller = instance();
            if (LLNotificationsUtil::getSelectedOption(notification, response) != 0 || !controller.canQuickSwitch() ||
                account != gAgent.getID() || session != gAgent.getSessionID() || revision != controller.revision() ||
                originals != gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles") || controller.activeId() != name) return;
            LLSD profiles = originals;
            profiles[name] = reviewed; // Save exactly the arrangement reviewed when confirmation opened.
            gSavedPerAccountSettings.setLLSD("FSWorkspaceProfiles", profiles);
            controller.mExpectedId.clear(); controller.mComparisonDirty = true; ++controller.mRevision;
        });
}

std::vector<FSWorkspaceController::Utility> FSWorkspaceController::utilityWindows() const
{
    std::vector<Utility> result;
    if (!available()) return result;
    auto add = [&](LLFloater* floater, const std::string& label)
    {
        if (standalone(floater) && floater->LLView::getVisible() && !floater->isMinimized() &&
            !floater->hasWorkspaceDependents() && !floater->isDependent() &&
            LLFloaterReg::canShowInstance(floater->getInstanceName(), floater->getKey()))
            result.emplace_back(floater->getDerivedHandle<LLFloater>(), label);
    };
    for (Role role : ROLES)
    {
        if (role == Role::ConversationsGeometry) continue;
        auto* floater = find(role);
        if (role == Role::NearbyChat && !chatCompatible(floater)) continue;
        add(floater, floater ? floater->getTitle() : "");
    }
    size_t index = 1;
    for (auto* floater : extraInventoryFloaters())
        if (result.size() < 22) add(floater, floater->getTitle() + " " + std::to_string(index++));
    return result;
}
bool FSWorkspaceController::snap(LLFloater* floater, S32& edge, LLView::ESnapEdge snap_edge)
{
    auto& controller = instance();
    if (!gSavedSettings.getBOOL("FSWorkspaceWindowSnapping") || !controller.canQuickSwitch()) return false;
    const auto utilities = controller.utilityWindows();
    if (std::none_of(utilities.begin(), utilities.end(), [floater](const Utility& item) { return item.first.get() == floater; })) return false;
    // Shift bypasses both utility and ordinary snapping during drag or resize.
    if (gKeyboard && (gKeyboard->currentMask(true) & MASK_SHIFT)) return true;
    const bool horizontal = snap_edge == LLView::SNAP_LEFT || snap_edge == LLView::SNAP_RIGHT;
    std::vector<LLRect> targets{gFloaterView->getLocalRect()};
    if (gViewerWindow)
    {
        LLRect viewport = gViewerWindow->getWorldViewRectScaled();
        S32 x = 0, y = 0; gFloaterView->localPointToScreen(0, 0, &x, &y);
        viewport.translate(-x, -y); targets.push_back(viewport);
    }
    for (const auto& item : utilities)
        if (item.first.get() != floater) targets.push_back(item.first.get()->getRect());
    const auto own = floater->getRect();
    S32 best = 9, candidate = edge;
    for (size_t i = 0; i < targets.size(); ++i)
    {
        const auto& target = targets[i];
        if (i >= 2 && (horizontal ? own.mTop < target.mBottom - 8 || own.mBottom > target.mTop + 8 :
                                        own.mRight < target.mLeft - 8 || own.mLeft > target.mRight + 8)) continue;
        for (S32 value : {horizontal ? target.mLeft : target.mBottom, horizontal ? target.mRight : target.mTop})
            if (std::abs(value - edge) < best) { best = std::abs(value - edge); candidate = value; }
    }
    edge = candidate;
    return true;
}
bool FSWorkspaceController::arrange(const std::vector<LLHandle<LLFloater>>& selection, int operation)
{
    if (!canQuickSwitch() || operation < 0 || operation > 5) return false;
    if (mQuickSwitch) finishQuickSwitch();
    const auto allowed = utilityWindows();
    std::vector<LLFloater*> windows;
    for (const auto& handle : selection)
        if (auto* floater = handle.get())
            if (std::any_of(allowed.begin(), allowed.end(), [floater](const Utility& item) { return item.first.get() == floater; }) &&
                std::find(windows.begin(), windows.end(), floater) == windows.end()) windows.push_back(floater);
    if (windows.size() < 2 || (operation >= 4 && windows.size() < 3)) return false;
    if (!rememberPrevious()) return false;
    LLRect bounds = windows.front()->getRect();
    for (auto* floater : windows)
    {
        const auto r = floater->getRect();
        bounds.mLeft = std::min(bounds.mLeft, r.mLeft); bounds.mRight = std::max(bounds.mRight, r.mRight);
        bounds.mBottom = std::min(bounds.mBottom, r.mBottom); bounds.mTop = std::max(bounds.mTop, r.mTop);
    }
    const bool horizontal = operation == 4;
    if (operation >= 4)
        std::stable_sort(windows.begin(), windows.end(), [horizontal](LLFloater* a, LLFloater* b)
        { return horizontal ? a->getRect().mLeft < b->getRect().mLeft : a->getRect().mBottom < b->getRect().mBottom; });
    F32 total = 0.f;
    for (auto* floater : windows) total += static_cast<F32>(horizontal ? floater->getRect().getWidth() : floater->getRect().getHeight());
    const F32 gap = ((horizontal ? static_cast<F32>(bounds.getWidth()) : static_cast<F32>(bounds.getHeight())) - total) /
        static_cast<F32>(windows.size() - 1);
    F32 position = static_cast<F32>(horizontal ? bounds.mLeft : bounds.mBottom);
    for (auto* floater : windows)
    {
        auto r = floater->getRect();
        switch (operation)
        {
        case 0: r.translate(bounds.mLeft - r.mLeft, 0); break;
        case 1: r.translate(bounds.mRight - r.mRight, 0); break;
        case 2: r.translate(0, bounds.mTop - r.mTop); break;
        case 3: r.translate(0, bounds.mBottom - r.mBottom); break;
        default:
            if (horizontal) r.translate(ll_round(position) - r.mLeft, 0);
            else r.translate(0, ll_round(position) - r.mBottom);
            position += static_cast<F32>(horizontal ? r.getWidth() : r.getHeight()) + gap;
        }
        const auto saved = FSWorkspaceLayout::capture(rect(r), frame(), true, false);
        std::vector<LLRect> placed; // Deliberate alignment does not stagger matching edges.
        placeWindow(floater, saved, floater == find(Role::InventoryPrimary), placed);
    }
    ++mRevision;
    return true;
}

void FSWorkspaceController::scheduleStartupRestore()
{
    mHasPrevious = false; mPrevious = FSWorkspaceLayout::Workspace{};
    mPreviousWorkspace.clear(); mPreviousLayout.clear();
    mPreviousAccount.setNull(); mPreviousSession.setNull();
    mStartupScheduled = available() && gSavedPerAccountSettings.getString("FSWorkspaceStartupMode") != "off";
    mStartupAccount = gAgent.getID(); mStartupSession = gAgent.getSessionID();
    mStartupGeneration = mGeneration; mStartupTimer.reset();
}
void FSWorkspaceController::lifecycleIdle(void* userdata)
{
    auto& controller = *static_cast<FSWorkspaceController*>(userdata);
    if (controller.mLifecycleTimer.getElapsedTimeF32() < .5f) return;
    controller.mLifecycleTimer.reset();
    controller.pollHUDs();
    controller.pollDisplays();
    if (!controller.mReport.lines.empty() && (!controller.available() || controller.mReportAccount != gAgent.getID() || controller.mReportSession != gAgent.getSessionID()))
    { controller.mReport = RestoreReport{}; controller.mReportPreview = false; ++controller.mReportRevision; ++controller.mReportGeneration; }
    if (controller.mHasPrevious && (!controller.available() || controller.mPreviousAccount != gAgent.getID() ||
        controller.mPreviousSession != gAgent.getSessionID()))
    {
        controller.mHasPrevious = false; controller.mPrevious = FSWorkspaceLayout::Workspace{};
        controller.mPreviousWorkspace.clear(); controller.mPreviousLayout.clear();
        controller.mExpectedId.clear(); controller.mExpectedDefinition = LLSD(); controller.mExpected = FSWorkspaceLayout::Workspace{};
    }
    if (controller.mTransaction && (!controller.available() || !controller.sameSession())) controller.abandon();
    if (!controller.mExpectedId.empty() && (!controller.available() || controller.mExpectedAccount != gAgent.getID() || controller.mExpectedSession != gAgent.getSessionID()))
    { controller.mExpectedId.clear(); controller.mExpectedDefinition = LLSD(); controller.mExpected = FSWorkspaceLayout::Workspace{}; }
    if (!controller.mStartupScheduled) return;
    if (!controller.available() || controller.mStartupAccount != gAgent.getID() ||
        controller.mStartupSession != gAgent.getSessionID() || controller.mStartupGeneration != controller.mGeneration)
    { controller.mStartupScheduled = false; return; }
    if (controller.mStartupTimer.getElapsedTimeF32() > 120.f)
    {
        controller.mStartupScheduled = false;
        LLSD args; args["MESSAGE"] = "Startup workspace was not restored because the UI or Inventory was not ready. Switch it manually when ready.";
        LLNotificationsUtil::add("GenericAlert", args); return;
    }
    if (!gInventory.isInventoryUsable() || !gToolBarView || !controller.canQuickSwitch()) return;
    controller.mStartupScheduled = false;
    const auto mode = gSavedPerAccountSettings.getString("FSWorkspaceStartupMode");
    if (mode == "off") return;
    bool applied = false;
    if (mode == "named") applied = controller.quickSwitchWorkspace(gSavedPerAccountSettings.getString("FSWorkspaceStartupName"));
    else if (mode == "last")
    {
        FSWorkspaceLayout::Workspace saved; std::string error;
        if (FSWorkspaceLayout::fromLLSD(gSavedPerAccountSettings.getLLSD("FSWorkspaceLastArrangement"), saved, error))
        {
            controller.abandon(); controller.beginPreferencesSession();
            applied = controller.startPreview(saved, "");
            controller.mQuickSwitch = applied; controller.mPreserveCoordinates = true; controller.mFocus.markDead();
        }
        else if (gSavedPerAccountSettings.getLLSD("FSWorkspaceLastArrangement").isUndefined()) return;
    }
    if (!applied)
    {
        LLSD args; args["MESSAGE"] = "The startup workspace is missing or invalid. Your current arrangement was kept.";
        LLNotificationsUtil::add("GenericAlert", args);
    }
    controller.mHasPrevious = false; // Startup is not a user switch.
}
void FSWorkspaceController::saveLastArrangement()
{
    if (!canQuickSwitch() || !gInventory.isInventoryUsable() || gDisconnected ||
        gSavedPerAccountSettings.getString("FSWorkspaceStartupMode") != "last") return;
    if (mQuickSwitch) finishQuickSwitch();
    const auto data = FSWorkspaceLayout::toLLSD(capture(true, FSWorkspaceLayout::All, true, FSWorkspaceContext::liveOptions()));
    FSWorkspaceLayout::Workspace validated; std::string error;
    if (FSWorkspaceLayout::fromLLSD(data, validated, error))
        gSavedPerAccountSettings.setLLSD("FSWorkspaceLastArrangement", data);
}

void FSWorkspaceController::applyToolbars(const FSWorkspaceLayout::Workspace& workspace)
{
    if (!workspace.remember_toolbars || !gToolBarView) return;
    gToolBarView->clearToolbars();
    for (const auto& bar : workspace.toolbars)
    {
        const auto location = static_cast<LLToolBarEnums::EToolBarLocation>(bar.location);
        auto* toolbar = gToolBarView->getToolbar(location);
        if (!toolbar) { ++mSkipped; report("toolbars", "unavailable"); continue; }
        toolbar->setButtonType(static_cast<LLToolBarEnums::ButtonType>(bar.display_mode));
        for (const auto& name : bar.commands)
            if (LLCommandManager::instance().getCommand(name)) gToolBarView->addCommand(LLCommandId(name), location);
            else { ++mSkipped; report("toolbar:" + name, "toolbar_unavailable"); }
        report("toolbars", "restored");
    }
}

bool FSWorkspaceController::importProfiles(const LLSD& accepted, const LLSD& originals, bool replace)
{
    if (!canQuickSwitch() || !accepted.isMap() || accepted.size() > FSWorkspaceLayout::MAX_PROFILES ||
        originals != gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles") || (!originals.isMap() && !originals.isUndefined())) return false;
    if (mQuickSwitch) finishQuickSwitch();
    LLSD result = originals.isUndefined() ? LLSD::emptyMap() : originals;
    for (auto it = accepted.beginMap(); it != accepted.endMap(); ++it)
    {
        if (result.has(it->first) && !replace) continue;
        FSWorkspaceLayout::Workspace parsed; std::string error;
        if (!FSWorkspaceLayout::canSaveProfile(result, it->first, error) ||
            !FSWorkspaceLayout::fromLLSD(it->second, parsed, error)) return false;
        result[it->first] = FSWorkspaceLayout::toLLSD(parsed);
    }
    if (result.size() > FSWorkspaceLayout::MAX_PROFILES) return false;
    gSavedPerAccountSettings.setLLSD("FSWorkspaceProfiles", result); // Single validated batch; no partial writes.
    mExpectedId.clear(); ++mRevision; return true;
}

FSWorkspaceController::Diagram FSWorkspaceController::diagram(const FSWorkspaceLayout::Workspace& workspace) const
{
    Diagram result; result.frame = frame(); result.chrome = workspace.chrome;
    result.components = workspace.components; result.toolbars = workspace.remember_toolbars;
    if (!available()) return result;
    // Read skin-layered minima without constructing floaters or applying state.
    const char* files[] = {"floater_my_inventory.xml", "floater_map.xml", "floater_world_map.xml",
        "floater_fs_nearby_chat.xml", "floater_fs_im_container.xml", "floater_my_inventory.xml"};
    const char* labels[] = {"Inventory", "Mini-map", "Map", "Nearby chat", "Conversations", "Inventory"};
    std::vector<FSWorkspaceLayout::Rect> placed;
    auto add = [&](const FSWorkspaceLayout::Window& saved, LLFloater* floater, const char* file, const std::string& label, bool host)
    {
        if (!saved.has_geometry) return;
        S32 width = 1, height = 1;
        if (floater) floater->getResizeLimits(&width, &height);
        else
        {
            LLXMLNodePtr node;
            if (LLUICtrlFactory::getLayeredXMLNode(file, node))
            { node->getAttributeS32("min_width", width); node->getAttributeS32("min_height", height); }
        }
        const auto fitted = FSWorkspaceLayout::fitWithNeighbors(saved, result.frame, static_cast<F32>(width), static_cast<F32>(height), placed);
        placed.push_back(fitted.rect);
        if (saved.visible || (host && floater && floater->LLView::getVisible()))
        {
            result.windows.push_back({fitted.rect, label, saved.minimized});
            if (fitted.adjusted) ++result.adjusted;
        }
        const auto& folder = saved.inventory_folder;
        if (folder.present && !folder.folder_id.empty() && gInventory.isInventoryUsable() && !gInventory.getCategory(LLUUID(folder.folder_id))) ++result.missing_folders;
    };
    for (const auto& entry : workspace.windows)
    {
        auto* floater = find(entry.first);
        if ((floater && (!standalone(floater) || floater->hasWorkspaceDependents() || floater->isDependent())) ||
            (entry.first == Role::NearbyChat && (!chatCompatible(floater) || !find(Role::ConversationsGeometry))) ||
            (entry.first == Role::ConversationsGeometry && (!standalone(floater) || floater->isMinimized())) ||
            (entry.second.visible && !LLFloaterReg::canShowInstance(registryName(entry.first))))
        { ++result.skipped; continue; }
        const auto index = static_cast<size_t>(entry.first);
        add(entry.second, floater, files[index], labels[index], entry.first == Role::ConversationsGeometry);
    }
    const auto extras = extraInventoryFloaters();
    for (size_t i = 0; i < workspace.extra_inventory.size(); ++i)
    {
        auto* floater = i < extras.size() ? extras[i] : nullptr;
        if ((floater && (floater->hasWorkspaceDependents() || floater->isDependent())) ||
            !LLFloaterReg::canShowInstance(floater ? floater->getInstanceName() : "inventory", floater ? floater->getKey() : LLSD()))
        { ++result.skipped; continue; }
        add(workspace.extra_inventory[i], floater, "floater_my_inventory.xml", "Inventory " + std::to_string(i + 2), false);
    }
    return result;
}

void FSWorkspaceController::noteLayoutRename(const std::string& old_name, const std::string& new_name)
{
    beginPreferencesSession();
    if (mTransaction && sameSession()) mRenamed.emplace_back("layout:" + old_name, new_name.empty() ? "" : "layout:" + new_name);
}

void FSWorkspaceController::applyContext(const FSWorkspaceLayout::Workspace& workspace)
{
    for (bool camera : {false, true})
    {
        const auto& group = camera ? workspace.context.camera : workspace.context.graphics;
        if (group.mode == FSWorkspaceContext::Mode::Off) continue;
        const std::string subject = camera ? "camera" : "graphics";
        FSWorkspaceContext::Values values; std::string error;
        if ((camera && !FSWorkspaceContext::cameraAllowed()) || !FSWorkspaceContext::resolveGroup(group, camera, values, error))
        { ++mSkipped; report(subject, camera && error.empty() ? "camera_blocked" : error); continue; }
        auto remember = [&](const std::string& name)
        {
            if (!mContextControls.count(name))
                if (LLControlVariable* control = gSavedSettings.getControl(name))
                { ControlBaseline baseline; baseline.existed = true; baseline.unsaved = control->hasUnsavedValue(); baseline.saved = control->getSaveValue(); baseline.effective = control->getValue(); mContextControls[name] = baseline; }
        };
        remember(camera ? "PresetCameraActive" : "PresetGraphicActive");
        for (const auto& definition : FSWorkspaceContext::controls(camera))
        {
            auto found = values.find(definition.name); if (found == values.end()) continue;
            if (!camera && !LLFeatureManager::instance().isFeatureAvailable(definition.name) &&
                (std::string(definition.name) == "RenderShadowDetail" || std::string(definition.name) == "RenderDeferredSSAO"))
            { report(subject, "graphics_limited"); ++mSkipped; continue; }
            if (LLControlVariable* control = gSavedSettings.getControl(definition.name))
            { remember(definition.name); control->setValue(FSWorkspaceContext::controlValue(definition, found->second), true); mContextTouched = true; }
        }
        gSavedSettings.setString(camera ? "PresetCameraActive" : "PresetGraphicActive", ""); mContextTouched = true;
        if (camera)
        {
            mCameraTouched = true;
            gAgentCamera.switchCameraPreset(static_cast<ECameraPreset>(gSavedSettings.getU32("CameraPresetType")));
            auto zoom = values.find("CameraZoomFraction");
            if (zoom != values.end()) gAgentCamera.setWorkspaceCameraZoom(static_cast<F32>(zoom->second[0]));
            LLPresetsManager::instance().triggerChangeCameraSignal();
        }
        else { LLFloaterPreference::refreshEnabledGraphics(); LLPresetsManager::instance().triggerChangeSignal(); }
        report(subject, "restored");
    }
    if (!workspace.context.huds.empty()) report("huds", "hud_review");
}

void FSWorkspaceController::acceptHUDs()
{
    if (!mHUDPreviewPending) return;
    mHUDPreviewPending = false; // A later Preferences OK must not replay an accepted/declined switch.
    std::vector<FSWorkspaceContext::HUD> missing;
    for (const auto& hud : mPending.context.huds)
    {
        if (FSWorkspaceContext::hudWorn(hud)) { report(hud.name, "restored"); continue; }
        std::string error;
        if (!FSWorkspaceContext::hudAvailable(hud, error)) { ++mSkipped; report(hud.name, error); continue; }
        missing.push_back(hud);
    }
    mReport.skipped = mSkipped; ++mReportRevision;
    if (missing.empty())
    {
        mReport.lines.erase(std::remove_if(mReport.lines.begin(), mReport.lines.end(), [](const ReportLine& line)
            { return line.reason == "hud_review"; }), mReport.lines.end());
        if (mSkipped) offerRestoreReport();
        return;
    }
    mReport.complete = false;
    const auto account = gAgent.getID(), session = gAgent.getSessionID(); const auto generation = mReportGeneration;
    LLSD args; std::string names;
    for (const auto& hud : missing) names += hud.name + "\n";
    args["HUDS"] = names;
    const auto permission_check = mHUDAllowed;
    auto apply = [this, account, session, generation, missing, permission_check](bool accepted)
        {
            if (account != gAgent.getID() || session != gAgent.getSessionID() || generation != mReportGeneration) return false;
            mReport.lines.erase(std::remove_if(mReport.lines.begin(), mReport.lines.end(), [](const ReportLine& line)
                { return line.reason == "hud_review"; }), mReport.lines.end());
            if ((permission_check && !permission_check()) || !canQuickSwitch())
            { report("huds", "hud_switch_blocked"); mSkipped += static_cast<int>(missing.size()); mReport.skipped = mSkipped; mReport.complete = true; ++mReportRevision; offerRestoreReport(); return false; }
            if (!accepted)
            { report("huds", "hud_declined"); mReport.complete = true; ++mReportRevision; return false; }
            mPendingHUDs.clear(); mHUDAccount = account; mHUDSession = session; mHUDReport = generation; mHUDTimer.reset();
            for (const auto& hud : missing)
            {
                if (permission_check && !permission_check()) { report(hud.name, "restricted"); continue; }
                std::string error;
                if (!FSWorkspaceContext::hudAvailable(hud, error)) { report(hud.name, error); ++mSkipped; continue; }
                if (FSWorkspaceContext::hudWorn(hud)) continue;
                if (!gAgentAvatarp->canAttachMoreObjects(static_cast<U32>(mPendingHUDs.size() + 1))) { report(hud.name, "hud_limit"); ++mSkipped; continue; }
                FSWorkspaceContext::attachHUD(hud); mPendingHUDs.push_back(hud); report(hud.name, "hud_pending");
            }
            mReport.complete = mPendingHUDs.empty(); mReport.skipped = mSkipped; ++mReportRevision;
            return false;
        };
    if (mHUDReviewed) apply(true);
    else LLNotificationsUtil::add("ConfirmWorkspaceHUDs", args, LLSD(), [apply](const LLSD& notification, const LLSD& response) { return apply(LLNotificationsUtil::getSelectedOption(notification, response) == 0); });
}
void FSWorkspaceController::pollHUDs()
{
    if (mPendingHUDs.empty()) return;
    if (!available() || mHUDAccount != gAgent.getID() || mHUDSession != gAgent.getSessionID() || mHUDReport != mReportGeneration)
    { mPendingHUDs.clear(); return; }
    for (auto it = mPendingHUDs.begin(); it != mPendingHUDs.end();)
    {
        std::string error;
        const bool worn = FSWorkspaceContext::hudWorn(*it), timeout = mHUDTimer.getElapsedTimeF32() > 30.f;
        if (!worn && !timeout && FSWorkspaceContext::hudAvailable(*it, error)) { ++it; continue; }
        mReport.lines.erase(std::remove_if(mReport.lines.begin(), mReport.lines.end(), [&](const ReportLine& line)
            { return line.subject == it->name && line.reason == "hud_pending"; }), mReport.lines.end());
        report(it->name, worn ? "restored" : timeout ? "hud_timeout" : error);
        if (!worn) ++mSkipped;
        it = mPendingHUDs.erase(it);
    }
    mReport.complete = mPendingHUDs.empty() && !mPendingPlacement; mReport.skipped = mSkipped; ++mReportRevision;
    if (mReport.complete && mSkipped) offerRestoreReport();
}
void FSWorkspaceController::pollDisplays()
{
    if (!available() || !gViewerWindow) { mDisplaySignature.clear(); mRecoveryWindows.clear(); mDisplayPending = false; return; }
    if (!gSavedSettings.getBOOL("FSWorkspaceDisplayRecovery")) { mDisplaySignature.clear(); mRecoveryWindows.clear(); mDisplayPending = false; return; }
    if (mDisplayPollTimer.getElapsedTimeF32() < 2.f) return;
    mDisplayPollTimer.reset();
    std::vector<LLWindow::MonitorRect> monitors;
    const bool supported = gViewerWindow->getWindow()->getMonitorRectsInClient(monitors) && !monitors.empty();
    std::sort(monitors.begin(), monitors.end(), [](const LLWindow::MonitorRect& a, const LLWindow::MonitorRect& b) { return a.id < b.id; });
    std::string signature = supported ? "monitors" : "unavailable";
    const auto scale = gViewerWindow->getDisplayScale();
    signature += ":" + std::to_string(scale.mV[VX]) + ":" + std::to_string(scale.mV[VY]);
    if (supported)
    {
        const auto anchor = monitors.front().rect;
        for (const auto& monitor : monitors)
            signature += ":" + monitor.id + ":" + std::to_string(monitor.rect.mLeft - anchor.mLeft) + ":" +
                std::to_string(monitor.rect.mBottom - anchor.mBottom) + ":" + std::to_string(monitor.rect.getWidth()) + ":" + std::to_string(monitor.rect.getHeight());
    }
    if (mDisplayAccount != gAgent.getID() || mDisplaySession != gAgent.getSessionID())
    { mDisplaySignature.clear(); mRecoveryWindows.clear(); mDisplayPending = false; mDisplayAccount = gAgent.getID(); mDisplaySession = gAgent.getSessionID(); }
    if (signature != mDisplaySignature)
    {
        if (!mDisplaySignature.empty() && gSavedSettings.getBOOL("FSWorkspaceDisplayRecovery")) { mDisplayPending = true; mDisplayTimer.reset(); }
        mDisplaySignature = signature;
    }
    if (mDisplayPending)
    {
        if (mDisplayTimer.getElapsedTimeF32() < 1.5f || !canQuickSwitch() || mQuickSwitch) return;
        mDisplayPending = false; recoverWindows(true);
    }
    if (!canQuickSwitch() || mQuickSwitch) return;
    mRecoverySnapshot = capture(); mRecoveryWindows.clear();
    for (const auto& entry : utilityWindows()) if (auto* floater = entry.first.get()) mRecoveryWindows.push_back({entry.first, window(floater)});
}
bool FSWorkspaceController::recoverWindows(bool automatic)
{
    if (!available() || !gViewerWindow) return false;
    const bool preferences_preview = !canQuickSwitch();
    if (preferences_preview)
    {
        beginPreferencesSession(); finishPlacement();
        mHUDPreviewPending = false;
        mPending = capture(); // Recovery only; accepting it never adds HUDs or reloads context.
    }
    else
    {
        if (mQuickSwitch) finishQuickSwitch();
        if (!rememberPrevious()) return false;
    }
    if (automatic && mDisplayAccount == gAgent.getID() && mDisplaySession == gAgent.getSessionID() && !mRecoveryWindows.empty())
    { mPrevious = mRecoverySnapshot; mPreviousWorkspace = activeId(); }
    beginReport("Display recovery"); mReportPreview = preferences_preview; mApplied = mAdjusted = mSkipped = 0;
    std::vector<FSWorkspaceLayout::Rect> visible_frames;
    std::vector<LLWindow::MonitorRect> monitors;
    const auto scale = gViewerWindow->getDisplayScale();
    S32 floater_left = 0, floater_bottom = 0;
    gFloaterView->localPointToScreen(0, 0, &floater_left, &floater_bottom);
    if (gViewerWindow->getWindow()->getMonitorRectsInClient(monitors))
        for (const auto& monitor : monitors)
        {
            const auto& raw = monitor.rect;
            const auto bounds = frame();
            FSWorkspaceLayout::Rect visible{std::max(bounds.left, static_cast<F32>(raw.mLeft) / scale.mV[VX] - static_cast<F32>(floater_left)),
                std::max(bounds.bottom, static_cast<F32>(raw.mBottom) / scale.mV[VY] - static_cast<F32>(floater_bottom)),
                std::min(bounds.right, static_cast<F32>(raw.mRight) / scale.mV[VX] - static_cast<F32>(floater_left)),
                std::min(bounds.top, static_cast<F32>(raw.mTop) / scale.mV[VY] - static_cast<F32>(floater_bottom))};
            if (visible.width() > 0.f && visible.height() > 0.f) visible_frames.push_back(visible);
        }
    if (visible_frames.empty()) { visible_frames.push_back(frame()); report("arrangement", "display_unavailable"); }
    // Keep the live world view reachable even when an unmaximized viewer still spans a disconnected screen.
    const auto base = gViewerWindow->getWorldViewBaseRectRaw(false);
    const auto world = gViewerWindow->getWorldViewRectRaw();
    LLRect reachable; S64 best_area = 0;
    for (const auto& monitor : monitors)
    {
        LLRect candidate; candidate.set(std::max(world.mLeft, monitor.rect.mLeft), std::min(world.mTop, monitor.rect.mTop),
            std::min(world.mRight, monitor.rect.mRight), std::max(world.mBottom, monitor.rect.mBottom));
        const S64 area = static_cast<S64>(std::max(0, candidate.getWidth())) * std::max(0, candidate.getHeight());
        if (area > best_area) { best_area = area; reachable = candidate; }
    }
    if (visible_frames.size() == 1 && !monitors.empty())
    {
        if (best_area == 0)
        {
            const auto& visible = visible_frames.front();
            reachable.set(ll_round((visible.left + static_cast<F32>(floater_left)) * scale.mV[VX]), ll_round((visible.top + static_cast<F32>(floater_bottom)) * scale.mV[VY]),
                ll_round((visible.right + static_cast<F32>(floater_left)) * scale.mV[VX]), ll_round((visible.bottom + static_cast<F32>(floater_bottom)) * scale.mV[VY]));
        }
        reachable.set(std::max(base.mLeft, reachable.mLeft), std::min(base.mTop, reachable.mTop),
            std::min(base.mRight, reachable.mRight), std::max(base.mBottom, reachable.mBottom));
        if (reachable != world && reachable.getWidth() > 0 && reachable.getHeight() > 0 && base.getWidth() > 0 && base.getHeight() > 0)
        {
            gSavedSettings.setBOOL("FSWorldViewEnabled", true);
            gSavedSettings.setF32("FSWorldViewInsetLeft", 100.f * static_cast<F32>(reachable.mLeft - base.mLeft) / static_cast<F32>(base.getWidth()));
            gSavedSettings.setF32("FSWorldViewInsetRight", 100.f * static_cast<F32>(base.mRight - reachable.mRight) / static_cast<F32>(base.getWidth()));
            gSavedSettings.setF32("FSWorldViewInsetBottom", 100.f * static_cast<F32>(reachable.mBottom - base.mBottom) / static_cast<F32>(base.getHeight()));
            gSavedSettings.setF32("FSWorldViewInsetTop", 100.f * static_cast<F32>(base.mTop - reachable.mTop) / static_cast<F32>(base.getHeight()));
            report("chrome", "display_recovered");
        }
    }
    std::vector<LLRect> placed;
    for (const auto& entry : utilityWindows())
        if (auto* floater = entry.first.get())
        {
            if (preferences_preview)
            {
                bool registered = false;
                for (Role role : ROLES) if (find(role) == floater)
                {
                    if (!mRuntime[role].existed && !mCreated.count(role)) rememberRole(role);
                    mTouched.insert(role); registered = true; break;
                }
                if (!registered)
                {
                    rememberExtraInventory(floater);
                    for (auto& baseline : mExtraRuntime) if (baseline.state.handle.get() == floater) baseline.touched = true;
                }
            }
            auto saved = window(floater);
            if (automatic) for (const auto& prior : mRecoveryWindows) if (prior.first.get() == floater) { saved = prior.second; break; }
            saved.visible = true; saved.minimized = false;
            const auto desired = FSWorkspaceLayout::fit(saved, frame(), 1.f, 1.f).rect;
            const auto center_x = (desired.left + desired.right) * .5f, center_y = (desired.bottom + desired.top) * .5f;
            const auto selected = std::min_element(visible_frames.begin(), visible_frames.end(), [&](const FSWorkspaceLayout::Rect& a, const FSWorkspaceLayout::Rect& b)
            {
                auto distance = [&](const FSWorkspaceLayout::Rect& bounds)
                { const auto dx = center_x - std::clamp(center_x, bounds.left, bounds.right), dy = center_y - std::clamp(center_y, bounds.bottom, bounds.top); return dx * dx + dy * dy; };
                return distance(a) < distance(b);
            });
            saved = FSWorkspaceLayout::capture(desired, *selected, true, false);
            if (placeWindow(floater, saved, floater == find(Role::InventoryPrimary), placed, &*selected)) report(entry.second, "restored");
        }
    if (gViewerWindow) gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
    FSChromeLayoutController::instance().apply();
    mReport.complete = true; mReport.applied = mApplied; mReport.adjusted = mAdjusted; mReport.skipped = mSkipped;
    report("arrangement", "display_recovered"); ++mReportRevision; ++mRevision; mComparisonDirty = true;
    return true;
}

bool FSWorkspaceController::previousWorkspace(FSWorkspaceLayout::Workspace& workspace) const
{
    if (!hasPrevious()) return false;
    workspace = mPrevious; return true;
}
void FSWorkspaceController::acceptAssistantSwitch(std::function<bool()> allowed)
{
    mHUDReviewed = true; mHUDAllowed = std::move(allowed);
    if (mQuickSwitch) finishQuickSwitch();
}
