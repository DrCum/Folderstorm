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
#include "fsfloaterinventorybulk.h"
#include "fsinventorybulk.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterreg.h"
#include "llfolderviewitem.h"
#include "llfolderviewmodelinventory.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "lllineeditor.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "lltexteditor.h"

#include <sstream>

namespace
{
constexpr S32 PAGE_SIZE = 20;
std::string display(std::string text)
{
    for (char& c : text) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    return text.size() > 1024 ? utf8str_truncate(text, 1024) + "…" : text;
}
LLUUID selectedID(LLFolderViewItem* item)
{
    auto* model = dynamic_cast<LLFolderViewModelItemInventory*>(item->getViewModelItem());
    return model ? model->getUUID() : LLUUID::null;
}
FSFloaterInventoryBulk* floater()
{
    return LLFloaterReg::getTypedInstance<FSFloaterInventoryBulk>("inventory_bulk_review");
}
}

FSFloaterInventoryBulk::FSFloaterInventoryBulk(const LLSD& key) : LLFloater(key) {}

bool FSFloaterInventoryBulk::postBuild()
{
    const auto bind = [this](const char* name, void (FSFloaterInventoryBulk::*method)())
    { getChild<LLUICtrl>(name)->setCommitCallback([this, method](LLUICtrl*, const LLSD&) { (this->*method)(); }); };
    bind("preview", &FSFloaterInventoryBulk::prepare);
    bind("execute", &FSFloaterInventoryBulk::execute);
    bind("history", &FSFloaterInventoryBulk::loadHistory);
    bind("review_undo", &FSFloaterInventoryBulk::reviewUndo);
    bind("use_folder", &FSFloaterInventoryBulk::chooseDestination);
    bind("clear_history", &FSFloaterInventoryBulk::clearHistory);
    getChild<LLUICtrl>("open_operation")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        if (mMode == Mode::History) loadOperation(getChild<LLScrollListCtrl>("rows")->getValue().asString());
    });
    getChild<LLUICtrl>("previous")->setCommitCallback([this](LLUICtrl*, const LLSD&) { changePage(-1); });
    getChild<LLUICtrl>("next")->setCommitCallback([this](LLUICtrl*, const LLSD&) { changePage(1); });
    getChild<LLUICtrl>("stop")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        if (!mOperationID.empty()) FSInventoryBulk::stop(mOperationID);
        childSetValue("heading", getString("stop_requested"));
        updateButtons();
    });
    getChild<LLUICtrl>("refresh")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        if (mMode == Mode::History) loadHistory();
        else if (mMode == Mode::Operation) loadOperation(mOperationID);
        else if (!mPlanID.empty()) loadPlan(mPlanID);
    });
    for (const char* name : {"kind", "name_pattern", "destination"})
        getChild<LLUICtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { invalidatePreview(); });
    for (const char* name : {"name_pattern", "destination"})
        getChild<LLLineEditor>(name)->setKeystrokeCallback([](LLLineEditor*, void* userdata)
        { static_cast<FSFloaterInventoryBulk*>(userdata)->invalidatePreview(); }, this);
    getChild<LLScrollListCtrl>("rows")->setCommitCallback([this](LLUICtrl*, const LLSD&) { describeSelection(); });
    getChild<LLScrollListCtrl>("rows")->setDoubleClickCallback([this]()
    {
        if (mMode == Mode::History) loadOperation(getChild<LLScrollListCtrl>("rows")->getValue().asString());
    });
    mSession = gAgent.getSessionID();
    updateButtons();
    return true;
}

void FSFloaterInventoryBulk::onOpen(const LLSD&)
{
    mPage = 0;
    loadHistory();
}

void FSFloaterInventoryBulk::showForSelection(LLInventoryPanel* panel)
{
    panel = panel ? panel : LLInventoryPanel::getActiveInventoryPanel(false);
    if (auto* ui = floater()) { ui->openFloater(); ui->loadSelection(panel); }
}
void FSFloaterInventoryBulk::showPlan(const std::string& id)
{
    if (auto* ui = floater()) { ui->openFloater(); ui->loadPlan(id); }
}
void FSFloaterInventoryBulk::showHistory()
{
    if (auto* ui = floater()) { ui->openFloater(); ui->mPage = 0; ui->loadHistory(); }
}

void FSFloaterInventoryBulk::loadSelection(LLInventoryPanel* panel)
{
    ++mGeneration;
    mBusy = false;
    mSelection = LLSD::emptyArray();
    mData = LLSD(); mPlanID.clear(); mOperationID.clear();
    mMode = Mode::Compose; mPage = 0; mSession = gAgent.getSessionID();
    panel = panel ? panel : LLInventoryPanel::getActiveInventoryPanel(false);
    if (panel)
        for (auto* item : panel->getSelectedItems())
        {
            const LLUUID id = selectedID(item);
            if (id.notNull()) mSelection.append(id);
        }
    if (mSelection.size() < 1 || mSelection.size() > 50)
    {
        mSelection = LLSD::emptyArray();
        reportError(getString("selection_limit"));
    }
    else
    {
        LLStringUtil::format_map_t args; args["COUNT"] = std::to_string(mSelection.size());
        childSetValue("heading", getString("frozen_selection", args));
    }
    updateRows();
}

void FSFloaterInventoryBulk::invalidatePreview()
{
    if (mUpdating || mBusy) return;
    ++mGeneration; mPlanID.clear(); mData = LLSD(); mPage = 0;
    mMode = Mode::Compose;
    childSetValue("heading", getString("preview_needed"));
    updateRows();
}

void FSFloaterInventoryBulk::chooseDestination()
{
    auto* panel = LLInventoryPanel::getActiveInventoryPanel(false);
    if (!panel || panel->getSelectedItems().size() != 1)
    { reportError(getString("select_folder")); return; }
    const auto id = selectedID(*panel->getSelectedItems().begin());
    if (!gInventory.getCategory(id)) { reportError(getString("select_folder")); return; }
    childSetValue("destination", id.asString());
    invalidatePreview();
}

void FSFloaterInventoryBulk::prepare()
{
    if (mBusy || mSelection.size() < 1 || mSelection.size() > 50) return;
    LLSD request;
    const bool rename = getChild<LLComboBox>("kind")->getValue().asString() == "rename";
    request["op"] = rename ? "previewBatchRename" : "previewBatchMove";
    if (rename)
    {
        const auto pattern = getChild<LLLineEditor>("name_pattern")->getText();
        request["items"] = LLSD::emptyArray();
        S32 index = 0;
        for (const auto& id : llsd::inArray(mSelection))
        {
            auto* object = gInventory.getObject(id.asUUID());
            std::string name = pattern;
            LLStringUtil::replaceString(name, "{n}", std::to_string(++index));
            LLStringUtil::replaceString(name, "{name}", object ? object->getName() : "");
            request["items"].append(llsd::map("id", id, "name", name));
        }
    }
    else
    {
        const auto text = getChild<LLLineEditor>("destination")->getText();
        if (!LLUUID::validate(text) || LLUUID(text).isNull()) { reportError(getString("invalid_folder")); return; }
        request["ids"] = mSelection;
        request["parent_id"] = LLUUID(text);
    }
    std::string error;
    auto result = FSInventoryBulk::preview(request, error);
    if (!error.empty()) { reportError(error); return; }
    loadPlan(result["plan_id"].asString());
}

void FSFloaterInventoryBulk::loadPlan(const std::string& id)
{
    std::string error;
    auto data = FSInventoryBulk::plan(id, error);
    if (!error.empty()) { reportError(error); return; }
    ++mGeneration; mBusy = false; mPlanID = id; mOperationID.clear();
    mSession = gAgent.getSessionID(); mMode = Mode::Preview; mData = data; mPage = 0;
    LLStringUtil::format_map_t args;
    args["COUNT"] = data["count"].asString(); args["ELIGIBLE"] = data["eligible_count"].asString();
    args["SKIPPED"] = data["skipped_count"].asString();
    childSetValue("heading", getString("preview_counts", args));
    updateRows();
}

void FSFloaterInventoryBulk::execute()
{
    if (mBusy || mMode != Mode::Preview || mPlanID.empty()) return;
    // Local consent is for this frozen plan only. The backend validates it and
    // reevaluates current policy; no saved permissions or confirm flags change.
    mBusy = true;
    const auto generation = ++mGeneration;
    const auto handle = getDerivedHandle<FSFloaterInventoryBulk>();
    const auto id = mPlanID;
    childSetValue("heading", getString("executing"));
    updateButtons();
    FSInventoryBulk::executeNative(id, [handle, generation](const LLSD& result)
    {
        auto* ui = handle.get();
        if (!ui || ui->mGeneration != generation || ui->mSession != gAgent.getSessionID()) return;
        ui->mBusy = false;
        if (result.has("error"))
        { ui->mPlanID.clear(); ui->reportError(result["error"].asString()); return; }
        ui->loadOperation(result["operation_id"].asString());
    });
    mRefresh.reset();
}

void FSFloaterInventoryBulk::loadHistory()
{
    ++mGeneration; mBusy = false; mMode = Mode::History;
    mPlanID.clear(); mOperationID.clear(); mSelection = LLSD::emptyArray();
    mSession = gAgent.getSessionID();
    std::string error;
    mData = FSInventoryBulk::history(llsd::map("offset", mPage * PAGE_SIZE, "limit", PAGE_SIZE), error);
    mHistoryTotal = mData["total"].asInteger();
    if (!error.empty()) { reportError(error); return; }
    if (mPage && mPage * PAGE_SIZE >= mHistoryTotal) { mPage = std::max(0, (mHistoryTotal - 1) / PAGE_SIZE); loadHistory(); return; }
    childSetValue("heading", getString("history_heading"));
    updateRows(); mRefresh.reset();
}

void FSFloaterInventoryBulk::loadOperation(const std::string& id)
{
    std::string error;
    auto result = FSInventoryBulk::history(llsd::map("operation_id", id), error);
    if (!error.empty())
    {
        mBusy = false; mOperationID.clear(); mPlanID.clear(); mData = LLSD();
        updateRows(); reportError(error); return;
    }
    mData = result["operation"]; mOperationID = id; mMode = Mode::Operation;
    LLStringUtil::format_map_t args;
    for (const auto& field : {"count", "submitted_count", "confirmed_count", "skipped_count"}) args[field] = mData[field].asString();
    childSetValue("heading", getString("operation_counts", args));
    updateRows(); mRefresh.reset();
}

void FSFloaterInventoryBulk::reviewUndo()
{
    if (mMode == Mode::History)
    {
        const auto id = getChild<LLScrollListCtrl>("rows")->getValue().asString();
        if (id.empty()) return;
        loadOperation(id);
        if (mMode != Mode::Operation) return;
    }
    if (mMode != Mode::Operation || mBusy) return;
    LLSD request = llsd::map("op", "previewBulkUndo", "operation_id", mOperationID);
    const auto id = getChild<LLScrollListCtrl>("rows")->getValue().asString();
    if (!id.empty()) { request["ids"] = LLSD::emptyArray(); request["ids"].append(LLUUID(id)); }
    std::string error;
    const auto result = FSInventoryBulk::preview(request, error);
    if (!error.empty()) { reportError(error); return; }
    mSelection = LLSD::emptyArray();
    loadPlan(result["plan_id"].asString());
}

void FSFloaterInventoryBulk::clearHistory()
{
    FSInventoryBulk::clear();
    ++mGeneration; mBusy = false; mPage = 0;
    loadHistory();
    childSetValue("heading", getString("history_cleared"));
}

void FSFloaterInventoryBulk::reportError(const std::string& error)
{
    childSetValue("heading", display(error));
    updateButtons();
}

void FSFloaterInventoryBulk::changePage(S32 delta)
{
    mPage += delta;
    if (mMode == Mode::History) loadHistory();
    else updateRows();
}

void FSFloaterInventoryBulk::updateRows()
{
    auto* list = getChild<LLScrollListCtrl>("rows");
    const std::string selected = list->getValue().asString();
    list->deleteAllItems();
    const bool history = mMode == Mode::History;
    list->setColumnLabel("before", getString(history ? "action_column" : "before_column"));
    list->setColumnLabel("after", getString(history ? "count_column" : "after_column"));
    const auto& rows = mData[history ? "operations" : "rows"];
    const S32 total = history ? mHistoryTotal : static_cast<S32>(rows.size());
    mPage = std::max<S32>(0, std::min<S32>(mPage, std::max<S32>(0, (total - 1) / PAGE_SIZE)));
    const S32 first = history ? 0 : mPage * PAGE_SIZE;
    const S32 end = std::min<S32>(static_cast<S32>(rows.size()), first + PAGE_SIZE);
    for (S32 i = first; i < end; ++i)
    {
        const auto& row = rows[i];
        LLSD item; item["value"] = row[history ? "operation_id" : "id"];
        std::string before = row["before_name"].asString();
        if (history)
        {
            LLStringUtil::format_map_t age;
            age["MINUTES"] = std::to_string(static_cast<S32>(std::max(0.0,
                static_cast<double>(totalTime()) / 1000000.0 - row["started"].asReal()) / 60.0));
            before = getString("kind_" + row["kind"].asString()) + " · " + getString("history_age", age);
        }
        const std::string after = history ? row["count"].asString() :
            (mData["kind"].asString() == "move" ? row["destination"].asString() : row["after_name"].asString());
        std::string status;
        if (row.has("status")) status = getString("status_" + row["status"].asString());
        else status = getString(row["eligible"].asBoolean() ? "eligible" : "skipped");
        bool undo = history ? false : row[row.has("status") ? "undo_available" : "undo_eligible"].asBoolean();
        if (history) for (const auto& detail : llsd::inArray(row["rows"])) undo |= detail["undo_available"].asBoolean();
        const std::string values[] = {display(before), display(after), status, getString(undo ? (mMode == Mode::Preview ? "undo_if_confirmed" : "undo_available") : "undo_unavailable")};
        const char* columns[] = {"before", "after", "status", "undo"};
        for (S32 c = 0; c < 4; ++c)
        { item["columns"][c]["column"] = columns[c]; item["columns"][c]["value"] = values[c]; }
        list->addElement(item);
    }
    if (!selected.empty()) list->selectByValue(selected);
    LLStringUtil::format_map_t args;
    args["FIRST"] = std::to_string(total ? mPage * PAGE_SIZE + 1 : 0);
    args["LAST"] = std::to_string(std::min(total, (mPage + 1) * PAGE_SIZE)); args["TOTAL"] = std::to_string(total);
    childSetValue("page", getString("page_count", args));
    describeSelection(); updateButtons();
}

void FSFloaterInventoryBulk::describeSelection()
{
    auto* list = getChild<LLScrollListCtrl>("rows");
    const std::string id = list->getValue().asString();
    std::ostringstream text;
    for (const auto& row : llsd::inArray(mData[mMode == Mode::History ? "operations" : "rows"]))
    {
        if (row[mMode == Mode::History ? "operation_id" : "id"].asString() != id) continue;
        text << getString(mMode == Mode::History ? "operation_id" : "object_id") << id << "\n";
        if (mMode == Mode::History) { text << getString("open_history_hint"); break; }
        text << getString("source") << display(row["path"].asString()) << "\n";
        if (mData["kind"].asString() == "move") text << getString("destination_label") << display(row["destination"].asString()) << "\n";
        if (!row["skip_reason"].asString().empty()) text << getString("skip_label") << display(row["skip_reason"].asString()) << "\n";
        if (!row["error"].asString().empty()) text << getString("result_label") << display(row["error"].asString()) << "\n";
        break;
    }
    if (id.empty()) text << getString(mMode == Mode::History ? "open_history_hint" : "row_hint");
    text << "\n" << getString("partial_notice");
    if (mData["kind"].asString() == "move") text << "\n" << getString("unique_move_notice");
    getChild<LLTextEditor>("details")->setText(text.str());
    updateButtons();
}

void FSFloaterInventoryBulk::updateButtons()
{
    const bool selection = mSelection.size() > 0 && !mBusy;
    const bool rename = getChild<LLComboBox>("kind")->getValue().asString() == "rename";
    getChild<LLUICtrl>("kind")->setEnabled(selection);
    getChild<LLUICtrl>("name_pattern")->setEnabled(selection && rename);
    getChild<LLUICtrl>("destination")->setEnabled(selection && !rename);
    getChild<LLUICtrl>("use_folder")->setEnabled(selection && !rename);
    getChild<LLUICtrl>("preview")->setEnabled(selection);
    getChild<LLButton>("execute")->setEnabled(!mBusy && mMode == Mode::Preview && !mPlanID.empty() &&
        mData["execution_allowed"].asBoolean() && mData["eligible_count"].asInteger() > 0);
    getChild<LLButton>("execute")->setLabel(getString(mData["current_permission"].asString() == "ask" ? "allow_execute" : "execute_label"));
    childSetValue("permission", mMode == Mode::Preview ? getString(mData["execution_allowed"].asBoolean() ? "one_request" : "permission_blocked") : getString("history_scope"));
    bool undo = false;
    if (mMode == Mode::Operation)
    {
        const auto id = getChild<LLScrollListCtrl>("rows")->getValue().asString();
        for (const auto& row : llsd::inArray(mData["rows"]))
            if (id.empty() || row["id"].asString() == id) undo |= row["undo_available"].asBoolean();
    }
    getChild<LLUICtrl>("review_undo")->setEnabled(!mBusy && undo);
    getChild<LLUICtrl>("open_operation")->setEnabled(mMode == Mode::History &&
        !getChild<LLScrollListCtrl>("rows")->getValue().asString().empty());
    getChild<LLUICtrl>("stop")->setEnabled(!mOperationID.empty() && (mBusy || mData["status"].asString() == "executing"));
    const S32 total = mMode == Mode::History ? mHistoryTotal : static_cast<S32>(mData["rows"].size());
    getChild<LLUICtrl>("previous")->setEnabled(mPage > 0);
    getChild<LLUICtrl>("next")->setEnabled((mPage + 1) * PAGE_SIZE < total);
}

void FSFloaterInventoryBulk::draw()
{
    if (mSession != gAgent.getSessionID() || (gDisconnected && (!mData.isUndefined() || mSelection.size())))
    {
        ++mGeneration; mBusy = false; mSelection = LLSD::emptyArray();
        mData = LLSD(); mPlanID.clear(); mOperationID.clear(); mHistoryTotal = 0;
        mSession = gAgent.getSessionID(); mMode = Mode::Compose;
        mUpdating = true; childSetValue("name_pattern", "{name}"); childSetValue("destination", ""); mUpdating = false;
        childSetValue("heading", getString("session_changed")); updateRows();
    }
    if (!gDisconnected && getVisible() && mRefresh.getElapsedTimeF32() > 1.f)
    {
        mRefresh.reset();
        if (mBusy && !mPlanID.empty() && mOperationID.empty())
        {
            std::string error;
            const auto current = FSInventoryBulk::plan(mPlanID, error);
            mOperationID = current["operation_id"].asString();
        }
        if (mMode == Mode::Operation || (mBusy && !mOperationID.empty())) loadOperation(mOperationID);
        else if (mMode == Mode::Preview && !mBusy)
        {
            std::string error;
            const auto current = FSInventoryBulk::plan(mPlanID, error);
            if (!error.empty()) { mPlanID.clear(); reportError(error); }
            else { mData = current; updateButtons(); }
        }
    }
    LLFloater::draw();
}

void FSFloaterInventoryBulk::onClose(bool)
{
    // Closing never cancels submitted server work. Late callbacks cannot reopen
    // this floater or expose a prior session's labels.
    ++mGeneration; mBusy = false; mSelection = LLSD::emptyArray(); mData = LLSD();
    mPlanID.clear(); mOperationID.clear(); mHistoryTotal = 0; mPage = 0;
    mUpdating = true; childSetValue("name_pattern", "{name}"); childSetValue("destination", ""); mUpdating = false;
    updateRows();
}
