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
#include "fsworkspacefile.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "lltextbox.h"
#include "llcheckboxctrl.h"
#include "llviewercontrol.h"
#include "llfilepicker.h"
#include "llviewermenufile.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llcommandmanager.h"
#include "llnotificationsutil.h"
#include "llfloaterpreference.h"
#include <sstream>
#include <algorithm>

bool FSFloaterWorkspaceTools::postBuild()
{
    getChild<LLButton>("refresh_windows")->setCommitCallback([this](LLUICtrl*, const LLSD&) { refreshWindows(); });
    getChild<LLButton>("arrange")->setCommitCallback([this](LLUICtrl*, const LLSD&) { arrange(); });
    getChild<LLButton>("export")->setCommitCallback([this](LLUICtrl*, const LLSD&) { exportWorkspace(); });
    getChild<LLButton>("import")->setCommitCallback([this](LLUICtrl*, const LLSD&) { importWorkspace(); });
    getChild<LLButton>("apply_import")->setCommitCallback([this](LLUICtrl*, const LLSD&) { applyImport(); });
    getChild<LLButton>("startup_settings")->setCommitCallback([](LLUICtrl*, const LLSD&) { LLFloaterPreference::showWorkspaceSettings(false); });
    return LLFloater::postBuild();
}
void FSFloaterWorkspaceTools::onOpen(const LLSD& key)
{
    mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
    ++mPickerGeneration; mAccepted = LLSD(); mOriginals = LLSD();
    getChild<LLScrollListCtrl>("import_review")->deleteAllItems();
    getChild<LLCheckBoxCtrl>("include_folders")->set(false);
    getChild<LLCheckBoxCtrl>("replace_collisions")->set(false);
    getChild<LLButton>("apply_import")->setEnabled(false);
    auto* combo = getChild<LLComboBox>("export_profile"); combo->removeall();
    for (const auto& name : FSWorkspaceController::instance().names()) combo->add(name, name);
    const auto target = key.asString();
    combo->setValue(target.compare(0, 10, "workspace:") == 0 ? target.substr(10) : FSWorkspaceController::instance().activeId());
    if (combo->getCurrentIndex() < 0 && combo->getItemCount()) combo->selectFirstItem();
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

void FSFloaterWorkspaceTools::exportWorkspace()
{
    if (!currentSession()) return;
    const auto name = getChild<LLComboBox>("export_profile")->getValue().asString();
    FSWorkspaceLayout::Workspace saved;
    if (!FSWorkspaceController::instance().readProfile(name, saved)) return;
    if (!getChild<LLCheckBoxCtrl>("include_folders")->get())
    {
        saved.remember_inventory_folders = false;
        for (auto& entry : saved.windows) entry.second.inventory_folder = FSWorkspaceLayout::InventoryFolder{};
        for (auto& entry : saved.extra_inventory) entry.inventory_folder = FSWorkspaceLayout::InventoryFolder{};
    }
    LLSD envelope; envelope["format"] = "folderstorm-workspaces"; envelope["version"] = 1;
    envelope["profiles"][name] = FSWorkspaceLayout::toLLSD(saved);
    std::ostringstream stream; LLSDSerialize::toPrettyXML(envelope, stream);
    const auto document = stream.str();
    if (document.size() > FSWorkspaceFile::MAX_TRANSFER_BYTES) return;
    const auto handle = getDerivedHandle<FSFloaterWorkspaceTools>(); const auto generation = ++mPickerGeneration;
    LLFilePickerReplyThread::startPicker([handle, generation, document](const std::vector<std::string>& files,
        LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter)
    {
        auto* tools = handle.get();
        if (!tools || !tools->currentSession() || generation != tools->mPickerGeneration || files.empty()) return;
        llofstream out(files.front(), std::ios::binary | std::ios::trunc);
        out.write(document.data(), static_cast<std::streamsize>(document.size())); out.flush();
        tools->getChild<LLTextBox>("transfer_status")->setText(tools->getString(out.good() ? "exported" : "file_failed"));
    }, LLFilePicker::FFSAVE_XML, name + ".workspace.xml");
}
void FSFloaterWorkspaceTools::importWorkspace()
{
    if (!currentSession()) return;
    const auto handle = getDerivedHandle<FSFloaterWorkspaceTools>(); const auto generation = ++mPickerGeneration;
    LLFilePickerReplyThread::startPicker([handle, generation](const std::vector<std::string>& files,
        LLFilePicker::ELoadFilter, LLFilePicker::ESaveFilter)
    {
        auto* tools = handle.get();
        if (tools && tools->currentSession() && generation == tools->mPickerGeneration && !files.empty()) tools->reviewImport(files.front());
    }, LLFilePicker::FFLOAD_XML, false);
}
void FSFloaterWorkspaceTools::reviewImport(const std::string& filename)
{
    mAccepted = LLSD::emptyMap();
    auto* list = getChild<LLScrollListCtrl>("import_review"); list->deleteAllItems();
    getChild<LLButton>("apply_import")->setEnabled(false);
    llifstream input(filename, std::ios::binary | std::ios::ate);
    const auto size = input.tellg();
    if (!input.good() || size <= 0 || size > static_cast<std::streamoff>(FSWorkspaceFile::MAX_TRANSFER_BYTES))
    { getChild<LLTextBox>("transfer_status")->setText(getString("file_failed")); return; }
    input.seekg(0); std::string document(static_cast<size_t>(size), '\0');
    input.read(&document[0], static_cast<std::streamsize>(document.size()));
    LLSD envelope; std::istringstream stream(document);
    if (!input.good() || !FSWorkspaceFile::boundedXML(document) || LLSDSerialize::fromXML(envelope, stream) <= 0 ||
        !envelope.isMap() || envelope.size() != 3 || !envelope["format"].isString() ||
        envelope["format"].asString() != "folderstorm-workspaces" || !envelope["version"].isInteger() ||
        envelope["version"].asInteger() != 1 || !envelope["profiles"].isMap() ||
        envelope["profiles"].size() > FSWorkspaceLayout::MAX_PROFILES)
    { getChild<LLTextBox>("transfer_status")->setText(getString("invalid_file")); return; }
    mOriginals = gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles");
    mImportRevision = FSWorkspaceController::instance().revision();
    int skipped = 0, unavailable_commands = 0;
    const auto& profiles = envelope["profiles"];
    for (auto it = profiles.beginMap(); it != profiles.endMap(); ++it)
    {
        FSWorkspaceLayout::Workspace saved; std::string error;
        bool valid = FSWorkspaceLayout::isSafeProfileName(it->first) && FSWorkspaceLayout::fromLLSD(it->second, saved, error) && saved.ignored_details == 0;
        const bool collision = mOriginals.has(it->first);
        if (valid && collision)
        {
            FSWorkspaceLayout::Workspace existing;
            if (!FSWorkspaceLayout::fromLLSD(mOriginals[it->first], existing, error) || existing.ignored_details != 0) valid = false;
        }
        if (valid)
        {
            for (auto& bar : saved.toolbars)
                bar.commands.erase(std::remove_if(bar.commands.begin(), bar.commands.end(), [&](const std::string& command)
                { if (LLCommandManager::instance().getCommand(command)) return false; ++unavailable_commands; return true; }), bar.commands.end());
            mAccepted[it->first] = FSWorkspaceLayout::toLLSD(saved);
        }
        else ++skipped;
        LLSD row; row["columns"][0]["column"] = "name"; row["columns"][0]["value"] = it->first;
        row["columns"][1]["column"] = "result"; row["columns"][1]["value"] = getString(!valid ? "skipped_entry" : collision ? "collision" : "new_entry");
        list->addElement(row);
    }
    getChild<LLCheckBoxCtrl>("replace_collisions")->set(false);
    getChild<LLButton>("apply_import")->setEnabled(mAccepted.size() != 0);
    getChild<LLTextBox>("transfer_status")->setText("Review: " + std::to_string(mAccepted.size()) + " valid, " + std::to_string(skipped) + " skipped, " + std::to_string(unavailable_commands) + " unavailable toolbar commands omitted. Existing names are kept by default.");
}
void FSFloaterWorkspaceTools::applyImport()
{
    if (!currentSession() || !mAccepted.isMap() || mAccepted.size() == 0 ||
        FSWorkspaceController::instance().revision() != mImportRevision) return;
    const bool replace = getChild<LLCheckBoxCtrl>("replace_collisions")->get();
    const auto handle = getDerivedHandle<FSFloaterWorkspaceTools>();
    const auto accepted = mAccepted, originals = mOriginals; const auto revision = mImportRevision;
    const auto generation = mPickerGeneration;
    LLNotificationsUtil::add("ConfirmWorkspaceImport", LLSD(), LLSD(),
        [handle, accepted, originals, revision, generation, replace](const LLSD& notification, const LLSD& response)
        {
            auto* tools = handle.get(); auto& controller = FSWorkspaceController::instance();
            if (!tools || !tools->currentSession() || controller.revision() != revision || generation != tools->mPickerGeneration ||
                LLNotificationsUtil::getSelectedOption(notification, response) != 0) return;
            const bool imported = controller.importProfiles(accepted, originals, replace);
            tools->getChild<LLTextBox>("transfer_status")->setText(tools->getString(imported ? "imported" : "import_failed"));
            tools->getChild<LLButton>("apply_import")->setEnabled(false);
            if (imported) tools->mAccepted = LLSD();
        });
}

void FSFloaterWorkspaceTools::draw()
{
    const bool valid = currentSession();
    for (const char* name : {"arrange", "refresh_windows", "export", "import", "apply_import", "snapping", "include_folders", "replace_collisions", "windows", "alignment", "export_profile"})
        getChild<LLUICtrl>(name)->setEnabled(valid && (std::string(name) != "apply_import" || (mAccepted.isMap() && mAccepted.size() != 0)));
    if (mAccount != gAgent.getID() || mSession != gAgent.getSessionID())
    {
        mWindows.clear(); mAccepted = LLSD(); mOriginals = LLSD(); ++mPickerGeneration;
        getChild<LLScrollListCtrl>("windows")->deleteAllItems();
        getChild<LLScrollListCtrl>("import_review")->deleteAllItems();
        getChild<LLComboBox>("export_profile")->removeall();
        getChild<LLTextBox>("transfer_status")->setText("");
    }
    LLFloater::draw();
}
