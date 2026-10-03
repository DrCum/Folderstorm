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
    return floater && !floater->getHost() && floater->getParent() == gFloaterView;
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
bool chatCompatible(LLFloater* floater)
{
    return standalone(floater) && gSavedSettings.getBOOL("ChatHistoryTornOff");
}
std::vector<std::string> controlsFor(Role role)
{
    const std::string name = LLFloaterReg::getBaseControlName(registryName(role));
    std::vector<std::string> result = {"floater_rect_" + name,
        "floater_pos_" + name + "_x", "floater_pos_" + name + "_y"};
    if (role != Role::ConversationsGeometry) result.push_back("floater_vis_" + name);
    if (role == Role::InventoryPrimary)
    {
        result.push_back("InventoryInboxToggleState");
        result.push_back("InventoryInboxHeight");
    }
    return result;
}
bool equalWindow(const FSWorkspaceLayout::Window& a, const FSWorkspaceLayout::Window& b, bool host)
{
    return (host || (a.visible == b.visible && a.minimized == b.minimized)) &&
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
    mTransaction = false;
    mRuntime.clear(); mControls.clear(); mPendingHandles.clear(); mTouched.clear(); mCreated.clear();
    mFocus.markDead();
    mProfiles = LLSD(); mActive.clear();
    mInboxTouched = mInboxBaselineValid = false;
}
FSWorkspaceLayout::Workspace FSWorkspaceController::capture() const
{
    FSWorkspaceLayout::Workspace result;
    result.chrome = FSChromeLayoutController::instance().captureSnapshot();
    result.world_view_in_mouselook = gSavedSettings.getBOOL("FSWorldViewInMouselook");
    result.frame_width = frame().width(); result.frame_height = frame().height();
    for (Role role : ROLES)
    {
        LLFloater* floater = find(role);
        if (role == Role::NearbyChat && !chatCompatible(floater)) continue;
        if (role == Role::ConversationsGeometry && !standalone(floater)) continue;
        if (floater && !standalone(floater)) continue;
        result.windows[role] = floater ? window(floater) : FSWorkspaceLayout::Window{};
    }
    if (auto* panel = inbox(find(Role::InventoryPrimary)))
    {
        S32 height = 0;
        result.has_inbox = panel->captureWorkspaceInbox(result.inbox_expanded, height);
        if (result.has_inbox) result.inbox_height = height;
    }
    return result;
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
    if (!available() || mTransaction) return;
    mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
    mProfiles = gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    mActive = gSavedPerAccountSettings.getString("FSActiveWorkspace");
    mBaseline = capture();
    for (Role role : ROLES) rememberRole(role);
    mTransaction = true;
    ++mRevision;
}
std::vector<std::string> FSWorkspaceController::names() const
{
    std::vector<std::string> result;
    if (mProfiles.isMap())
        for (auto it = mProfiles.beginMap(); it != mProfiles.endMap() && result.size() < FSWorkspaceLayout::MAX_PROFILES; ++it)
            if (FSWorkspaceLayout::isSafeProfileName(it->first)) result.push_back(it->first);
    return result;
}
bool FSWorkspaceController::isCustom(const std::string& id) const
{
    return mTransaction && mProfiles.isMap() && mProfiles.has(id) && !FSWorkspaceLayout::isBuiltinProfileId(id);
}
bool FSWorkspaceController::saveCurrent(const std::string& name, bool overwrite)
{
    beginPreferencesSession();
    if (!available() || !mTransaction) { mStatus = "unavailable"; return false; }
    finishPlacement();
    std::string error;
    if (!FSWorkspaceLayout::canSaveProfile(mProfiles, name, error)) { mStatus = "invalid_name_or_data"; return false; }
    if (mProfiles.has(name) && !overwrite) { mStatus = "exists"; return false; }
    const auto current = capture();
    FSWorkspaceLayout::Workspace validated;
    const LLSD data = FSWorkspaceLayout::toLLSD(current);
    if (!FSWorkspaceLayout::fromLLSD(data, validated, error)) { mStatus = "invalid_data"; return false; }
    mProfiles[name] = data; mActive = name; mStatus = "saved"; ++mRevision;
    return true;
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
FSWorkspaceLayout::Workspace FSWorkspaceController::startingArrangement(const std::string& id) const
{
    auto result = capture();
    const auto area = frame();
    auto place = [&](Role role, float cx, float cy, float width, float height)
    {
        FSWorkspaceLayout::Window value;
        value.visible = true; value.has_geometry = true;
        value.center_x = cx; value.center_y = cy; value.width_ui = width; value.height_ui = height;
        result.windows[role] = value;
    };
    auto hide = [&](Role role) { result.windows[role].visible = false; result.windows[role].minimized = false; };
    if (id == "builtin:inventory_sorting")
    {
        // Use viewport utility gutters when they can hold a readable inventory;
        // otherwise use the corresponding side of the full UI client.
        const float left = result.chrome.viewport_enabled ? result.chrome.inset_left * .01f : 0.f;
        const float right = result.chrome.viewport_enabled ? result.chrome.inset_right * .01f : 0.f;
        const float width = std::min(370.f, area.width() * .42f);
        const float height = std::min(650.f, area.height() * .85f);
        place(Role::InventoryPrimary, right * area.width() >= 330.f ? 1.f - right / 2.f : .78f, .5f, width, height);
        place(Role::InventoryExtra1, left * area.width() >= 330.f ? left / 2.f : .22f, .5f, width, height);
        hide(Role::MiniMap); hide(Role::WorldMap);
    }
    else
    {
        place(Role::MiniMap, std::min(.5f, 125.f / area.width()), std::max(.5f, 1.f - 125.f / area.height()), 220.f, 220.f);
        hide(Role::InventoryPrimary); hide(Role::InventoryExtra1); hide(Role::WorldMap);
        if (chatCompatible(find(Role::NearbyChat))) place(Role::NearbyChat, .25f, .2f, 400.f, 220.f);
    }
    return result;
}
bool FSWorkspaceController::preview(const std::string& id)
{
    beginPreferencesSession();
    if (!available() || !mTransaction) { mStatus = "unavailable"; return false; }
    FSWorkspaceLayout::Workspace workspace;
    std::string error;
    if (FSWorkspaceLayout::isBuiltinProfileId(id) && id != "builtin:inventory_sorting" && id != "builtin:driving")
    { mStatus = "invalid_data"; return false; }
    const LLSD& profiles = mProfiles; // Lookup must not insert a missing definition.
    const LLSD data = FSWorkspaceLayout::isBuiltinProfileId(id) ? FSWorkspaceLayout::toLLSD(startingArrangement(id)) : profiles[id];
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
    mPending = workspace; mChatSkipped = mDependentSkipped = false; mApplied = mAdjusted = 0; mSkipped = workspace.ignored_details;
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
        // baseline. Arbitrary keyed/manual extra Inventory windows are excluded.
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
        if (!host) floater->setMinimized(false);
        if (saved.has_geometry)
        {
            S32 min_width, min_height; floater->getResizeLimits(&min_width, &min_height);
            const auto placement = FSWorkspaceLayout::fit(saved, frame(), min_width, min_height);
            LLRect target(ll_round(placement.rect.left), ll_round(placement.rect.top),
                          ll_round(placement.rect.right), ll_round(placement.rect.bottom));
            // Tiny frames may require overlap. Stagger coincident title edges.
            for (const auto& prior : placed)
                if (std::abs(target.mTop - prior.mTop) < 8 && std::abs(target.mLeft - prior.mLeft) < 24)
                    target.translate(16, -24);
            if (!floater->applyWorkspaceRect(target, role == Role::InventoryPrimary)) { ++mSkipped; continue; }
            gFloaterView->adjustToFitScreen(floater, false);
            // The ordinary fitter may align oversized minima on the left/bottom.
            // Keep the right-hand close control and title edge reachable instead.
            LLRect final_rect = floater->getRect();
            const auto client = frame();
            if (final_rect.getWidth() > client.width()) final_rect.translate(ll_round(client.right) - final_rect.mRight, 0);
            if (final_rect.getHeight() > client.height()) final_rect.translate(0, ll_round(client.top) - final_rect.mTop);
            floater->applyWorkspaceRect(final_rect, role == Role::InventoryPrimary);
            if (placement.adjusted || floater->getRect() != target) ++mAdjusted;
            placed.push_back(floater->getRect());
        }
        if (!host)
        {
            if (!saved.visible) floater->closeFloater(false);
            else if (role != Role::NearbyChat) floater->setMinimized(saved.minimized);
        }
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
    mStatus = mDependentSkipped ? "applied_dependents_skipped" : mChatSkipped ? "applied_chat_skipped" : "applied";
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
        S32 min_width, min_height; floater->getResizeLimits(&min_width, &min_height);
        auto placement = FSWorkspaceLayout::fit(baseline.window, frame(), min_width, min_height);
        const bool host = role == Role::ConversationsGeometry;
        const bool may_show = host || !baseline.window.visible || LLFloaterReg::canShowInstance(registryName(role));
        floater->restoreWorkspaceState(LLRect(ll_round(placement.rect.left), ll_round(placement.rect.top),
            ll_round(placement.rect.right), ll_round(placement.rect.bottom)),
            host ? floater->LLView::getVisible() : baseline.window.visible && may_show,
            host ? floater->isMinimized() : baseline.window.minimized, baseline.positioning, role == Role::InventoryPrimary, host);
    }
    if (mInboxTouched && mInboxBaselineValid)
        if (auto* panel = inbox(find(Role::InventoryPrimary)))
            panel->applyWorkspaceInbox(mInboxBaselineExpanded, mInboxBaselineHeight);
    // Restore controls last: close/destroy and Inbox destructors also persist.
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
    const auto current = capture();
    if (FSChromeLayoutController::snapshotToLLSD(saved.chrome) != FSChromeLayoutController::snapshotToLLSD(current.chrome) ||
        saved.world_view_in_mouselook != current.world_view_in_mouselook) return true;
    for (const auto& entry : saved.windows)
    {
        auto it = current.windows.find(entry.first);
        if (it == current.windows.end() || !equalWindow(entry.second, it->second, entry.first == Role::ConversationsGeometry)) return true;
    }
    return saved.has_inbox && (!current.has_inbox || saved.inbox_expanded != current.inbox_expanded ||
        std::fabs(saved.inbox_height - current.inbox_height) > 1.f);
}
