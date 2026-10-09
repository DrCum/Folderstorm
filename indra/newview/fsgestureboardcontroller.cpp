/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "fsgestureboardcontroller.h"
#include "llagent.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "llinventorymodel.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llsdutil.h"
#include "rlvactions.h"
#include <algorithm>
FSGestureBoardController& FSGestureBoardController::instance()
{ static FSGestureBoardController controller; return controller; }
FSGestureBoardController::FSGestureBoardController()
{
    LLGestureMgr::instance().addObserver(this); gInventory.addObserver(this); gIdleCallbacks.addFunction(idle, this);
}
FSGestureBoardController::~FSGestureBoardController()
{
    gIdleCallbacks.deleteFunction(idle, this); gInventory.removeObserver(this);
    if (LLGestureMgr::instanceExists()) LLGestureMgr::instance().removeObserver(this);
}
bool FSGestureBoardController::available() const
{
    return !LLApp::isQuitting() && LLStartUp::getStartupState() == STATE_STARTED && gAgent.getID().notNull() && gAgent.getSessionID().notNull() &&
        gSavedPerAccountSettings.controlExists("FSGestureBoards");
}
bool FSGestureBoardController::permitted() const
{ return available() && gSavedPerAccountSettings.getBOOL("FSGesturesEnabled") && RlvActions::canPlayGestures(); }
void FSGestureBoardController::setStatus(const std::string& status)
{ if (mStatus != status) { mStatus = status; ++mRevision; } }
void FSGestureBoardController::cancelPending()
{ ++mGeneration; if (!mPending.empty()) { mPending.clear(); ++mRevision; setStatus("cancelled"); } }
void FSGestureBoardController::refresh()
{
    if (!available() || mAccount != gAgent.getID() || mSession != gAgent.getSessionID())
    {
        cancelPending(); mStarted.clear(); mData = {}; mRaw = LLSD(); mValid = false;
        mAccount = gAgent.getID(); mSession = gAgent.getSessionID(); ++mRevision;
        setStatus(available() ? "ready" : "logged_out");
    }
    if (!available()) return;
    const auto raw = gSavedPerAccountSettings.getLLSD("FSGestureBoards");
    if (!mValid || raw != mRaw)
    {
        cancelPending(); mRaw = raw; mValid = FSGestureBoard::decode(raw, mData); ++mRevision;
        if (!mValid) { mData = {}; setStatus("invalid_data"); }
    }
}
bool FSGestureBoardController::commit(const FSGestureBoard::Collection& data, const LLSD& expected, const LLUUID& account, const LLUUID& session)
{
    refresh();
    if (!valid() || account != mAccount || session != mSession) { setStatus(available() ? "invalid_data" : "logged_out"); return false; }
    if (expected != mRaw) { setStatus("changed"); return false; }
    FSGestureBoard::Collection checked;
    const LLSD encoded = FSGestureBoard::encode(data);
    if (!FSGestureBoard::decode(encoded, checked)) { setStatus("invalid_name"); return false; }
    cancelPending(); gSavedPerAccountSettings.setLLSD("FSGestureBoards", encoded);
    mRaw = encoded; mData = checked; ++mRevision; setStatus("saved"); return true;
}
bool FSGestureBoardController::selectBoard(const std::string& id)
{
    refresh(); if (!valid() || !FSGestureBoard::findBoard(mData, id)) return false;
    auto data = mData; data.selected = id; return commit(data, mRaw, mAccount, mSession);
}
bool FSGestureBoardController::resolve(const LLUUID& reference, LLUUID& item, LLUUID& asset, std::string& name, std::string& reason) const
{
    if (!gInventory.isInventoryUsable()) { reason = "inventory_not_ready"; return false; }
    const auto* original = gInventory.getItem(reference);
    item = gInventory.getLinkedItemID(reference);
    const auto* resolved = gInventory.getItem(item);
    if (!original || !resolved || resolved->getIsLinkType() || resolved->getType() != LLAssetType::AT_GESTURE ||
        resolved->getAssetUUID().isNull()) { reason = "missing"; return false; }
    const auto trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (trash.notNull() && (gInventory.isObjectDescendentOf(reference, trash) || gInventory.isObjectDescendentOf(item, trash)))
    { reason = "missing"; return false; }
    asset = resolved->getAssetUUID(); name = original->getName(); return true;
}
bool FSGestureBoardController::addItems(const std::string& id, const std::vector<LLUUID>& items, const LLSD& expected, const LLUUID& account, const LLUUID& session)
{
    refresh(); auto data = mData; auto* board = FSGestureBoard::findBoard(data, id);
    if (!valid() || !board || items.empty() || items.size() > static_cast<size_t>(FSGestureBoard::MAX_TILES)) return false;
    for (const auto& reference : items)
    {
        LLUUID item, asset; std::string name, error;
        if (!resolve(reference, item, asset, name, error)) { setStatus(error); return false; }
        bool exists = false;
        for (const auto& tile : board->tiles) if (gInventory.getLinkedItemID(LLUUID(tile.item)) == item) { exists = true; break; }
        if (exists) continue;
        if (board->tiles.size() >= static_cast<size_t>(FSGestureBoard::MAX_TILES)) { setStatus("full"); return false; }
        FSGestureBoard::Tile tile; tile.id = LLUUID::generateNewID().asString(); tile.item = reference.asString(); board->tiles.push_back(tile);
    }
    return commit(data, expected, account, session);
}
std::string FSGestureBoardController::tileState(const FSGestureBoard::Tile& tile) const
{
    LLUUID item, asset; std::string name, error;
    if (!resolve(LLUUID(tile.item), item, asset, name, error)) return error;
    if (!permitted()) return "blocked";
    if (mPending.count(item)) return "loading";
    return LLGestureMgr::instance().isGesturePlaying(item) ? "playing" : "ready";
}
bool FSGestureBoardController::play(const std::string& id, const std::string& tile_id)
{
    refresh(); const auto* board = FSGestureBoard::findBoard(mData, id);
    const auto* tile = board ? FSGestureBoard::findTile(*board, tile_id) : nullptr;
    if (!valid() || !tile || mData.selected != id) return false;
    if (!permitted()) { setStatus("blocked"); return false; }
    LLUUID item, asset; std::string name, error;
    if (!resolve(LLUUID(tile->item), item, asset, name, error)) { setStatus(error); return false; }
    auto& manager = LLGestureMgr::instance();
    if (mPending.count(item)) { setStatus("loading"); return true; }
    if (manager.isGesturePlaying(item)) { manager.stopGesture(item); mStarted.erase(item); setStatus("stopped"); return true; }
    const auto loaded = manager.getActiveGestures().find(item);
    if (loaded != manager.getActiveGestures().end() && loaded->second)
    { manager.playGesture(item); if (manager.isGesturePlaying(item)) mStarted.insert(item); setStatus("playing"); return true; }
    Pending pending; pending.board = id; pending.tile = tile_id; pending.reference = LLUUID(tile->item);
    pending.item = item; pending.asset = asset; pending.account = mAccount; pending.session = mSession; pending.generation = mGeneration;
    pending.timer.reset(); mPending[item] = pending; setStatus("loading");
    if (!manager.isGestureActive(item)) manager.activateGestureWithAsset(item, asset, true, false);
    return true;
}
void FSGestureBoardController::stopBoard()
{
    refresh(); cancelPending();
    if (!available()) return;
    const auto started = mStarted; mStarted.clear();
    for (const auto& item : started) if (LLGestureMgr::instance().isGesturePlaying(item)) LLGestureMgr::instance().stopGesture(item);
    setStatus("stopped");
}
void FSGestureBoardController::idle(void* data) { static_cast<FSGestureBoardController*>(data)->poll(); }
void FSGestureBoardController::poll()
{
    if (!available() || mAccount != gAgent.getID() || mSession != gAgent.getSessionID())
    { if (!mPending.empty() || !mStarted.empty()) { cancelPending(); mStarted.clear(); } return; }
    auto& manager = LLGestureMgr::instance();
    for (auto it = mStarted.begin(); it != mStarted.end();)
        if (!manager.isGesturePlaying(*it)) it = mStarted.erase(it); else ++it;
    if (mPending.empty() || mPollTimer.getElapsedTimeF32() < .1f) return;
    mPollTimer.reset(); refresh();
    for (auto it = mPending.begin(); it != mPending.end();)
    {
        const Pending pending = it->second;
        const auto* board = FSGestureBoard::findBoard(mData, pending.board);
        const auto* tile = board ? FSGestureBoard::findTile(*board, pending.tile) : nullptr;
        LLUUID item, asset; std::string name, error;
        const bool target = tile && tile->item == pending.reference.asString() && mData.selected == pending.board &&
            resolve(pending.reference, item, asset, name, error) && item == pending.item && asset == pending.asset;
        const float elapsed = pending.timer.getElapsedTimeF32();
        if (!FSGestureBoard::pendingMatches(pending.account.asString(), pending.session.asString(), mAccount.asString(), mSession.asString(),
                pending.generation, mGeneration, target, permitted(), elapsed))
        { it = mPending.erase(it); setStatus(elapsed >= 30.f ? "timeout" : !permitted() ? "blocked" : "changed"); continue; }
        const auto loaded = manager.getActiveGestures().find(item);
        if (loaded == manager.getActiveGestures().end()) { it = mPending.erase(it); setStatus("load_failed"); continue; }
        if (!loaded->second) { ++it; continue; }
        // Erase before entering the native engine: it notifies observers.
        it = mPending.erase(it);
        if (!manager.isGesturePlaying(item)) { manager.playGesture(item); if (manager.isGesturePlaying(item)) mStarted.insert(item); }
        setStatus("playing");
    }
}
