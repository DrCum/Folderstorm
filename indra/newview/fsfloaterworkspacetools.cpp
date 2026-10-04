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
#include "fsfloaterworkspacetools.h"
#include "fsworkspacecontroller.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"

bool FSFloaterWorkspaceTools::postBuild()
{
    getChild<LLButton>("refresh_windows")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshWindows(); });
    getChild<LLButton>("arrange")->setCommitCallback([this](LLUICtrl*, const LLSD&) { arrange(); });
    return LLFloater::postBuild();
}
void FSFloaterWorkspaceTools::onOpen(const LLSD& key)
{
    mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
    refreshWindows();
}
bool FSFloaterWorkspaceTools::currentSession() const
{
    return mAccount == gAgent.getID() && mSession == gAgent.getSessionID() && FSWorkspaceController::instance().canQuickSwitch();
}
void FSFloaterWorkspaceTools::refreshWindows()
{
    auto* list = getChild<LLScrollListCtrl>("windows");
    list->deleteAllItems(); mWindows.clear();
    if (!currentSession()) return;
    for (const auto& item : FSWorkspaceController::instance().utilityWindows())
    {
        LLSD row; row["value"] = static_cast<S32>(mWindows.size());
        row["columns"][0]["column"] = "name"; row["columns"][0]["value"] = item.second;
        list->addElement(row); mWindows.push_back(item.first);
    }
}
void FSFloaterWorkspaceTools::arrange()
{
    if (!currentSession()) return;
    std::vector<LLHandle<LLFloater>> selection;
    for (const auto* row : getChild<LLScrollListCtrl>("windows")->getAllSelected())
    {
        const S32 index = row->getValue().asInteger();
        if (index >= 0 && static_cast<size_t>(index) < mWindows.size()) selection.push_back(mWindows[index]);
    }
    const bool applied = FSWorkspaceController::instance().arrange(selection, getChild<LLComboBox>("alignment")->getValue().asInteger());
    getChild<LLTextBox>("status")->setText(applied ? getString("arranged") : getString("choose_windows"));
}
