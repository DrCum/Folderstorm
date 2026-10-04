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
#include "llbutton.h"
#include "llcombobox.h"
#include "lllineeditor.h"
#include "llnotificationsutil.h"
#include "lltextbox.h"

static LLPanelInjector<FSPanelPreferenceWorkspaces> t_workspace_panel("panel_preference_workspaces");

bool FSPanelPreferenceWorkspaces::postBuild()
{
    const char* actions[] = {"preview", "save", "rename", "delete", "inventory_sorting", "driving", "add_inventory"};
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
                std::string label = name;
                if (name == "builtin:inventory_sorting") label = getString("sorting_name");
                else if (name == "builtin:driving") label = getString("driving_name");
                combo->add(label, name);
            }
            if (!prior.empty()) combo->setValue(prior);
            if (combo->getCurrentIndex() < 0 && !controller.activeId().empty()) combo->setValue(controller.activeId());
            if (combo->getCurrentIndex() < 0 && combo->getItemCount()) combo->selectFirstItem();
        }
        mObservedRevision = controller.revision();
    }
    combo->setEnabled(ready && combo->getItemCount());
    getChild<LLLineEditor>("workspace_name")->setEnabled(ready);
    getChild<LLButton>("workspace_preview")->setEnabled(ready && !selected().empty());
    getChild<LLButton>("workspace_save")->setEnabled(ready);
    getChild<LLButton>("workspace_rename")->setEnabled(ready && controller.isCustom(selected()));
    getChild<LLButton>("workspace_delete")->setEnabled(ready && controller.isCustom(selected()));
    for (const char* name : {"inventory_sorting", "driving", "add_inventory"})
        getChild<LLButton>(std::string("workspace_") + name)->setEnabled(ready);
    std::string status = getString(ready ? controller.status() : "unavailable");
    LLStringUtil::format_map_t args;
    args["[APPLIED]"] = std::to_string(controller.appliedCount());
    args["[ADJUSTED]"] = std::to_string(controller.adjustedCount());
    args["[SKIPPED]"] = std::to_string(controller.skippedCount());
    LLStringUtil::format(status, args);
    if (ready && controller.modified()) status += " " + getString("modified");
    getChild<LLTextBox>("workspace_status")->setText(status);
    std::string active = controller.activeId();
    if (active == "builtin:inventory_sorting") active = getString("sorting_name");
    else if (active == "builtin:driving") active = getString("driving_name");
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
        if (!controller.saveCurrent(label) && controller.status() == "exists") confirm("overwrite", label);
    }
    else if (name == "rename") controller.rename(selected(), enteredName());
    else if (name == "delete") confirm("delete", selected());
    else if (name == "add_inventory") controller.addInventoryWindow();
    else controller.preview("builtin:" + name);
    refresh();
}
void FSPanelPreferenceWorkspaces::confirm(const std::string& action, const std::string& name)
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.available() || !controller.isCustom(name)) return;
    const auto revision = controller.revision();
    const auto handle = getDerivedHandle<FSPanelPreferenceWorkspaces>();
    LLSD args;
    args["NAME"] = name;
    LLNotificationsUtil::add(action == "overwrite" ? "ConfirmWorkspaceOverwrite" : "ConfirmWorkspaceDelete",
        args, LLSD(), [handle, revision, action, name](const LLSD& notification, const LLSD& response)
        {
            auto* panel = handle.get();
            auto& controller = FSWorkspaceController::instance();
            if (!panel || !controller.available() || controller.revision() != revision ||
                LLNotificationsUtil::getSelectedOption(notification, response) != 0) return;
            if (action == "overwrite") controller.saveCurrent(name, true);
            else controller.remove(name);
            panel->refresh();
        });
}
