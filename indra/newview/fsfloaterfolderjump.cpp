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
#include "fsfloaterfolderjump.h"

#include "llagent.h"
#include "llbutton.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llinventoryfilter.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llpanelmaininventory.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lltimer.h"
#include "llviewerinventory.h"
#include "llviewerregion.h"
#include "rlvactions.h"

namespace
{
constexpr size_t MAX_RESULTS = 60;

// Prefer the leaf name. Parent names disambiguate duplicate leaves and can also be searched.
int matchRank(const std::string& name, const std::string& path, const std::string& query,
              const std::vector<std::string>& words)
{
    if (query == name) return 0;
    if (name.compare(0, query.size(), query) == 0) return 1;
    bool all_in_name = true;
    for (const auto& word : words)
    {
        if (name.find(word) != std::string::npos) continue;
        if (path.find(word) == std::string::npos) return -1;
        all_in_name = false;
    }
    return all_in_name ? 2 : 3;
}
}

FSFloaterFolderJump::~FSFloaterFolderJump()
{
    if (gInventory.containsObserver(this)) gInventory.removeObserver(this);
}

void FSFloaterFolderJump::showFrom(LLPanelMainInventory& inventory, LLView* requester, FolderChoice choose)
{
    if (!gInventory.isInventoryUsable() || !gAgent.getRegion() || RlvActions::hasBehaviour(RLV_BHVR_SHOWINV)) return;
    auto* floater = LLFloaterReg::getTypedInstance<FSFloaterFolderJump>("inventory_folder_jump");
    if (!floater) return;
    floater->mRequester = requester ? requester->getHandle() : LLHandle<LLView>();
    floater->mChoose = std::move(choose);
    floater->mOwner = inventory.getHandle();
    floater->mSession = gAgent.getSessionID();
    floater->openFloater();
}

LLPanelMainInventory* FSFloaterFolderJump::owner() const
{
    auto* panel = dynamic_cast<LLPanelMainInventory*>(mOwner.get());
    if (mChoose && (!mRequester.get() || !mRequester.get()->isInVisibleChain())) return nullptr;
    if (!panel || !panel->isInVisibleChain() || mSession.isNull() || mSession != gAgent.getSessionID()
        || gDisconnected || !gAgent.getRegion() || !gInventory.isInventoryUsable()
        || RlvActions::hasBehaviour(RLV_BHVR_SHOWINV)) return nullptr;
    return panel;
}

bool FSFloaterFolderJump::postBuild()
{
    mQuery = getChild<LLFilterEditor>("query");
    mResults = getChild<LLScrollListCtrl>("folders");
    mStatus = getChild<LLTextBox>("status");
    mOpen = getChild<LLButton>("open_folder");
    mQuery->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        const auto query = mQuery->getText();
        if (query == mRequestedQuery) return;
        mRequestedQuery = query;
        mChooseFirst = true;
        mRefreshNeeded = true;
        mOpen->setEnabled(false);
    });
    mResults->setDoubleClickCallback([this]() { openSelected(); });
    mResults->setCommitOnSelectionChange(true);
    mResults->setCommitOnKeyboardMovement(true);
    setDefaultBtn(mOpen);
    mResults->setCommitCallback([this](LLUICtrl*, const LLSD&) {
        mOpen->setEnabled(!mIndexDirty && !mRefreshNeeded && mResults->getSelectedValue().asUUID().notNull());
    });
    mOpen->setCommitCallback([this](LLUICtrl*, const LLSD&) { openSelected(); });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&) { closeFloater(); });
    return LLFloater::postBuild();
}

void FSFloaterFolderJump::onOpen(const LLSD&)
{
    if (!owner()) { closeFloater(); return; }
    if (!gInventory.containsObserver(this)) gInventory.addObserver(this);
    setTitle(getString(mChoose ? "choose_title" : "navigate_title"));
    mOpen->setLabel(getString(mChoose ? "choose_action" : "navigate_action"));
    mOpen->setToolTip(getString(mChoose ? "choose_tip" : "navigate_tip"));
    mRequestedQuery.clear();
    mResults->deleteAllItems();
    mQuery->setText(LLStringExplicit(""));
    mQuery->setFocus(true);
    mChooseFirst = true;
    beginIndex();
}

void FSFloaterFolderJump::clearIndex()
{
    mFolders.clear();
    mSeen.clear();
    mNextFolder = 0;
    mNextChild = 0;
    mOpen->setEnabled(false);
    mResults->setEnabled(false);
}

void FSFloaterFolderJump::onClose(bool)
{
    if (gInventory.containsObserver(this)) gInventory.removeObserver(this);
    clearIndex();
    mResults->deleteAllItems();
    mChoose = {};
    mRequester.markDead();
    mOwner.markDead();
    mSession.setNull();
}

void FSFloaterFolderJump::changed(U32 mask)
{
    if (!(mask & (LABEL | STRUCTURE | ADD | REMOVE))) return;
    for (const auto& id : gInventory.getChangedIDs())
    {
        if (mSeen.count(id) || gInventory.getCategory(id))
        {
            mIndexDirty = true;
            break;
        }
    }
}

void FSFloaterFolderJump::beginIndex()
{
    clearIndex();
    const LLUUID root = gInventory.getRootFolderID();
    mSeen.insert(root);
    mFolders.push_back({root, {}, {}, {}, {}, false});
    mIndexDirty = false;
    mRefreshNeeded = true;
    mGeneration = owner()->getAllItemsPanel()->getFilter().getCurrentGeneration();
    mStatus->setText(getString("looking"));
}

void FSFloaterFolderJump::extendIndex(const LLInventoryFilter& filter)
{
    LLTimer slice;
    while (mNextFolder < mFolders.size() && slice.getElapsedTimeF64() < 0.002)
    {
        const auto& parent = mFolders[mNextFolder];
        std::string path = parent.parent_path;
        if (!parent.name.empty())
        {
            if (!path.empty()) path += " / ";
            path += parent.name;
        }
        LLInventoryModel::cat_array_t* children = nullptr;
        LLInventoryModel::item_array_t* items = nullptr;
        gInventory.getDirectDescendentsOf(parent.id, children, items);
        while (children && mNextChild < children->size())
        {
            const auto& child = (*children)[mNextChild++];
            if (child && child->getPreferredType() != LLFolderType::FT_TRASH)
            {
                const auto id = child->getUUID();
                if (mSeen.insert(id).second && filter.checkFolder(id, false))
                {
                    const auto& name = child->getName();
                    mFolders.push_back({id, name, path, utf8str_tolower(name), utf8str_tolower(path), mNextFolder == 0});
                }
            }
            // A single folder can have thousands of children. Resume by index next frame.
            if (mNextChild % 32 == 0 && slice.getElapsedTimeF64() >= 0.002) return;
        }
        mNextChild = 0;
        ++mNextFolder;
    }
}

void FSFloaterFolderJump::draw()
{
    auto* inventory = owner();
    if (!inventory) { closeFloater(); return; }
    const auto& filter = inventory->getAllItemsPanel()->getFilter();
    if (mIndexDirty || mGeneration != filter.getCurrentGeneration()) beginIndex();
    if (mNextFolder < mFolders.size())
    {
        extendIndex(filter);
        mRefreshNeeded = true;
    }
    if (mRefreshNeeded && mNextFolder == mFolders.size())
    {
        refreshResults(mChooseFirst);
        mChooseFirst = false;
    }
    LLFloater::draw();
}

void FSFloaterFolderJump::refreshResults(bool select_first)
{
    const LLUUID previous = mResults->getSelectedValue().asUUID();
    mResults->deleteAllItems();
    std::string query = utf8str_tolower(mQuery->getText());
    LLStringUtil::trim(query);
    const auto words = LLStringUtil::getTokens(query, " /\t");
    using Match = std::pair<int, const Folder*>;
    std::vector<Match> matches;
    for (size_t i = 1; i < mFolders.size(); ++i)
    {
        const auto& folder = mFolders[i];
        if (words.empty() && !folder.top_level) continue;
        const int rank = words.empty() ? 0 : matchRank(folder.folded_name, folder.folded_path, query, words);
        if (rank >= 0) matches.emplace_back(rank, &folder);
    }
    const size_t count = std::min(MAX_RESULTS, matches.size());
    std::partial_sort(matches.begin(), matches.begin() + count, matches.end(), [](const Match& a, const Match& b) {
        return std::tie(a.first, a.second->folded_name, a.second->folded_path, a.second->id)
             < std::tie(b.first, b.second->folded_name, b.second->folded_path, b.second->id);
    });
    for (size_t i = 0; i < count; ++i)
    {
        const auto& folder = *matches[i].second;
        LLSD row;
        row["id"] = folder.id;
        row["columns"][0]["column"] = "name";
        row["columns"][0]["value"] = folder.name;
        row["columns"][1]["column"] = "parent";
        row["columns"][1]["value"] = folder.parent_path.empty() ? getString("inventory_root") : folder.parent_path;
        mResults->addElement(row);
    }
    mResults->setEnabled(true);
    if (select_first) mResults->selectFirstItem();
    else if (previous.notNull()) mResults->selectByID(previous);
    mRefreshNeeded = false;
    mOpen->setEnabled(mResults->getSelectedValue().asUUID().notNull());
    LLStringUtil::format_map_t args;
    args["[COUNT]"] = llformat("%d", static_cast<int>(matches.size()));
    args["[LIMIT]"] = llformat("%d", static_cast<int>(MAX_RESULTS));
    std::string status = words.empty() ? "at_root" : "matches";
    if (!count) status = "no_results";
    else if (matches.size() > count) status = "limited";
    mStatus->setText(getString(status, args));
}

bool FSFloaterFolderJump::isReachable(const LLUUID& id, const LLInventoryFilter& filter) const
{
    std::set<LLUUID> visited;
    const LLUUID root = gInventory.getRootFolderID();
    auto folder = gInventory.getCategory(id);
    while (folder && folder->getUUID() != root)
    {
        if (folder->getPreferredType() == LLFolderType::FT_TRASH || !visited.insert(folder->getUUID()).second
            || !filter.checkFolder(folder->getUUID(), false)) return false;
        folder = gInventory.getCategory(folder->getParentUUID());
    }
    return folder && id != root;
}

void FSFloaterFolderJump::openSelected()
{
    auto* inventory = owner();
    if (!inventory || mIndexDirty || mRefreshNeeded) return;
    const LLUUID id = mResults->getSelectedValue().asUUID();
    if (id.isNull()) return;
    if (!isReachable(id, inventory->getAllItemsPanel()->getFilter()))
    {
        mIndexDirty = true;
        return;
    }
    auto choose = std::move(mChoose);
    closeFloater();
    if (choose) { choose(id); return; }
    if (inventory->isSingleFolderMode()) inventory->setSingleFolderViewRoot(id, false);
    else LLPanelMainInventory::newFolderWindow(id);
}

bool FSFloaterFolderJump::handleKeyHere(KEY key, MASK mask)
{
    if (mask == MASK_NONE && mQuery->hasFocus() && (key == KEY_DOWN || key == KEY_UP))
    {
        mResults->setFocus(true);
        if (key == KEY_DOWN && mResults->getFirstSelectedIndex() < 0) mResults->selectFirstItem();
        return true;
    }
    if (key == KEY_ESCAPE && mask == MASK_NONE) { closeFloater(); return true; }
    if (key == KEY_RETURN && mask == MASK_NONE && (mQuery->hasFocus() || mResults->hasFocus()))
    {
        if (mOpen->getEnabled()) openSelected();
        return true;
    }
    return LLFloater::handleKeyHere(key, mask);
}
