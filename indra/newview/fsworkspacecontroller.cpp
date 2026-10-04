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
        af.view_mode == bf.view_mode && af.folder_id == bf.folder_id);
    return equal_folder && (host || (a.visible == b.visible && a.minimized == b.minimized)) &&
        a.has_geometry == b.has_geometry && (!a.has_geometry ||
        (std::fabs(a.center_x - b.center_x) < .002f && std::fabs(a.center_y - b.center_y) < .002f &&
         std::fabs(a.width_ui - b.width_ui) <= 1.f && std::fabs(a.height_ui - b.height_ui) <= 1.f));
}
}

FSWorkspaceController& FSWorkspaceController::instance()
{
    static FSWorkspaceController controller;
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
void FSWorkspaceController::abandon()
{
    ++mGeneration;
    ++mRevision;
    mPendingPlacement = false;
    mRestoreLayout = false;
    mQuickSwitch = false;
    mTransaction = false;
    mRuntime.clear(); mControls.clear(); mPendingHandles.clear(); mTouched.clear(); mCreated.clear();
    mExtraControls.clear(); mExtraRuntime.clear(); mPendingExtraInventory.clear();
    mFocus.markDead();
    mProfiles = LLSD(); mActive.clear();
    mInboxTouched = mInboxBaselineValid = false;
}
FSWorkspaceLayout::Workspace FSWorkspaceController::capture(bool remember_folders) const
{
    FSWorkspaceLayout::Workspace result;
    result.remember_inventory_folders = remember_folders;
    result.chrome = FSChromeLayoutController::instance().captureSnapshot();
    result.world_view_in_mouselook = gSavedSettings.getBOOL("FSWorldViewInMouselook");
    result.frame_width = frame().width(); result.frame_height = frame().height();
    // Capture registered utility roles and additional Inventory instances only.
    // Preferences, the workspace switcher and other management windows are excluded.
    for (Role role : ROLES)
    {
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
    if (auto* panel = inbox(find(Role::InventoryPrimary)))
    {
        S32 height = 0;
        result.has_inbox = panel->captureWorkspaceInbox(result.inbox_expanded, height);
        if (result.has_inbox) result.inbox_height = static_cast<F32>(height);
    }
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
    mBaseline = capture();
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
    rememberPrevious();
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
    const auto prior = capture(true);
    const auto workspace = activeId();
    const auto layout = gSavedSettings.getString("FSChromeActiveProfile");
    abandon();
    if (!FSChromeLayoutController::instance().applyProfile(id)) return false;
    mPrevious = prior; mPreviousWorkspace = workspace; mPreviousLayout = layout;
    mPreviousAccount = gAgent.getID(); mPreviousSession = gAgent.getSessionID(); mHasPrevious = true;
    gSavedPerAccountSettings.setString("FSActiveWorkspace", "");
    return true;
}
void FSWorkspaceController::finishQuickSwitch()
{
    if (!mQuickSwitch) return;
    if (!sameSession() || !available()) { abandon(); return; }
    finishPlacement();
    // Save only the chosen identifier. Definitions remain unchanged.
    gSavedPerAccountSettings.setString("FSActiveWorkspace", mActive);
    if (mRestoreLayout) gSavedSettings.setString("FSChromeActiveProfile", mRestoredLayout);
    abandon();
}
void FSWorkspaceController::rememberPrevious()
{
    mPrevious = capture(true);
    mPreviousWorkspace = activeId();
    mPreviousLayout = gSavedSettings.getString("FSChromeActiveProfile");
    mPreviousAccount = gAgent.getID(); mPreviousSession = gAgent.getSessionID();
    mHasPrevious = true;
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
    rememberPrevious(); // Swap, including unsaved positions; never change named definitions.
    abandon();
    beginPreferencesSession();
    if (!startPreview(target, workspace)) return false;
    mQuickSwitch = true; mRestoreLayout = true; mRestoredLayout = layout;
    mFocus.markDead();
    return true;
}
bool FSWorkspaceController::saveCurrentNow(const std::string& name, bool remember_folders)
{
    if (!canQuickSwitch()) return false;
    if (mQuickSwitch) finishQuickSwitch();
    abandon();
    // Reuse strict capture/validation, allowing only creation from this UI.
    // Pending Preferences edits are excluded by the guard above.
    if (!saveCurrent(name, false, remember_folders)) { abandon(); return false; }
    gSavedPerAccountSettings.setLLSD("FSWorkspaceProfiles", mProfiles);
    gSavedPerAccountSettings.setString("FSActiveWorkspace", mActive);
    mStatus = "saved_immediately";
    abandon();
    return true;
}
bool FSWorkspaceController::saveCurrent(const std::string& name, bool overwrite, bool remember_folders)
{
    beginPreferencesSession();
    if (!available() || !mTransaction) { mStatus = "unavailable"; return false; }
    finishPlacement();
    if (remember_folders && !gInventory.isInventoryUsable()) { mStatus = "inventory_not_ready"; return false; }
    std::string error;
    if (!FSWorkspaceLayout::canSaveProfile(mProfiles, name, error)) { mStatus = "invalid_name_or_data"; return false; }
    if (mProfiles.has(name) && !overwrite) { mStatus = "exists"; return false; }
    const auto current = capture(remember_folders);
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
    if (mActive == old_name) mActive = new_name;
    mStatus = "renamed"; ++mRevision; return true;
}
bool FSWorkspaceController::remove(const std::string& name)
{
    beginPreferencesSession();
    if (!available() || !isCustom(name)) return false;
    mProfiles.erase(name); if (mActive == name) mActive.clear();
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
    mPending = workspace; mFolderSkipped = mChatSkipped = mDependentSkipped = false; mApplied = mAdjusted = 0; mSkipped = workspace.ignored_details;
    mFocus.markDead();
    if (auto* focus = dynamic_cast<LLView*>(gFocusMgr.getKeyboardFocus())) mFocus = focus->getDerivedHandle<LLView>();
    FSChromeLayoutController::instance().applySnapshot(workspace.chrome);
    gSavedSettings.setBOOL("FSWorldViewInMouselook", workspace.world_view_in_mouselook);
    gSavedSettings.setString("FSChromeActiveProfile", "");
    if (gViewerWindow) gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
    FSChromeLayoutController::instance().apply();
    for (const auto& entry : workspace.windows)
    {
        const Role role = entry.first;
        LLFloater* floater = find(role);
        if ((role == Role::NearbyChat && (!chatCompatible(floater) ||
             !find(Role::ConversationsGeometry))) ||
            (role == Role::ConversationsGeometry && (!standalone(floater) || floater->isMinimized())) ||
            (floater && !standalone(floater)))
        { ++mSkipped; if (role == Role::NearbyChat) mChatSkipped = true; continue; }
        // Moving/minimizing/closing utility parents would propagate to
        // untracked Filters/other windows. Keep those arrangements intact.
        if (floater && (floater->hasWorkspaceDependents() || floater->isDependent()))
        { ++mSkipped; mDependentSkipped = true; continue; }
        // Adopt a newly opened allowed utility's current state as its rollback
        // baseline. Additional Inventory instances have their own baseline below.
        if (!mRuntime[role].existed && floater && !mCreated.count(role)) rememberRole(role);
        const bool needs_open = role != Role::ConversationsGeometry && entry.second.visible &&
            (!floater || !floater->LLView::getVisible());
        if (needs_open && !LLFloaterReg::canShowInstance(registryName(role))) { ++mSkipped; continue; }
        if (needs_open)
        {
            const bool created = !floater;
            mTouched.insert(role);
            floater = LLFloaterReg::showInstance(registryName(role), LLSD(), false);
            if (created && floater) mCreated.insert(role);
        }
        if (!floater) continue; // hidden saved windows never construct anything
        if (!standalone(floater) || (role == Role::NearbyChat && !chatCompatible(floater))) { ++mSkipped; continue; }
        mTouched.insert(role);
        mPendingHandles[role] = floater->getDerivedHandle<LLFloater>();
    }
    auto extras = extraInventoryFloaters();
    // Reuse registered instances in the same order used by capture, including
    // hidden windows from earlier switches. Folder restoration is optional.
    const size_t count = std::max(extras.size(), workspace.extra_inventory.size());
    for (size_t i = 0; i < count; ++i)
    {
        const auto saved = i < workspace.extra_inventory.size() ? workspace.extra_inventory[i] : FSWorkspaceLayout::Window{};
        LLFloater* floater = i < extras.size() ? extras[i] : nullptr;
        if (floater && (floater->hasWorkspaceDependents() || floater->isDependent()))
        { ++mSkipped; mDependentSkipped = true; continue; }
        if (saved.visible && !LLFloaterReg::canShowInstance(floater ? floater->getInstanceName() : "inventory",
                                                         floater ? floater->getKey() : LLSD()))
        { ++mSkipped; continue; }
        const bool created = !floater;
        if (!floater && saved.visible)
        {
            rememberExtraInventoryControls("inventory");
            floater = LLPanelMainInventory::newWindow();
            if (!floater) { ++mSkipped; continue; }
        }
        if (!floater) continue;
        rememberExtraInventory(floater, created);
        for (auto& baseline : mExtraRuntime)
            if (baseline.state.handle.get() == floater) { baseline.touched = true; break; }
        mPendingExtraInventory.push_back({floater->getDerivedHandle<LLFloater>(), saved});
    }
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
    bool primary_inventory, std::vector<LLRect>& placed)
{
    if (!saved.has_geometry) return true;
    S32 min_width, min_height; floater->getResizeLimits(&min_width, &min_height);
    const auto placement = FSWorkspaceLayout::fit(saved, frame(), static_cast<F32>(min_width), static_cast<F32>(min_height));
    LLRect target(ll_round(placement.rect.left), ll_round(placement.rect.top),
                  ll_round(placement.rect.right), ll_round(placement.rect.bottom));
    // Tiny frames may require overlap. Stagger coincident title edges.
    for (const auto& prior : placed)
        if (std::abs(target.mTop - prior.mTop) < 8 && std::abs(target.mLeft - prior.mLeft) < 24)
            target.translate(16, -24);
    if (!floater->applyWorkspaceRect(target, primary_inventory)) return false;
    gFloaterView->adjustToFitScreen(floater, false);
    // The ordinary fitter may align oversized minima on the left/bottom.
    // Keep the right-hand close control and title edge reachable instead.
    LLRect final_rect = floater->getRect();
    const auto client = frame();
    if (final_rect.getWidth() > client.width()) final_rect.translate(ll_round(client.right) - final_rect.mRight, 0);
    if (final_rect.getHeight() > client.height()) final_rect.translate(0, ll_round(client.top) - final_rect.mTop);
    floater->applyWorkspaceRect(final_rect, primary_inventory);
    if (placement.adjusted || floater->getRect() != target) ++mAdjusted;
    placed.push_back(floater->getRect());
    return true;
}
void FSWorkspaceController::applyInventoryFolder(LLFloater* floater, const FSWorkspaceLayout::Window& saved,
    RuntimeBaseline& baseline)
{
    if (!saved.visible || !saved.inventory_folder.present) return;
    auto* panel = inventoryPanel(floater);
    if (!panel || !panel->applyWorkspaceFolder(saved.inventory_folder))
    {
        ++mSkipped;
        mFolderSkipped = true;
    }
    else baseline.folder_touched = true;
}
void FSWorkspaceController::finishPlacement()
{
    if (!mPendingPlacement || !sameSession() || !available()) return;
    mPendingPlacement = false;
    std::vector<LLRect> placed;
    for (const auto& entry : mPendingHandles)
    {
        Role role = entry.first;
        LLFloater* floater = entry.second.get();
        const auto& saved = mPending.windows.at(role);
        if (!standalone(floater) || (role == Role::NearbyChat && !chatCompatible(floater)) ||
            (role == Role::ConversationsGeometry && floater->isMinimized())) { ++mSkipped; continue; }
        if (floater->hasWorkspaceDependents() || floater->isDependent())
        { ++mSkipped; mDependentSkipped = true; continue; }
        const bool host = role == Role::ConversationsGeometry;
        // Restrictions may change between opening the window and this idle pass.
        if (!host && saved.visible && !LLFloaterReg::canShowInstance(registryName(role)))
        { ++mSkipped; continue; }
        if (!host) floater->setMinimized(false);
        if (role == Role::InventoryPrimary || role == Role::InventoryExtra1)
            applyInventoryFolder(floater, saved, mRuntime[role]);
        if (!placeWindow(floater, saved, role == Role::InventoryPrimary, placed)) { ++mSkipped; continue; }
        if (!host)
        {
            if (!saved.visible) floater->closeFloater(false);
            else if (role != Role::NearbyChat) floater->setMinimized(saved.minimized);
        }
        ++mApplied;
    }
    for (const auto& entry : mPendingExtraInventory)
    {
        LLFloater* floater = entry.handle.get();
        if (!standalone(floater)) { ++mSkipped; continue; }
        if (floater->hasWorkspaceDependents() || floater->isDependent())
        { ++mSkipped; mDependentSkipped = true; continue; }
        const auto& saved = entry.window;
        if (saved.visible && !LLFloaterReg::canShowInstance(floater->getInstanceName(), floater->getKey()))
        { ++mSkipped; continue; }
        floater->setMinimized(false);
        for (auto& baseline : mExtraRuntime)
            if (baseline.state.handle.get() == floater)
            {
                applyInventoryFolder(floater, saved, baseline.state);
                break;
            }
        if (!placeWindow(floater, saved, false, placed)) { ++mSkipped; continue; }
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
        if (!panel || !panel->applyWorkspaceInbox(mPending.inbox_expanded, ll_round(mPending.inbox_height))) ++mSkipped;
        else mInboxTouched = true;
    }
    if (auto* focus = dynamic_cast<LLFocusableElement*>(mFocus.get())) gFocusMgr.setKeyboardFocus(focus);
    mStatus = mFolderSkipped ? "applied_folders_skipped" : mDependentSkipped ? "applied_dependents_skipped" : mChatSkipped ? "applied_chat_skipped" : "applied";
    if (mFolderSkipped && mQuickSwitch) LLNotificationsUtil::add("WorkspaceInventoryFolderSkipped");
}
void FSWorkspaceController::commitPendingSettings()
{
    if (!mTransaction) return;
    if (!sameSession() || !available()) { abandon(); return; }
    // Explicit OK settles its one pending placement before refreshing baseline.
    finishPlacement();
    ++mGeneration;
    gSavedPerAccountSettings.setLLSD("FSWorkspaceProfiles", mProfiles);
    gSavedPerAccountSettings.setString("FSActiveWorkspace", mActive);
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
    if (!sameSession() || !available()) { abandon(); return; }
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
bool FSWorkspaceController::modified() const
{
    if (!mTransaction || !available() || !sameSession() || mPendingPlacement || mActive.empty()) return false;
    FSWorkspaceLayout::Workspace saved;
    std::string error;
    if (!isCustom(mActive) || !FSWorkspaceLayout::fromLLSD(mProfiles[mActive], saved, error)) return false;
    const auto current = capture(saved.remember_inventory_folders);
    if (FSChromeLayoutController::snapshotToLLSD(saved.chrome) != FSChromeLayoutController::snapshotToLLSD(current.chrome) ||
        saved.world_view_in_mouselook != current.world_view_in_mouselook) return true;
    for (const auto& entry : saved.windows)
    {
        auto it = current.windows.find(entry.first);
        if (it == current.windows.end() || !equalWindow(entry.second, it->second, entry.first == Role::ConversationsGeometry)) return true;
    }
    if (saved.extra_inventory.size() != current.extra_inventory.size()) return true;
    for (size_t i = 0; i < saved.extra_inventory.size(); ++i)
        if (!equalWindow(saved.extra_inventory[i], current.extra_inventory[i], false)) return true;
    return saved.has_inbox && (!current.has_inbox || saved.inbox_expanded != current.inbox_expanded ||
        std::fabs(saved.inbox_height - current.inbox_height) > 1.f);
}
