/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#ifndef FS_FLOATER_GESTURE_BOARD_H
#define FS_FLOATER_GESTURE_BOARD_H
#include "llfloater.h"
#include "lltimer.h"
#include "lluuid.h"
#include "fsgestureboardmodel.h"
#include <vector>
class LLContextMenu;
class LLButton;
class LLTextBox;
class FSGestureBoardTile;
class FSFloaterGestureBoard final : public LLFloater
{
public:
    explicit FSFloaterGestureBoard(const LLSD& key) : LLFloater(key) {}
    ~FSFloaterGestureBoard() override;
    bool postBuild() override;
    void onOpen(const LLSD&) override;
    void onClose(bool) override;
    void draw() override;
    void reshape(S32 width, S32 height, bool from_parent = true) override;
    bool handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip) override;
    void tileMenu(const std::string& tile, LLView* view, S32 x, S32 y);
    void dragTile(const std::string& tile, S32 screen_x, S32 screen_y, bool finish);
    void playTile(const std::string& tile);
    void focusGesture(const LLUUID& item) { mFocusReference = item; }
    bool current() const;
private:
    void refresh(bool force = false);
    void rebuild();
    void editBoard(const std::string& mode);
    void addGestures();
    void tileAction(const std::string& action, const std::string& board, const std::string& tile, const LLSD& expected);
    void deleteBoard();
    void closeEditors();
    LLTimer mRefreshTimer;
    LLUUID mAccount, mSession, mFocusReference;
    LLSD mDisplayed;
    S32 mGridWidth = 0;
    bool mBuilt = false;
    std::vector<FSGestureBoardTile*> mTiles;
    LLHandle<LLContextMenu> mMenu;
};
class FSFloaterGesturePicker final : public LLFloater
{
public:
    explicit FSFloaterGesturePicker(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool) override;
    void draw() override;
private:
    void acceptSelection();
    LLSD mKey, mExpected;
    LLUUID mAccount, mSession;
};
class FSFloaterGestureTileEditor final : public LLFloater
{
public:
    explicit FSFloaterGestureTileEditor(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool) override;
    void draw() override;
    bool replaceGesture(const LLUUID& item, const LLSD& key);
private:
    void save();
    void replace();
    void updatePreview();
    LLSD mKey, mExpected;
    LLUUID mAccount, mSession, mToken;
    FSGestureBoard::Tile mTile;
};
#endif
