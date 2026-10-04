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
#include "llviewerprecompiledheaders.h"
#include "fsfloaterworkspaces.h"
#include "fschromelayoutcontroller.h"
#include "fsworkspacecontroller.h"
#include "fsworkspacequickaccess.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llfloaterpreference.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "llviewercontrol.h"

#include <algorithm>
#include <vector>

namespace
{
constexpr const char* FAVORITES_SETTING = FSWorkspaceQuickAccess::FAVORITES_SETTING;
constexpr const char* WORKSPACE_PREFIX = "workspace:";
constexpr const char* LAYOUT_PREFIX = "layout:";
bool isLayout(const std::string& key) { return key.compare(0, 7, LAYOUT_PREFIX) == 0; }
}

FSFloaterWorkspaces::FSFloaterWorkspaces(const LLSD& key) : LLFloater(key) {}
bool FSFloaterWorkspaces::postBuild()
{
    getChild<LLButton>("switch")->setCommitCallback([this](LLUICtrl*, const LLSD&) { switchSelected(); });
    getChild<LLButton>("favorite")->setCommitCallback([this](LLUICtrl*, const LLSD&) { toggleFavorite(); });
    getChild<LLButton>("manage")->setCommitCallback([this](LLUICtrl*, const LLSD&) { manageSelected(); });
    getChild<LLButton>("save")->setCommitCallback([this](LLUICtrl*, const LLSD&) { saveCurrent(); });
    getChild<LLLineEditor>("workspace_name")->setKeystrokeCallback([](LLLineEditor*, void* userdata)
    { static_cast<FSFloaterWorkspaces*>(userdata)->updateButtons(); }, this);
    auto* list = getChild<LLScrollListCtrl>("profiles");
    list->setCommitOnSelectionChange(true);
    list->setCommitCallback([this](LLUICtrl*, const LLSD&) { mActionStatus.clear(); updateButtons(); });
    list->setDoubleClickCallback([this]() { switchSelected(); });
    refresh(true);
    return LLFloater::postBuild();
}
void FSFloaterWorkspaces::onOpen(const LLSD& key)
{
    mActionStatus.clear();
    getChild<LLLineEditor>("workspace_name")->setText(LLStringExplicit(""));
    refresh(true);
}
void FSFloaterWorkspaces::draw()
{
    if (mRefreshTimer.getElapsedTimeF32() >= .5f)
    {
        refresh();
        mRefreshTimer.reset();
    }
    LLFloater::draw();
}
std::string FSFloaterWorkspaces::selected() const
{
    return getChild<LLScrollListCtrl>("profiles")->getValue().asString();
}
void FSFloaterWorkspaces::addHeading(const std::string& label)
{
    LLSD row;
    row["enabled"] = false;
    row["columns"][0]["column"] = "name";
    row["columns"][0]["value"] = label;
    row["columns"][0]["font-style"] = "BOLD";
    getChild<LLScrollListCtrl>("profiles")->addElement(row);
}
void FSFloaterWorkspaces::addEntry(const std::string& key, const std::string& label, bool layout)
{
    LLSD row;
    row["value"] = key;
    row["columns"][0]["column"] = "name";
    row["columns"][0]["value"] = label;
    row["columns"][1]["column"] = "kind";
    row["columns"][1]["value"] = getString(layout ? "layout_type" : "workspace_type");
    getChild<LLScrollListCtrl>("profiles")->addElement(row);
}
void FSFloaterWorkspaces::refresh(bool force)
{
    auto& controller = FSWorkspaceController::instance();
    const bool ready = controller.available();
    const LLSD profiles = ready ? gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles") : LLSD();
    const LLSD layouts = ready ? gSavedSettings.getLLSD("FSChromeLayoutProfiles") : LLSD();
    const LLSD favorites = ready ? gSavedPerAccountSettings.getLLSD(FAVORITES_SETTING) : LLSD();
    if (force || ready != mReady || gAgent.getID() != mAccount || gAgent.getSessionID() != mSession ||
        profiles != mProfiles || layouts != mLayouts || favorites != mFavoriteData || controller.revision() != mRevision)
    {
        const bool same_account = gAgent.getID() == mAccount && gAgent.getSessionID() == mSession;
        if (!same_account)
        {
            getChild<LLLineEditor>("workspace_name")->setText(LLStringExplicit(""));
            mActionStatus.clear(); mSavedName.clear();
        }
        auto* list = getChild<LLScrollListCtrl>("profiles");
        const auto prior = same_account ? selected() : "";
        const auto scroll = same_account ? list->getScrollPos() : 0;
        list->deleteAllItems();
        const auto entries = FSWorkspaceQuickAccess::entries();
        mFavorites = FSWorkspaceQuickAccess::favorites(entries);
        if (ready)
        {
            bool heading = false;
            for (const auto& entry : entries)
                if (mFavorites.count(entry.key))
                {
                    if (!heading) { addHeading(getString("favorites_heading")); heading = true; }
                    addEntry(entry.key, entry.label, entry.layout);
                }
            for (bool layout : {false, true})
            {
                heading = false;
                for (const auto& entry : entries)
                    if (entry.layout == layout && !mFavorites.count(entry.key))
                    {
                        if (!heading) { addHeading(getString(layout ? "layouts_heading" : "workspaces_heading")); heading = true; }
                        addEntry(entry.key, entry.label, layout);
                    }
            }
            if (prior.empty() || !list->setSelectedByValue(prior, true))
            {
                const auto active = controller.activeId();
                if (active.empty() || !list->setSelectedByValue(std::string(WORKSPACE_PREFIX) + active, true))
                    list->selectFirstItem();
            }
            list->setScrollPos(scroll);
        }
        mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
        mReady = ready; mProfiles = profiles; mLayouts = layouts; mFavoriteData = favorites;
        mRevision = controller.revision();
    }
    updateButtons();
}
void FSFloaterWorkspaces::updateButtons()
{
    const auto& controller = FSWorkspaceController::instance();
    const bool ready = controller.available();
    const bool can_switch = controller.canQuickSwitch();
    const bool has_selection = !selected().empty();
    getChild<LLButton>("switch")->setEnabled(can_switch && has_selection);
    getChild<LLButton>("favorite")->setEnabled(can_switch && has_selection);
    getChild<LLButton>("favorite")->setLabel(getString(mFavorites.count(selected()) ? "unfavorite_label" : "favorite_label"));
    getChild<LLButton>("manage")->setEnabled(ready);
    getChild<LLCheckBoxCtrl>("show_favorites_strip")->setEnabled(can_switch);
    std::string name = getChild<LLLineEditor>("workspace_name")->getText();
    LLStringUtil::trim(name);
    getChild<LLButton>("save")->setEnabled(can_switch && !name.empty());
    getChild<LLLineEditor>("workspace_name")->setEnabled(can_switch);
    getChild<LLScrollListCtrl>("profiles")->setEnabled(ready);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = mSavedName;
    getChild<LLTextBox>("status")->setText(getString(!ready ? "unavailable" : !can_switch ? "preferences_open" : mActionStatus.empty() ? "ready" : mActionStatus, args));
}
void FSFloaterWorkspaces::switchSelected()
{
    auto& controller = FSWorkspaceController::instance();
    const auto key = selected();
    if (!controller.canQuickSwitch() || key.empty() || gAgent.getID() != mAccount || gAgent.getSessionID() != mSession) return;
    const bool applied = FSWorkspaceQuickAccess::apply(key, mAccount, mSession);
    if (applied) closeFloater(false);
    else { mActionStatus = "switch_failed"; updateButtons(); }
}
void FSFloaterWorkspaces::toggleFavorite()
{
    if (!FSWorkspaceController::instance().canQuickSwitch() || selected().empty() ||
        gAgent.getID() != mAccount || gAgent.getSessionID() != mSession) return;
    const auto key = selected();
    if (mFavorites.count(key)) mFavorites.erase(key);
    else if (mFavorites.size() < 128) mFavorites.insert(key);
    LLSD data = LLSD::emptyArray();
    for (const auto& favorite : mFavorites) data.append(favorite);
    gSavedPerAccountSettings.setLLSD(FAVORITES_SETTING, data);
    refresh(true);
}
void FSFloaterWorkspaces::manageSelected()
{
    LLFloaterPreference::showWorkspaceSettings(isLayout(selected()));
}
void FSFloaterWorkspaces::saveCurrent()
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.canQuickSwitch() || gAgent.getID() != mAccount || gAgent.getSessionID() != mSession) return;
    std::string name = getChild<LLLineEditor>("workspace_name")->getText();
    LLStringUtil::trim(name);
    if (controller.saveCurrentNow(name))
    {
        refresh(true);
        auto* list = getChild<LLScrollListCtrl>("profiles");
        list->setSelectedByValue(std::string(WORKSPACE_PREFIX) + name, true);
        list->scrollToShowSelected();
        getChild<LLLineEditor>("workspace_name")->setText(LLStringExplicit(""));
        mSavedName = name;
        mActionStatus = "saved_immediately";
    }
    else mActionStatus = controller.status();
    updateButtons();
}
