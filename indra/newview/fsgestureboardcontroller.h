/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#ifndef FS_GESTURE_BOARD_CONTROLLER_H
#define FS_GESTURE_BOARD_CONTROLLER_H
#include "fsgestureboardmodel.h"
#include "llassettype.h"
#include "llgesturemgr.h"
#include "llinventoryobserver.h"
#include "llsd.h"
#include "lltimer.h"
#include "lluuid.h"
#include <map>
class FSGestureBoardController final : public LLGestureManagerObserver, public LLInventoryObserver
{
public:
    static FSGestureBoardController& instance();
    ~FSGestureBoardController() override;
    bool available() const;
    void refresh();
    bool valid() const { return mValid && available(); }
    const FSGestureBoard::Collection& data() const { return mData; }
    const LLSD& snapshot() const { return mRaw; }
    unsigned long revision() const { return mRevision; }
    const std::string& status() const { return mStatus; }
    void setStatus(const std::string& status);
    bool commit(const FSGestureBoard::Collection& data, const LLSD& expected, const LLUUID& account, const LLUUID& session);
    bool matchesSnapshot(const LLSD& expected) const;
    bool selectBoard(const std::string& id);
    bool addItems(const std::string& board, const std::vector<LLUUID>& items, const LLSD& expected, const LLUUID& account, const LLUUID& session);
    bool resolve(const LLUUID& reference, LLUUID& item, LLUUID& asset, std::string& name, std::string& reason,
                 LLAssetType::EType* type = nullptr) const;
    std::string tileState(const FSGestureBoard::Tile& tile) const;
    bool play(const std::string& board, const std::string& tile, const LLUUID& owner = LLUUID::null);
    void cancelPending();
    void cancelPendingForWindow(const LLUUID& owner);
    void cancelPendingForBoard(const std::string& board);
    void stopBoard(const std::string& board);
    void changed() override { ++mRevision; }
    void changed(U32) override { ++mRevision; }
private:
    FSGestureBoardController();
    bool permitted(LLAssetType::EType type = LLAssetType::AT_GESTURE) const;
    static void idle(void*);
    void poll();
    struct Pending
    {
        std::string board, tile;
        LLUUID reference, item, asset, account, session, owner;
        unsigned long generation = 0;
        LLTimer timer;
    };
    FSGestureBoard::Collection mData;
    LLSD mRaw;
    LLUUID mAccount, mSession;
    bool mValid = false;
    unsigned long mRevision = 0, mGeneration = 0;
    std::string mStatus = "ready";
    std::map<LLUUID, Pending> mPending;
    // Native gestures share one playback instance per item. Retain only those
    // a board started, retaining its board ID. Forget completions before a later
    // external play; Stop in one board must not stop another board's gestures.
    std::map<LLUUID, std::string> mStarted;
    LLTimer mPollTimer;
};
#endif
