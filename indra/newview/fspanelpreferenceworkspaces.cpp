/**
 * @file fspanelpreferenceworkspaces.cpp
 * @brief Manual workspace preferences and scoped confirmations
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
#include "fspanelpreferenceworkspaces.h"
#include "fsworkspacecontroller.h"
#include "fsworkspacecontextui.h"
#include "llbutton.h"
#include "llfloaterreg.h"
#include "llcheckboxctrl.h"
#include "llagent.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llnotificationsutil.h"
#include "lltextbox.h"


namespace {
int captureComponents(LLView* view)
{
    int mask = 0;
    if (view->getChild<LLCheckBoxCtrl>("workspace_capture_chrome")->get()) mask |= FSWorkspaceLayout::Chrome;
    if (view->getChild<LLCheckBoxCtrl>("workspace_capture_inventory")->get()) mask |= FSWorkspaceLayout::Inventory;
    if (view->getChild<LLCheckBoxCtrl>("workspace_capture_maps")->get()) mask |= FSWorkspaceLayout::Maps;
    if (view->getChild<LLCheckBoxCtrl>("workspace_capture_chat")->get()) mask |= FSWorkspaceLayout::Chat;
    const auto context = FSWorkspaceContextUI::options(view);
    if (context.graphics.mode != FSWorkspaceContext::Mode::Off) mask |= FSWorkspaceLayout::Graphics;
    if (context.camera.mode != FSWorkspaceContext::Mode::Off) mask |= FSWorkspaceLayout::Camera;
    if (!context.huds.empty()) mask |= FSWorkspaceLayout::HUDs;
    return mask;
}
}
static LLPanelInjector<FSPanelPreferenceWorkspaces> t_workspace_panel("panel_preference_workspaces");

bool FSPanelPreferenceWorkspaces::postBuild()
{
    FSWorkspaceContextUI::initialize(this);
    getChild<LLButton>("gesture_board_launcher")->setCommitCallback([](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("gesture_board"); });
    getChild<LLButton>("recover_windows")->setCommitCallback([](LLUICtrl*, const LLSD&) { FSWorkspaceController::instance().recoverWindows(); });
    const char* actions[] = {"preview", "save", "rename", "delete", "add_inventory"};
    for (const char* name : actions)
        getChild<LLButton>(std::string("workspace_") + name)->setCommitCallback(
            [this, name](LLUICtrl*, const LLSD&) { action(name); });
    getChild<LLComboBox>("workspace_combo")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        if (FSWorkspaceController::instance().isCustom(selected()))
            getChild<LLLineEditor>("workspace_name")->setText(selected());
        refresh();
    });
    refresh();
    return LLPanel::postBuild();
}

std::string FSPanelPreferenceWorkspaces::selected() const
{
    return getChild<LLComboBox>("workspace_combo")->getValue().asString();
}
std::string FSPanelPreferenceWorkspaces::enteredName() const
{
    auto name = getChild<LLLineEditor>("workspace_name")->getText();
    LLStringUtil::trim(name);
    return name;
}
void FSPanelPreferenceWorkspaces::draw()
{
    if (mRefreshTimer.getElapsedTimeF32() >= .5f)
    {
        refresh();
        mRefreshTimer.reset();
    }
    LLPanel::draw();
}
void FSPanelPreferenceWorkspaces::refresh()
{
    auto& controller = FSWorkspaceController::instance();
    controller.beginPreferencesSession();
    const bool ready = controller.available();
    auto* combo = getChild<LLComboBox>("workspace_combo");
    if (!ready || mObservedRevision != controller.revision() || combo->getItemCount() == 0)
    {
        const std::string prior = selected();
        combo->removeall();
        if (ready)
        {
            for (const auto& name : controller.names())
            {
                combo->add(name, name);
            }
            if (!prior.empty()) combo->setValue(prior);
            if (combo->getCurrentIndex() < 0 && !controller.activeId().empty()) combo->setValue(controller.activeId());
            if (combo->getCurrentIndex() < 0 && combo->getItemCount()) combo->selectFirstItem();
        }
        auto* startup = getChild<LLComboBox>("workspace_startup_name");
        const auto prior_startup = startup->getValue();
        startup->removeall();
        if (ready) for (const auto& name : controller.names()) startup->add(name, name);
        startup->setValue(prior_startup);
        mObservedRevision = controller.revision();
    }
    auto* remember = getChild<LLCheckBoxCtrl>("workspace_remember_folders");
    if (!ready || selected() != mFolderOptionWorkspace || gAgent.getID() != mFolderOptionAccount ||
        gAgent.getSessionID() != mFolderOptionSession)
    {
        remember->set(ready && controller.remembersInventoryFolders(selected()));
        FSWorkspaceLayout::Workspace saved;
        const int mask = ready && controller.readProfile(selected(), saved) ? saved.components : FSWorkspaceLayout::All;
        getChild<LLCheckBoxCtrl>("workspace_capture_chrome")->set((mask & FSWorkspaceLayout::Chrome) != 0);
        getChild<LLCheckBoxCtrl>("workspace_capture_inventory")->set((mask & FSWorkspaceLayout::Inventory) != 0);
        getChild<LLCheckBoxCtrl>("workspace_capture_maps")->set((mask & FSWorkspaceLayout::Maps) != 0);
        getChild<LLCheckBoxCtrl>("workspace_capture_chat")->set((mask & FSWorkspaceLayout::Chat) != 0);

        getChild<LLCheckBoxCtrl>("workspace_capture_toolbars")->set(saved.remember_toolbars);
        FSWorkspaceContextUI::setOptions(this, saved.context);
        mFolderOptionWorkspace = selected();
        mFolderOptionAccount = gAgent.getID(); mFolderOptionSession = gAgent.getSessionID();
    }
    getChild<LLView>("workspace_context")->setEnabled(ready);
    getChild<LLButton>("recover_windows")->setEnabled(ready);
    remember->setEnabled(ready && getChild<LLCheckBoxCtrl>("workspace_capture_inventory")->get());
    for (const char* name : {"workspace_capture_chrome", "workspace_capture_inventory", "workspace_capture_maps", "workspace_capture_chat", "workspace_capture_toolbars"}) getChild<LLCheckBoxCtrl>(name)->setEnabled(ready);
    getChild<LLComboBox>("workspace_startup_mode")->setEnabled(ready);
    getChild<LLComboBox>("workspace_startup_name")->setEnabled(ready && getChild<LLComboBox>("workspace_startup_mode")->getValue().asString() == "named");
    combo->setEnabled(ready && combo->getItemCount());
    getChild<LLLineEditor>("workspace_name")->setEnabled(ready);
    getChild<LLButton>("workspace_preview")->setEnabled(ready && !selected().empty());
    getChild<LLButton>("workspace_save")->setEnabled(ready && (captureComponents(this) != 0 || getChild<LLCheckBoxCtrl>("workspace_capture_toolbars")->get()));
    getChild<LLButton>("workspace_rename")->setEnabled(ready && controller.isCustom(selected()));
    getChild<LLButton>("workspace_delete")->setEnabled(ready && controller.isCustom(selected()));
    getChild<LLButton>("workspace_add_inventory")->setEnabled(ready);
    std::string status = getString(ready ? controller.status() : "unavailable");
    LLStringUtil::format_map_t args;
    args["[APPLIED]"] = std::to_string(controller.appliedCount());
    args["[ADJUSTED]"] = std::to_string(controller.adjustedCount());
    args["[SKIPPED]"] = std::to_string(controller.skippedCount());
    LLStringUtil::format(status, args);
    if (ready && controller.modified()) status += " " + getString("modified");
    getChild<LLTextBox>("workspace_status")->setText(status);
    std::string active = controller.activeId();
    if (active.empty()) active = getString("no_workspace");
    getChild<LLTextBox>("workspace_active")->setText(getString("active_label") + " " + active);
}
void FSPanelPreferenceWorkspaces::action(const std::string& name)
{
    auto& controller = FSWorkspaceController::instance();
    if (name == "preview") controller.preview(selected());
    else if (name == "save")
    {
        const auto label = enteredName();
        if (!controller.saveCurrent(label, false, getChild<LLCheckBoxCtrl>("workspace_remember_folders")->get(), captureComponents(this), getChild<LLCheckBoxCtrl>("workspace_capture_toolbars")->get(), FSWorkspaceContextUI::options(this)) && controller.status() == "exists") confirm("overwrite", label);
    }
    else if (name == "rename") controller.rename(selected(), enteredName());
    else if (name == "delete") confirm("delete", selected());
    else if (name == "add_inventory") controller.addInventoryWindow();
    refresh();
}
void FSPanelPreferenceWorkspaces::confirm(const std::string& action, const std::string& name)
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.available() || !controller.isCustom(name)) return;
    const auto revision = controller.revision();
    const bool remember_folders = getChild<LLCheckBoxCtrl>("workspace_remember_folders")->get();
    const int components = captureComponents(this);
    const bool toolbars = getChild<LLCheckBoxCtrl>("workspace_capture_toolbars")->get();
    const auto context = FSWorkspaceContextUI::options(this);
    const auto handle = getDerivedHandle<FSPanelPreferenceWorkspaces>();
    LLSD args;
    args["NAME"] = name;
    LLNotificationsUtil::add(action == "overwrite" ? "ConfirmWorkspaceOverwrite" : "ConfirmWorkspaceDelete",
        args, LLSD(), [handle, revision, action, name, remember_folders, components, toolbars, context](const LLSD& notification, const LLSD& response)
        {
            auto* panel = handle.get();
            auto& controller = FSWorkspaceController::instance();
            if (!panel || !controller.available() || controller.revision() != revision ||
                LLNotificationsUtil::getSelectedOption(notification, response) != 0) return;
            if (action == "overwrite") controller.saveCurrent(name, true, remember_folders, components, toolbars, context);
            else controller.remove(name);
            panel->refresh();
        });
}
