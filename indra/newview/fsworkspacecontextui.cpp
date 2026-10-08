/**
 * @file fsworkspacecontextui.cpp
 * @brief Shared optional workspace context controls
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
#include "fsworkspacecontextui.h"
#include "fsworkspacecontextadapter.h"
#include "llcombobox.h"
#include "llcheckboxctrl.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "llpresetsmanager.h"
#include "llbutton.h"
namespace FSWorkspaceContextUI
{
namespace
{
LLView* panel(LLView* view) { return view->getChild<LLView>("workspace_context"); }
LLSD hudValue(const FSWorkspaceContext::HUD& hud)
{ LLSD value; value["item"] = hud.item; value["name"] = hud.name; value["point"] = hud.point; return value; }
}
void initialize(LLView* view)
{
    auto* root = panel(view);
    for (bool camera : {false, true})
    {
        const std::string prefix = camera ? "camera" : "graphics";
        auto* presets = root->getChild<LLComboBox>(prefix + "_preset"); presets->removeall();
        LLPresetsManager::preset_name_list_t names;
        LLPresetsManager::instance().loadPresetNamesFromDir(camera ? PRESETS_CAMERA : PRESETS_GRAPHIC, names, DEFAULT_SHOW);
        for (const auto& name : names) presets->add(name, name);
        if (presets->getItemCount()) presets->selectFirstItem();
        root->getChild<LLComboBox>(prefix + "_mode")->setCommitCallback([root, prefix](LLUICtrl*, const LLSD&)
        { root->getChild<LLComboBox>(prefix + "_preset")->setEnabled(root->getChild<LLComboBox>(prefix + "_mode")->getValue().asString() == "preset"); });
    }
    root->getChild<LLButton>("hud_refresh")->setCommitCallback([view](LLUICtrl*, const LLSD&) { setOptions(view, options(view)); });
    root->getChild<LLButton>("hud_all")->setCommitCallback([root](LLUICtrl*, const LLSD&) { root->getChild<LLScrollListCtrl>("hud_list")->selectAll(); root->getChild<LLCheckBoxCtrl>("remember_huds")->set(true); });
    setOptions(view, {});
}
void setOptions(LLView* view, const FSWorkspaceContext::Options& choices)
{
    auto* root = panel(view);
    for (bool camera : {false, true})
    {
        const std::string prefix = camera ? "camera" : "graphics";
        const auto& group = camera ? choices.camera : choices.graphics;
        root->getChild<LLComboBox>(prefix + "_mode")->setValue(group.mode == FSWorkspaceContext::Mode::Off ? "off" : group.mode == FSWorkspaceContext::Mode::Current ? "current" : "preset");
        auto* preset = root->getChild<LLComboBox>(prefix + "_preset");
        if (!group.preset.empty()) { if (!preset->setSelectedByValue(group.preset, true)) preset->add(group.preset, group.preset); preset->setValue(group.preset); }
        preset->setEnabled(group.mode == FSWorkspaceContext::Mode::Preset);
    }
    auto* list = root->getChild<LLScrollListCtrl>("hud_list"); list->deleteAllItems();
    auto huds = FSWorkspaceContext::attachedHUDs();
    for (const auto& selected : choices.huds)
    {
        auto current = std::find_if(huds.begin(), huds.end(), [&](const FSWorkspaceContext::HUD& hud) { return hud.item == selected.item; });
        if (current == huds.end()) huds.push_back(selected);
        else *current = selected; // Keep the saved point/selection after an Inventory rename or manual reattachment.
    }
    for (const auto& hud : huds)
    {
        LLSD row; row["value"] = hudValue(hud); row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = hud.name; row["columns"][1]["column"] = "point"; row["columns"][1]["value"] = FSWorkspaceContext::hudPointName(hud.point);
        list->addElement(row);
    }
    for (const auto& hud : choices.huds) list->setSelectedByValue(hudValue(hud), true);
    root->getChild<LLCheckBoxCtrl>("remember_huds")->set(!choices.huds.empty());
}
FSWorkspaceContext::Options options(LLView* view)
{
    FSWorkspaceContext::Options result; auto* root = panel(view);
    for (bool camera : {false, true})
    {
        auto& group = camera ? result.camera : result.graphics;
        const std::string prefix = camera ? "camera" : "graphics";
        const auto mode = root->getChild<LLComboBox>(prefix + "_mode")->getValue().asString();
        group.mode = mode == "current" ? FSWorkspaceContext::Mode::Current : mode == "preset" ? FSWorkspaceContext::Mode::Preset : FSWorkspaceContext::Mode::Off;
        if (group.mode == FSWorkspaceContext::Mode::Preset) group.preset = root->getChild<LLComboBox>(prefix + "_preset")->getValue().asString();
    }
    if (root->getChild<LLCheckBoxCtrl>("remember_huds")->get())
        for (auto* row : root->getChild<LLScrollListCtrl>("hud_list")->getAllSelected())
        { const auto value = row->getValue(); result.huds.push_back({value["item"].asString(), value["name"].asString(), value["point"].asInteger()}); }
    return result;
}
}
