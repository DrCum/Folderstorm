/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#include "llviewerprecompiledheaders.h"
#include "fsfloatergestureboard.h"
#include "fsgestureboardcontroller.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcolorswatch.h"
#include "llcombobox.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llfolderview.h"
#include "llfolderviewmodelinventory.h"
#include "llfocusmgr.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "llspinctrl.h"
#include "llsdutil.h"
#include "lltextbox.h"
#include "lltooldraganddrop.h"
#include "lluiimage.h"
#include "lluicolortable.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "llviewertexture.h"
#include "lluictrlfactory.h"
#include <algorithm>
#include <cmath>
namespace
{
using namespace FSGestureBoard;
LLColor4 color(const Tile& tile) { return LLColor4(tile.color[0], tile.color[1], tile.color[2], 1.f); }
LLColor4 foregroundColor(const Tile& tile) { const float c = foreground(tile.color); return LLColor4(c, c, c, 1.f); }
Tile editorPresentation(const std::string& mode, const Collection& collection, const Board* board, const Tile& tile)
{
    Collection preview; preview.defaults = collection.defaults;
    Board context; context.compact = !board || board->compact;
    if (board && mode != "global_defaults") context.defaults = board->defaults;
    Tile leaf = tile;
    if (mode == "global_defaults") { preview.defaults = tile; leaf = {}; }
    if (mode == "defaults") { context.defaults = tile; leaf = {}; }
    return presentation(preview, context, leaf);
}
class FSGestureBoardButton : public LLButton
{
public:
    FSGestureBoardButton(const Params& p, const Tile& tile)
        : LLButton(p), mCustomColor(tile.custom_color), mOutline(foregroundColor(tile))
    {
        initFromParams(p);
        if (!mCustomColor) return; // Theme tiles retain every native skin state.
        // Multiplying by a colored/dark skin texture cannot reproduce picked
        // RGB. Use the viewer's neutral white texture for every custom state.
        LLUIImagePtr fill = new LLUIImage("gesture_board_fill", LLViewerFetchedTexture::sWhiteImagep.get());
        for (auto* image : {&mImageUnselected, &mImageSelected, &mImageHoverSelected, &mImageHoverUnselected,
                           &mImageDisabled, &mImageDisabledSelected, &mImagePressed, &mImagePressedSelected, &mImageFlash})
            *image = fill;
        mImageOverlay = nullptr;
        setScaleImage(true);
        setImageColor(color(tile)); setDisabledImageColor(color(tile));
        setUnselectedLabelColor(mOutline); setSelectedLabelColor(mOutline);
        setDisabledLabelColor(mOutline); setDisabledSelectedLabelColor(mOutline);
        setUseFontColor(false);
        mHoverGlowStrength = 0.f;
        mDropShadowedText = false;
    }
    void setSound(bool sound)
    {
        if (sound == mSound) return;
        mSound = sound;
        const LLColor4 tint = mCustomColor ? mOutline : mUnselectedLabelColor.get();
        setImageOverlay(std::string(sound ? "Inv_Sound" : ""), LLFontGL::LEFT, tint);
        mImageOverlaySelectedColor = mCustomColor ? mOutline : mSelectedLabelColor.get();
        mImageOverlayDisabledColor = mCustomColor ? mOutline : mDisabledLabelColor.get();
    }
    void mark(bool marked) { mMarked = marked; }
    void draw() override
    {
        LLButton::draw();
        if (mCustomColor && (getToggleState() || mNeedsHighlight || hasMouseCapture()))
        {
            const F32 alpha = mUseDrawContextAlpha ? getDrawContext().mAlpha : getCurrentTransparency();
            // Outline feedback preserves the fill instead of tinting it.
            gl_rect_2d(1, getRect().getHeight() - 1, getRect().getWidth() - 1, 1, mOutline % alpha, false);
            if (getToggleState())
                gl_rect_2d(2, getRect().getHeight() - 2, getRect().getWidth() - 2, 2, mOutline % alpha, false);
        }
        if (mMarked && getRect().getWidth() >= 6 && getRect().getHeight() >= 6)
        {
            const LLColor4 accent = LLUIColorTable::instance().getColor("FocusColor");
            const F32 alpha = mUseDrawContextAlpha ? getDrawContext().mAlpha : getCurrentTransparency();
            const S32 w = getRect().getWidth(), h = getRect().getHeight();
            gl_rect_2d(1, h - 1, w - 1, 1, accent % alpha, false);
            gl_rect_2d(2, h - 2, w - 2, 2, accent % alpha, false);
            if (w >= 18 && h >= 18)
            {
                gl_rect_2d(w - 15, h - 3, w - 3, h - 15, accent % alpha, true);
                const float ink = foreground({accent.mV[0], accent.mV[1], accent.mV[2]});
                const LLColor4 check(ink, ink, ink, alpha);
                gl_line_2d(w - 13, h - 9, w - 10, h - 12, check);
                gl_line_2d(w - 10, h - 12, w - 5, h - 6, check);
            }
        }
    }
private:
    bool mMarked = false;
    bool mSound = false;
    bool mCustomColor;
    LLColor4 mOutline;
};
LLSD pickerKey(FSFloaterGestureBoard* owner, const LLSD& expected)
{
    LLSD key; key["board"] = owner->selectedBoard(); key["expected"] = expected;
    key["window"] = owner->getKey(); key["owner"] = owner->windowID(); return key;
}
bool sameSession(const LLUUID& account, const LLUUID& session)
{ return account == gAgent.getID() && session == gAgent.getSessionID() && FSGestureBoardController::instance().available(); }
FSFloaterGestureBoard* ownerWindow(const LLSD& key)
{
    auto* owner = LLFloaterReg::findTypedInstance<FSFloaterGestureBoard>("gesture_board", key["window"]);
    return owner && owner->windowID() == key["owner"].asUUID() && owner->selectedBoard() == key["board"].asString() && owner->current() ? owner : nullptr;
}
constexpr size_t MAX_BOARD_WINDOWS = 16;
size_t windowCount()
{
    size_t count = 0;
    for (auto* window : LLFloaterReg::getFloaterList("gesture_board")) if (!window->isDead()) ++count;
    return count;
}
}
class FSGestureBoardTile final : public FSGestureBoardButton
{
public:
    FSGestureBoardTile(const Params& p, FSFloaterGestureBoard* board_owner, const Tile& tile)
        : FSGestureBoardButton(p, tile), mOwner(board_owner->getHandle()), mID(tile.id)
    {
        setUseEllipses(true);
        setCommitCallback([this](LLUICtrl*, const LLSD&) { if (auto* board = this->owner()) board->playTile(mID); });
    }
    const std::string& id() const { return mID; }
    void update(const Tile& tile)
    {
        auto& controller = FSGestureBoardController::instance();
        LLUUID item, asset; std::string name, reason;
        LLAssetType::EType type = LLAssetType::AT_NONE;
        if (!controller.resolve(LLUUID(tile.item), item, asset, name, reason, &type)) name = owner() ? owner()->getString("missing_tile") : "";
        setSound(type == LLAssetType::AT_SOUND);
        const std::string label = tile.label.empty() ? name : tile.label;
        setLabel(LLStringExplicit(label));
        const auto state = controller.tileState(tile);
        const std::string state_text = owner() ? owner()->getString(owner()->selecting() ? "tile_selecting" : "tile_" + state) : "";
        setToolTip(label + "\n" + name + (type == LLAssetType::AT_SOUND && owner() ? "\n" + owner()->getString("sound_item") : "") + "\n" + make_inventory_path(LLUUID(tile.item)) +
                   (state_text.empty() ? "" : "\n" + state_text));
        setToggleState(state == "playing" && owner() && !owner()->selecting());
    }
    bool handleRightMouseDown(S32 x, S32 y, MASK) override
    { if (auto* board = owner()) board->tileMenu(mID, this, x, y); return true; }
    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    {
        if (auto* board = owner()) if (board->selecting() || (mask & (MASK_CONTROL | MASK_SHIFT)))
        { mDragging = false; setFocus(true); board->selectTile(mID, mask); return true; }
        mStartX = x; mStartY = y; mDragging = false; return LLButton::handleMouseDown(x, y, mask);
    }
    bool handleHover(S32 x, S32 y, MASK mask) override
    {
        if (hasMouseCapture() && (mDragging || std::abs(x - mStartX) + std::abs(y - mStartY) >= 8))
        {
            mDragging = true; resetMouseDownTimer();
            S32 sx, sy; localPointToScreen(x, y, &sx, &sy);
            if (auto* board = owner()) board->dragTile(mID, sx, sy, false);
            return true;
        }
        return LLButton::handleHover(x, y, mask);
    }
    void onMouseCaptureLost() override { mDragging = false; LLButton::onMouseCaptureLost(); }
    bool handleMouseUp(S32 x, S32 y, MASK mask) override
    {
        if (auto* board = owner()) if (board->selecting()) return true;
        if (!mDragging) return LLButton::handleMouseUp(x, y, mask);
        mDragging = false; resetMouseDownTimer(); gFocusMgr.setMouseCapture(nullptr);
        S32 sx, sy; localPointToScreen(x, y, &sx, &sy);
        if (auto* board = owner()) board->dragTile(mID, sx, sy, true);
        return true; // Reordering must never commit the Play button.
    }
private:
    FSFloaterGestureBoard* owner() const { return dynamic_cast<FSFloaterGestureBoard*>(mOwner.get()); }
    LLHandle<LLFloater> mOwner;
    std::string mID;
    S32 mStartX = 0, mStartY = 0;
    bool mDragging = false;
};
FSFloaterGestureBoard::~FSFloaterGestureBoard()
{ closeEditors(); if (auto* menu = mMenu.get()) menu->die(); }
bool FSFloaterGestureBoard::postBuild()
{
    getChild<LLButton>("options")->setCommitCallback([this](LLUICtrl*, const LLSD&) { optionsMenu(); });
    getChild<LLButton>("previous_page")->setCommitCallback([this](LLUICtrl*, const LLSD&) { changePage(-1); });
    getChild<LLButton>("next_page")->setCommitCallback([this](LLUICtrl*, const LLSD&) { changePage(1); });
    getChild<LLButton>("add_items")->setCommitCallback([this](LLUICtrl*, const LLSD&) { addItems(); });
    getChild<LLButton>("stop")->setCommitCallback([this](LLUICtrl*, const LLSD&) { if (current()) FSGestureBoardController::instance().stopBoard(mBoard); });
    getChild<LLComboBox>("boards")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        if (!current()) return;
        selectBoard(getChild<LLComboBox>("boards")->getValue().asString());
    });
    getChild<LLButton>("edit_selected")->setCommitCallback([this](LLUICtrl*, const LLSD&) { editSelected(); });
    getChild<LLButton>("done_selecting")->setCommitCallback([this](LLUICtrl*, const LLSD&) { selectionMode(false); });
    mBuilt = true; refresh(true); return LLFloater::postBuild();
}
void FSFloaterGestureBoard::onOpen(const LLSD& key)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (windowCount() > MAX_BOARD_WINDOWS) { controller.setStatus("windows_full"); closeFloater(false); return; }
    if (controller.valid() && controller.data().boards.empty() && controller.snapshot().size() == 0)
    {
        Collection data; Board board; board.id = LLUUID::generateNewID().asString(); board.name = getString("default_board");
        data.selected = board.id; data.boards.push_back(board);
        controller.commit(data, controller.snapshot(), gAgent.getID(), gAgent.getSessionID());
    }
    if (key.has("board") && findBoard(controller.data(), key["board"].asString())) mBoard = key["board"].asString();
    refresh(true);
}
void FSFloaterGestureBoard::selectBoard(const std::string& id)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (!current() || !findBoard(controller.data(), id)) return;
    if (mBoard != id) { closeEditors(); mSelection.clear(); mSelecting = false; }
    mBoard = id; controller.selectBoard(id); refresh(true);
}
void FSFloaterGestureBoard::closeEditors()
{
    FSGestureBoardController::instance().cancelPendingForWindow(mWindowID);
    for (const char* name : {"gesture_board_picker", "gesture_board_editor"})
        if (auto* floater = LLFloaterReg::findInstance(name))
            if (floater->getKey()["owner"].asUUID() == mWindowID) floater->closeFloater(false);
    if (auto* menu = mMenu.get()) menu->hide();
}
void FSFloaterGestureBoard::onClose(bool) { closeEditors(); }
bool FSFloaterGestureBoard::current() const
{ return sameSession(mAccount, mSession) && FSGestureBoardController::instance().valid() && LLView::getVisible(); }
void FSFloaterGestureBoard::reshape(S32 width, S32 height, bool from_parent)
{
    LLFloater::reshape(width, height, from_parent);
    if (mBuilt && (getRect().getWidth() != mGridWidth || getRect().getHeight() != mGridHeight)) rebuild();
}
void FSFloaterGestureBoard::draw()
{
    if (mRebuildSelection || mRefreshTimer.getElapsedTimeF32() >= .25f) { refresh(); mRefreshTimer.reset(); }
    LLFloater::draw();
}
void FSFloaterGestureBoard::refresh(bool force)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (mAccount != gAgent.getID() || mSession != gAgent.getSessionID())
    { closeEditors(); mBoard.clear(); mSelection.clear(); mSelecting = false; mFocusReference.setNull(); mAccount = gAgent.getID(); mSession = gAgent.getSessionID(); force = true; }
    if (!findBoard(controller.data(), mBoard))
    {
        const auto& data = controller.data();
        const auto next = findBoard(data, data.selected) ? data.selected : data.boards.empty() ? "" : data.boards.front().id;
        if (next != mBoard) { closeEditors(); mBoard = next; force = true; }
    }
    if (force || mRebuildSelection || !controller.matchesSnapshot(mDisplayed))
    {
        mDisplayed = controller.snapshot();
        auto* boards = getChild<LLComboBox>("boards"); boards->removeall();
        for (const auto& board : controller.data().boards) boards->add(board.name, LLSD(board.id));
        boards->setValue(mBoard); rebuild();
    }
    const bool ready = controller.valid();
    const auto* board = findBoard(controller.data(), mBoard);
    LLStringUtil::format_map_t title; title["NAME"] = board ? board->name : getString("default_board");
    setTitle(getString("board_title", title));
    for (const char* name : {"add_items", "stop"}) getChild<LLButton>(name)->setEnabled(ready && board);
    getChild<LLButton>("options")->setEnabled(ready);
    getChild<LLComboBox>("boards")->setEnabled(ready && board);
    getChild<LLTextBox>("empty")->setVisible(!board || board->tiles.empty());
    const std::string status = ready ? controller.status() : controller.available() ? "invalid_data" : "logged_out";
    const bool warning = status != "ready" && status != "saved" && status != "playing" && status != "sound_played" && status != "loading" &&
        status != "stopped" && status != "cancelled";
    auto* indicator = getChild<LLTextBox>("status"); indicator->setVisible(warning);
    indicator->setToolTip(getString(status));
    if (board && mFocusReference.notNull()) rebuild(); // The referenced tile may be on another page.
    if (board) for (auto* view : mTiles) if (const auto* tile = findTile(*board, view->id()))
    {
        view->update(*tile); view->mark(mSelecting && mSelection.ids.count(tile->id));
        if (mFocusReference.notNull() && gInventory.getLinkedItemID(LLUUID(tile->item)) == gInventory.getLinkedItemID(mFocusReference))
        {
            view->setFocus(true); mFocusReference.setNull();
        }
    }
}
void FSFloaterGestureBoard::rebuild()
{
    auto* grid = getChild<LLView>("grid");
    mRebuildSelection = false; mTiles.clear(); grid->deleteAllChildren();
    mGridWidth = getRect().getWidth(); mGridHeight = getRect().getHeight();
    const auto& data = FSGestureBoardController::instance().data();
    const auto* board = findBoard(data, mBoard);
    if (mBoard != mDisplayedBoard) { mPage = 0; mDisplayedBoard = mBoard; mSelection.clear(); mSelecting = false; }
    if (board) mSelection.retain(*board); else { mSelection.clear(); mSelecting = false; }
    // LLFloater adds legacy header stretch after building XML children without
    // translating them. Lay out every surface from the same final dimensions;
    // mixing those XML positions with runtime tile positions covers the strip.
    auto frame = layoutFrame(mGridWidth, mGridHeight, getHeaderHeight(), false, mSelecting);
    auto pages = board ? paginate(*board, frame.grid.width, frame.grid.height, data.defaults) : std::vector<Page>(1);
    const bool multiple = pages.size() > 1;
    if (multiple && board)
    {
        frame = layoutFrame(mGridWidth, mGridHeight, getHeaderHeight(), true, mSelecting);
        pages = paginate(*board, frame.grid.width, frame.grid.height, data.defaults);
    }
    mPage = std::clamp(mPage, 0, static_cast<S32>(pages.size()) - 1);
    if (board && mFocusReference.notNull())
    {
        bool found = false;
        for (S32 page = 0; page < static_cast<S32>(pages.size()); ++page)
            for (const auto& place : pages[page])
                if (gInventory.getLinkedItemID(LLUUID(board->tiles[place.index].item)) == gInventory.getLinkedItemID(mFocusReference))
                { mPage = page; found = true; }
        if (!found) mFocusReference.setNull();
    }
    const auto position_view = [](LLView* view, const Rect& rect, S32 parent_height)
    { view->setShape(LLRect(rect.x, parent_height - rect.y, rect.x + rect.width, parent_height - rect.y - rect.height)); };
    position_view(getChild<LLComboBox>("boards"), frame.boards, mGridHeight);
    position_view(getChild<LLTextBox>("status"), frame.status, mGridHeight);
    position_view(getChild<LLButton>("add_items"), frame.add, mGridHeight);
    position_view(getChild<LLButton>("stop"), frame.stop, mGridHeight);
    position_view(getChild<LLButton>("options"), frame.options, mGridHeight);
    position_view(getChild<LLView>("selection"), frame.selection, mGridHeight);
    position_view(getChild<LLTextBox>("selected_count"), frame.selected_count, frame.selection.height);
    position_view(getChild<LLButton>("edit_selected"), frame.edit_selected, frame.selection.height);
    position_view(getChild<LLButton>("done_selecting"), frame.done_selecting, frame.selection.height);
    getChild<LLView>("selection")->setVisible(mSelecting);
    getChild<LLButton>("edit_selected")->setEnabled(!mSelection.ids.empty());
    LLStringUtil::format_map_t count; count["COUNT"] = std::to_string(mSelection.ids.size());
    getChild<LLTextBox>("selected_count")->setText(getString("selected_count", count));
    position_view(grid, frame.grid, mGridHeight);
    position_view(getChild<LLTextBox>("empty"), frame.empty, mGridHeight);
    position_view(getChild<LLView>("pagination"), frame.pagination, mGridHeight);
    position_view(getChild<LLButton>("previous_page"), frame.previous, frame.pagination.height);
    position_view(getChild<LLTextBox>("page_number"), frame.number, frame.pagination.height);
    position_view(getChild<LLButton>("next_page"), frame.next, frame.pagination.height);
    getChild<LLView>("pagination")->setVisible(multiple);
    getChild<LLButton>("previous_page")->setEnabled(mPage > 0);
    getChild<LLButton>("next_page")->setEnabled(mPage + 1 < static_cast<S32>(pages.size()));
    LLStringUtil::format_map_t args;
    args["CURRENT"] = std::to_string(mPage + 1);
    args["TOTAL"] = std::to_string(pages.size());
    getChild<LLTextBox>("page_number")->setText(getString("page_count", args));
    if (board)
    {
        for (const auto& place : pages[mPage])
        {
            const auto& definition = board->tiles[place.index];
            LLButton::Params p(LLUICtrlFactory::getDefaultParams<LLButton>()); p.name = definition.id; p.label = ""; p.auto_resize = false; p.commit_on_capture_lost = false;
            p.use_ellipses = true;
            p.rect = LLRect(place.x, frame.grid.height - place.y, place.x + place.width, frame.grid.height - place.y - place.height);
            auto* tile = new FSGestureBoardTile(p, this, presentation(data, *board, definition)); grid->addChild(tile); mTiles.push_back(tile); tile->update(definition);
            tile->mark(mSelecting && mSelection.ids.count(definition.id));
        }
    }
}
void FSFloaterGestureBoard::changePage(S32 direction)
{ if (!current()) return; mFocusReference.setNull(); mPage += direction; rebuild(); }
void FSFloaterGestureBoard::optionsMenu()
{
    if (!current() || !gMenuHolder) return;
    if (auto* prior = mMenu.get()) prior->die();
    auto& controller = FSGestureBoardController::instance();
    const auto id = mBoard; const LLSD expected = controller.snapshot();
    const auto handle = getHandle(); const auto account = mAccount, session = mSession;
    const auto* board = findBoard(controller.data(), id);
    LLContextMenu::Params p; p.name = "gesture_board_options";
    auto* menu = LLUICtrlFactory::create<LLContextMenu>(p); gMenuHolder->addChild(menu); mMenu = menu->getHandle();
    for (const char* action : {"window", "new", "rename", "duplicate", "delete", "defaults", "global_defaults", "select", "edit_selected", "select_all", "clear_selection"})
    {
        LLMenuItemCallGL::Params item; item.name = action; item.label = getString(std::string("board_") + action);
        item.enabled = std::string(action) == "window" ? windowCount() < MAX_BOARD_WINDOWS : std::string(action) == "new" ? controller.data().boards.size() < static_cast<size_t>(MAX_BOARDS) : board != nullptr;
        const std::string kind(action);
        if (kind == "select" && mSelecting) item.label = getString("board_finish_selection");
        if (kind == "global_defaults") item.enabled = true;
        if (kind == "edit_selected") item.enabled = board && !mSelection.ids.empty();
        if (kind == "clear_selection") item.enabled = !mSelection.ids.empty();
        item.on_click.function([handle, id, expected, action, account, session](LLUICtrl*, const LLSD&)
        { if (!sameSession(account, session)) return; if (auto* self = dynamic_cast<FSFloaterGestureBoard*>(handle.get())) self->boardAction(action, id, expected); });
        menu->addChild(LLUICtrlFactory::create<LLMenuItemCallGL>(item));
    }
    LLMenuItemCheckGL::Params compact; compact.name = "compact"; compact.label = getString("compact"); compact.enabled = board != nullptr;
    compact.on_check.function([checked = board && board->compact](LLUICtrl*, const LLSD&) { return checked; });
    compact.on_click.function([handle, id, expected, account, session](LLUICtrl*, const LLSD&)
    { if (!sameSession(account, session)) return; if (auto* self = dynamic_cast<FSFloaterGestureBoard*>(handle.get())) self->boardAction("compact", id, expected); });
    menu->addChild(LLUICtrlFactory::create<LLMenuItemCheckGL>(compact));
    auto* button = getChild<LLButton>("options"); S32 sx, sy; button->localPointToScreen(0, 0, &sx, &sy); menu->show(sx, sy, button);
}
void FSFloaterGestureBoard::boardAction(const std::string& action, const std::string& id, const LLSD& expected)
{
    if (!current()) return;
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (!controller.matchesSnapshot(expected) || mBoard != id) { controller.setStatus("changed"); return; }
    if (action == "window")
    {
        if (windowCount() >= MAX_BOARD_WINDOWS) { controller.setStatus("windows_full"); return; }
        LLSD key; key["window"] = LLUUID::generateNewID(); key["board"] = mBoard;
        LLFloaterReg::showInstance("gesture_board", key);
    }
    else if (action == "select") selectionMode(!mSelecting);
    else if (action == "edit_selected") editSelected();
    else if (action == "select_all")
    {
        selectionMode(true);
        if (const auto* board = findBoard(controller.data(), id)) mSelection.all(*board);
        mRebuildSelection = true;
    }
    else if (action == "clear_selection") { mSelection.clear(); mRebuildSelection = true; }
    else if (action == "compact")
    {
        auto data = controller.data(); auto* board = findBoard(data, id); if (!board) return;
        board->compact = !board->compact; controller.commit(data, expected, mAccount, mSession);
    }
    else if (action == "delete") deleteBoard();
    else editBoard(action);
}
void FSFloaterGestureBoard::playTile(const std::string& tile)
{
    if (!current()) return;
    if (mSelecting) { selectTile(tile, MASK_NONE); return; }
    auto& controller = FSGestureBoardController::instance(); controller.play(mBoard, tile, mWindowID);
}
void FSFloaterGestureBoard::selectionMode(bool enabled)
{
    if (!current()) return;
    if (mSelecting == enabled) return;
    closeEditors(); mSelecting = enabled; mSelection.clear(); mRebuildSelection = true;
}
void FSFloaterGestureBoard::selectTile(const std::string& id, MASK mask)
{
    if (!current()) return;
    const auto* board = findBoard(FSGestureBoardController::instance().data(), mBoard);
    if (!board || !findTile(*board, id)) return;
    selectionMode(true); mSelection.choose(*board, id, (mask & MASK_SHIFT) != 0); mFocusReference = LLUUID(findTile(*board, id)->item); mRebuildSelection = true;
}
bool FSFloaterGestureBoard::handleKeyHere(KEY key, MASK mask)
{
    if (mSelecting && key == KEY_ESCAPE && mask == MASK_NONE) { selectionMode(false); return true; }
    if (current() && mSelecting && key == 'A' && mask == MASK_CONTROL)
    {
        if (const auto* board = findBoard(FSGestureBoardController::instance().data(), mBoard)) mSelection.all(*board);
        mRebuildSelection = true; return true;
    }
    return LLFloater::handleKeyHere(key, mask);
}
void FSFloaterGestureBoard::editSelected()
{
    if (!current() || mSelection.ids.empty()) return;
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (const auto* board = findBoard(controller.data(), mBoard)) mSelection.retain(*board); else return;
    if (mSelection.ids.empty()) return;
    closeEditors(); auto key = pickerKey(this, controller.snapshot()); key["mode"] = "bulk";
    key["tiles"] = LLSD::emptyArray(); for (const auto& id : mSelection.ids) key["tiles"].append(id);
    LLFloaterReg::showInstance("gesture_board_editor", key);
}
void FSFloaterGestureBoard::addItems()
{
    if (!current()) return;
    closeEditors(); auto& controller = FSGestureBoardController::instance();
    LLFloaterReg::showInstance("gesture_board_picker", pickerKey(this, controller.snapshot()));
}
void FSFloaterGestureBoard::editBoard(const std::string& mode)
{
    if (!current()) return;
    closeEditors(); auto& controller = FSGestureBoardController::instance();
    auto key = pickerKey(this, controller.snapshot()); key["mode"] = mode;
    LLFloaterReg::showInstance("gesture_board_editor", key);
}
void FSFloaterGestureBoard::deleteBoard()
{
    if (!current()) return;
    closeEditors(); auto& controller = FSGestureBoardController::instance();
    const auto* board = findBoard(controller.data(), mBoard); if (!board) return;
    const auto id = board->id; const LLSD expected = controller.snapshot(); const auto account = mAccount, session = mSession;
    LLSD args; args["NAME"] = board->name; const auto handle = getHandle();
    LLNotificationsUtil::add("ConfirmGestureBoardDelete", args, LLSD(), [id, expected, account, session, handle](const LLSD& notification, const LLSD& response)
    {
        auto* owner = dynamic_cast<FSFloaterGestureBoard*>(handle.get());
        if (LLNotificationsUtil::getSelectedOption(notification, response) != 0 || !sameSession(account, session) ||
            !owner || !owner->current() || owner->selectedBoard() != id) return false;
        auto& controller = FSGestureBoardController::instance(); controller.refresh(); auto data = controller.data();
        data.boards.erase(std::remove_if(data.boards.begin(), data.boards.end(), [&](const Board& board) { return board.id == id; }), data.boards.end());
        if (data.selected == id) data.selected = data.boards.empty() ? "" : data.boards.front().id;
        controller.commit(data, expected, account, session); return false;
    });
}
void FSFloaterGestureBoard::tileMenu(const std::string& tile, LLView* view, S32 x, S32 y)
{
    if (!current() || !gMenuHolder) return;
    if (auto* prior = mMenu.get()) prior->die();
    auto& controller = FSGestureBoardController::instance();
    const auto board = mBoard; const LLSD expected = controller.snapshot(); const auto handle = getHandle(); const auto account = mAccount, session = mSession;
    LLContextMenu::Params p; p.name = "gesture_board_tile_menu";
    auto* menu = LLUICtrlFactory::create<LLContextMenu>(p); gMenuHolder->addChild(menu); mMenu = menu->getHandle();
    for (const char* action : {"edit", "edit_selected", "replace", "up", "down", "remove"})
    {
        LLMenuItemCallGL::Params item; item.name = action; item.label = getString(std::string("action_") + action);
        item.enabled = std::string(action) != "edit_selected" || !mSelection.ids.empty();
        item.on_click.function([handle, board, tile, expected, action, account, session](LLUICtrl*, const LLSD&)
        { if (!sameSession(account, session)) return; if (auto* self = dynamic_cast<FSFloaterGestureBoard*>(handle.get())) self->tileAction(action, board, tile, expected); });
        menu->addChild(LLUICtrlFactory::create<LLMenuItemCallGL>(item));
    }
    S32 sx, sy; view->localPointToScreen(x, y, &sx, &sy); menu->show(sx, sy, view);
}
void FSFloaterGestureBoard::tileAction(const std::string& action, const std::string& id, const std::string& tile_id, const LLSD& expected)
{
    if (!current()) return;
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (!controller.matchesSnapshot(expected) || mBoard != id) { controller.setStatus("changed"); return; }
    if (action == "edit_selected") { editSelected(); return; }
    if (action == "edit" || action == "replace")
    {
        closeEditors(); auto key = pickerKey(this, expected); key["mode"] = "tile"; key["tile"] = tile_id;
        key["replace"] = action == "replace"; LLFloaterReg::showInstance("gesture_board_editor", key); return;
    }
    auto data = controller.data(); auto* board = findBoard(data, id); if (!board) return;
    const auto it = std::find_if(board->tiles.begin(), board->tiles.end(), [&](const Tile& tile) { return tile.id == tile_id; });
    if (it == board->tiles.end()) return;
    if (action == "remove") board->tiles.erase(it);
    else if (!moveTile(*board, tile_id, static_cast<int>(it - board->tiles.begin()) + (action == "up" ? -1 : 1))) return;
    controller.commit(data, expected, mAccount, mSession);
}
void FSFloaterGestureBoard::dragTile(const std::string& id, S32 sx, S32 sy, bool finish)
{
    if (!current()) return;
    auto& controller = FSGestureBoardController::instance();
    for (auto* view : mTiles)
    {
        S32 x, y; view->screenPointToLocal(sx, sy, &x, &y);
        const bool target = view->pointInView(x, y);
        if (!finish) view->setToggleState(target);
        if (finish && target && view->id() != id)
        {
            auto data = controller.data(); auto* board = findBoard(data, mBoard); if (!board) return;
            const auto it = std::find_if(board->tiles.begin(), board->tiles.end(), [&](const Tile& tile) { return tile.id == view->id(); });
            if (it != board->tiles.end() && moveTile(*board, id, static_cast<int>(it - board->tiles.begin()))) controller.commit(data, controller.snapshot(), mAccount, mSession);
            return;
        }
    }
}
bool FSFloaterGestureBoard::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)
{
    // Handle Inventory references before mouse-opaque tiles/panels consume the
    // event. The former scroll container also always returned true on rejection.
    *accept = ACCEPT_NO;
    auto& controller = FSGestureBoardController::instance();
    const auto source = LLToolDragAndDrop::getInstance()->getSource();
    if (!pointInView(x, y) || !current() || !findBoard(controller.data(), mBoard) ||
        (source != LLToolDragAndDrop::SOURCE_AGENT && source != LLToolDragAndDrop::SOURCE_LIBRARY) ||
        (type != DAD_GESTURE && type != DAD_SOUND && type != DAD_LINK) || !cargo) return true;
    const auto* reference = static_cast<LLInventoryItem*>(cargo);
    LLUUID item, asset; std::string name, reason;
    if (!controller.resolve(reference->getUUID(), item, asset, name, reason)) { tooltip = getString(reason); return true; }
    *accept = ACCEPT_YES_COPY_MULTI;
    if (drop)
    {
        if (controller.addItems(mBoard, {reference->getUUID()}, controller.snapshot(), mAccount, mSession)) focusItem(reference->getUUID());
        else *accept = ACCEPT_NO;
    }
    return true; // Inventory drags add; Replace is an explicit picker action.
}

bool FSFloaterGesturePicker::postBuild()
{
    auto* panel = getChild<LLInventoryPanel>("inventory");
    panel->setFilterTypes((1ULL << LLInventoryType::IT_GESTURE) | (1ULL << LLInventoryType::IT_SOUND));
    panel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS); panel->setSuppressOpenItemAction(true);
    getChild<LLFilterEditor>("search")->setCommitCallback([panel](LLUICtrl*, const LLSD& value) { panel->setFilterSubString(value.asString()); });
    getChild<LLButton>("add")->setCommitCallback([this](LLUICtrl*, const LLSD&) { acceptSelection(); });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&) { closeFloater(false); });
    return LLFloater::postBuild();
}
void FSFloaterGesturePicker::onOpen(const LLSD& key)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh(); controller.cancelPendingForWindow(key["owner"].asUUID());
    mKey = key; mExpected = key["expected"]; mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
    getChild<LLInventoryPanel>("inventory")->clearSelection();
    getChild<LLTextBox>("status")->setText(getString("picker_help"));
}
void FSFloaterGesturePicker::onClose(bool) { FSGestureBoardController::instance().cancelPendingForWindow(mKey["owner"].asUUID()); }
void FSFloaterGesturePicker::draw()
{
    const bool current = sameSession(mAccount, mSession) && ownerWindow(mKey) && FSGestureBoardController::instance().valid();
    getChild<LLButton>("add")->setEnabled(current && gInventory.isInventoryUsable() && !getChild<LLInventoryPanel>("inventory")->getSelectedItems().empty());
    if (!current) { closeFloater(false); return; }
    LLFloater::draw();
}
void FSFloaterGesturePicker::acceptSelection()
{
    if (!sameSession(mAccount, mSession) || !ownerWindow(mKey)) return;
    std::vector<LLUUID> selected;
    for (auto* item : getChild<LLInventoryPanel>("inventory")->getSelectedItems())
        if (const auto* model = dynamic_cast<LLFolderViewModelItemInventory*>(item->getViewModelItem())) selected.push_back(model->getUUID());
    auto& controller = FSGestureBoardController::instance();
    if (mKey.has("editor_token"))
    {
        if (selected.size() != 1) { getChild<LLTextBox>("status")->setText(getString("select_one")); return; }
        auto* editor = LLFloaterReg::findTypedInstance<FSFloaterGestureTileEditor>("gesture_board_editor");
        if (editor && editor->replaceItem(selected.front(), mKey)) closeFloater(false);
        else getChild<LLTextBox>("status")->setText(getString(controller.status()));
    }
    else if (controller.addItems(mKey["board"].asString(), selected, mExpected, mAccount, mSession))
    {
        if (!selected.empty()) if (auto* board = ownerWindow(mKey)) board->focusItem(selected.front());
        closeFloater(false);
    }
    else getChild<LLTextBox>("status")->setText(getString(controller.status()));
}

bool FSFloaterGestureTileEditor::postBuild()
{
    getChild<LLButton>("save")->setCommitCallback([this](LLUICtrl*, const LLSD&) { save(); });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&) { closeFloater(false); });
    getChild<LLButton>("replace")->setCommitCallback([this](LLUICtrl*, const LLSD&) { replace(); });
    getChild<LLButton>("reset_label")->setCommitCallback([this](LLUICtrl*, const LLSD&) { getChild<LLLineEditor>("label")->setText(LLStringExplicit("")); updatePreview(); });
    for (const char* name : {"color_mode", "size_mode"})
        getChild<LLComboBox>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { updatePreview(); });
    for (const char* name : {"apply_color", "apply_size"})
        getChild<LLCheckBoxCtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { updatePreview(); });
    for (const char* name : {"tile_width", "tile_height"})
        getChild<LLSpinCtrl>(name)->setCommitCallback([this](LLUICtrl*, const LLSD&) { updatePreview(); });
    auto* swatch = getChild<LLColorSwatchCtrl>("color");
    swatch->setCommitCallback([this](LLUICtrl*, const LLSD&) { if (LLView::getVisible()) updatePreview(); });
    swatch->setPreviewCallback([this](LLUICtrl*, const LLSD&) { if (LLView::getVisible()) updatePreview(); });
    getChild<LLLineEditor>("label")->setKeystrokeCallback([](LLLineEditor*, void* self) { static_cast<FSFloaterGestureTileEditor*>(self)->updatePreview(); }, this);
    return LLFloater::postBuild();
}
void FSFloaterGestureTileEditor::onOpen(const LLSD& key)
{
    getChild<LLColorSwatchCtrl>("color")->closeFloaterColorPicker();
    auto& controller = FSGestureBoardController::instance(); controller.refresh(); controller.cancelPendingForWindow(key["owner"].asUUID());
    mKey = key; mExpected = key["expected"]; mAccount = gAgent.getID(); mSession = gAgent.getSessionID(); mToken = LLUUID::generateNewID();
    const auto* board = findBoard(controller.data(), key["board"].asString());
    const auto mode = key["mode"].asString();
    const bool tile_mode = mode == "tile", bulk = mode == "bulk", global = mode == "global_defaults";
    const bool styling = tile_mode || bulk || global || mode == "defaults";
    getChild<LLView>("tile_controls")->setVisible(tile_mode);
    getChild<LLView>("style_controls")->setVisible(styling);
    getChild<LLView>("board_controls")->setVisible(!styling);
    reshape(440, getHeaderHeight() + (tile_mode ? 540 : styling ? 392 : 163), false);
    const S32 top = getRect().getHeight() - getHeaderHeight() - 6;
    const S32 right = getRect().getWidth() - 12;
    getChild<LLView>("board_controls")->setShape(LLRect(12, top, right, top - 64));
    getChild<LLView>("tile_controls")->setShape(LLRect(12, top, right, top - 140));
    const S32 style_top = top - (tile_mode ? 148 : 0);
    getChild<LLView>("style_controls")->setShape(LLRect(12, style_top, right, style_top - 282));
    getChild<LLTextBox>("status")->setShape(LLRect(12, 88, right, 48));
    getChild<LLButton>("save")->setShape(LLRect(12, 36, 122, 12));
    getChild<LLButton>("cancel")->setShape(LLRect(right - 110, 36, right, 12));
    getChild<LLLineEditor>("board_name")->setText(LLStringExplicit(board && mode != "new" ? board->name : ""));
    const auto* tile = board ? findTile(*board, key["tile"].asString()) : nullptr;
    mSelection.clear();
    if (bulk)
    {
        if (!board || !key["tiles"].isArray() || key["tiles"].size() == 0 || key["tiles"].size() > static_cast<size_t>(MAX_TILES)) { closeFloater(false); return; }
        for (S32 index = 0; index < static_cast<S32>(key["tiles"].size()); ++index)
        {
            const auto& id = key["tiles"][index];
            if (!id.isString() || !findTile(*board, id.asString()) || !mSelection.insert(id.asString()).second) { closeFloater(false); return; }
        }
        tile = findTile(*board, *mSelection.begin());
    }
    mTile = tile ? *tile : Tile{};
    if (global) static_cast<Style&>(mTile) = controller.data().defaults;
    else if (mode == "defaults" && board) static_cast<Style&>(mTile) = board->defaults;
    mOriginalStyle = mTile;
    getChild<LLLineEditor>("label")->setText(LLStringExplicit(mTile.label));
    const auto inherited = global ? getString("theme_default") : getString(mode == "defaults" ? "global_default" : "board_default");
    auto* colors = getChild<LLComboBox>("color_mode"); colors->removeall();
    colors->add(inherited, LLSD("inherit"));
    if (!global) colors->add(getString("theme_default"), LLSD("theme"));
    colors->add(getString("custom_color"), LLSD("custom"));
    colors->setValue(mTile.custom_color ? "custom" : mTile.theme_color && !global ? "theme" : "inherit");
    getChild<LLColorSwatchCtrl>("color")->set(color(mTile));
    auto* sizes = getChild<LLComboBox>("size_mode"); sizes->removeall();
    sizes->add(getString(global ? "density_size" : mode == "defaults" ? "global_default" : "board_default"), LLSD("inherit"));
    sizes->add(getString("custom_size"), LLSD("custom")); sizes->setValue(mTile.width ? "custom" : "inherit");
    const auto resolved = editorPresentation(mode, controller.data(), board, mTile);
    getChild<LLSpinCtrl>("tile_width")->set(static_cast<F32>(resolved.width ? resolved.width : 100));
    getChild<LLSpinCtrl>("tile_height")->set(static_cast<F32>(resolved.height ? resolved.height : 28));
    for (const char* name : {"apply_color", "apply_size"}) { getChild<LLCheckBoxCtrl>(name)->set(false); getChild<LLView>(name)->setVisible(bulk); }
    getChild<LLView>("color_caption")->setVisible(!bulk); getChild<LLView>("size_caption")->setVisible(!bulk);
    LLStringUtil::format_map_t args; args["COUNT"] = std::to_string(mSelection.size());
    setTitle(getString(tile_mode ? "title_tile" : bulk ? "title_bulk" : global ? "title_global" : mode == "defaults" ? "title_defaults" : "title_board", args));
    getChild<LLTextBox>("status")->setText(getString(tile_mode ? "editor_help" : bulk ? "bulk_help" : global ? "global_help" : mode == "defaults" ? "defaults_help" : "board_help"));
    updatePreview(); if (tile_mode && key["replace"].asBoolean()) replace();
}
void FSFloaterGestureTileEditor::onClose(bool)
{
    const auto token = mToken; mToken.setNull(); getChild<LLColorSwatchCtrl>("color")->closeFloaterColorPicker();
    FSGestureBoardController::instance().cancelPendingForWindow(mKey["owner"].asUUID());
    if (auto* picker = LLFloaterReg::findInstance("gesture_board_picker"))
        if (token.notNull() && picker->getKey()["editor_token"].asUUID() == token) picker->closeFloater(false);
}
void FSFloaterGestureTileEditor::draw()
{
    if (!sameSession(mAccount, mSession) || !ownerWindow(mKey)) { closeFloater(false); return; }
    LLFloater::draw();
}
void FSFloaterGestureTileEditor::replace()
{
    if (!sameSession(mAccount, mSession) || !ownerWindow(mKey) || mTile.id.empty()) return;
    auto key = mKey; key["expected"] = mExpected; key["tile"] = mTile.id; key["editor_token"] = mToken;
    FSGestureBoardController::instance().cancelPendingForWindow(mKey["owner"].asUUID()); LLFloaterReg::showInstance("gesture_board_picker", key);
}
bool FSFloaterGestureTileEditor::replaceItem(const LLUUID& reference, const LLSD& key)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (!LLView::getVisible() || !sameSession(mAccount, mSession) || !ownerWindow(mKey) || key["owner"] != mKey["owner"] || mToken.isNull() || key["editor_token"].asUUID() != mToken ||
        key["tile"].asString() != mTile.id || key["board"] != mKey["board"] || !controller.matchesSnapshot(mExpected))
    { controller.setStatus("changed"); return false; }
    LLUUID item, asset; std::string name, reason;
    if (!controller.resolve(reference, item, asset, name, reason)) { controller.setStatus(reason); return false; }
    const auto* board = findBoard(controller.data(), mKey["board"].asString());
    if (board) for (const auto& tile : board->tiles)
        if (tile.id != mTile.id && gInventory.getLinkedItemID(LLUUID(tile.item)) == item) { controller.setStatus("duplicate_tile"); return false; }
    mTile.item = reference.asString(); updatePreview(); return true;
}
void FSFloaterGestureTileEditor::updatePreview()
{
    if (mUpdatingPreview) return;
    mUpdatingPreview = true;
    struct ResetPreviewFlag { bool& value; ~ResetPreviewFlag() { value = false; } } reset{mUpdatingPreview};
    const auto mode = mKey["mode"].asString();
    const bool tile_mode = mode == "tile", bulk = mode == "bulk";
    const bool apply_color = !bulk || getChild<LLCheckBoxCtrl>("apply_color")->get();
    const bool apply_size = !bulk || getChild<LLCheckBoxCtrl>("apply_size")->get();
    if (tile_mode) mTile.label = getChild<LLLineEditor>("label")->getText();
    const auto color_mode = getChild<LLComboBox>("color_mode")->getValue().asString();
    if (apply_color) { mTile.custom_color = color_mode == "custom"; mTile.theme_color = color_mode == "theme"; }
    else { mTile.custom_color = mOriginalStyle.custom_color; mTile.theme_color = mOriginalStyle.theme_color; mTile.color = mOriginalStyle.color; }
    auto* swatch = getChild<LLColorSwatchCtrl>("color");
    if (!apply_color || !mTile.custom_color) swatch->closeFloaterColorPicker();
    // Detach retained pickers before disabling; their callbacks can re-enter.
    if (apply_color) { const auto& picked = swatch->get(); for (int i = 0; i < 3; ++i) mTile.color[i] = picked.mV[i]; }
    getChild<LLComboBox>("color_mode")->setEnabled(apply_color); swatch->setEnabled(apply_color && mTile.custom_color);
    const bool custom_size = getChild<LLComboBox>("size_mode")->getValue().asString() == "custom";
    getChild<LLComboBox>("size_mode")->setEnabled(apply_size);
    getChild<LLSpinCtrl>("tile_width")->setEnabled(apply_size && custom_size); getChild<LLSpinCtrl>("tile_height")->setEnabled(apply_size && custom_size);
    if (apply_size)
    {
        mTile.width = custom_size ? getChild<LLSpinCtrl>("tile_width")->getValue().asInteger() : 0;
        mTile.height = custom_size ? getChild<LLSpinCtrl>("tile_height")->getValue().asInteger() : 0;
    }
    else { mTile.width = mOriginalStyle.width; mTile.height = mOriginalStyle.height; }
    LLUUID item, asset; std::string name, reason; LLAssetType::EType type = LLAssetType::AT_NONE;
    if (tile_mode && !FSGestureBoardController::instance().resolve(LLUUID(mTile.item), item, asset, name, reason, &type)) name = getString("missing_tile");
    if (!tile_mode) name = getString("sample_label");
    getChild<LLTextBox>("item_name")->setText(LLStringExplicit(name)); getChild<LLTextBox>("item_name")->setToolTip(name);
    auto* sample = getChild<LLView>("sample"); sample->deleteAllChildren();
    const auto& collection = FSGestureBoardController::instance().data();
    const auto* board = findBoard(collection, mKey["board"].asString());
    const auto resolved = editorPresentation(mode, collection, board, mTile);
    LLButton::Params p(LLUICtrlFactory::getDefaultParams<LLButton>()); p.name = "preview_tile";
    p.label = tile_mode && !mTile.label.empty() ? mTile.label : name; p.use_ellipses = true; p.auto_resize = false;
    const S32 top = sample->getRect().getHeight();
    p.rect = LLRect(0, top, resolved.width, top - resolved.height); p.tab_stop = false;
    auto* button = new FSGestureBoardButton(p, resolved); button->setSound(type == LLAssetType::AT_SOUND); sample->addChild(button);
}
void FSFloaterGestureTileEditor::save()
{
    if (!sameSession(mAccount, mSession) || !ownerWindow(mKey)) return;
    auto& controller = FSGestureBoardController::instance(); controller.refresh(); auto data = controller.data();
    const auto mode = mKey["mode"].asString(); auto* board = findBoard(data, mKey["board"].asString());
    if (mode == "tile")
    {
        updatePreview(); auto* tile = board ? findTile(*board, mTile.id) : nullptr;
        if (!tile) { controller.setStatus("changed"); getChild<LLTextBox>("status")->setText(getString("changed")); return; }
        *tile = mTile;
    }
    else if (mode == "bulk")
    {
        updatePreview();
        if (!board || !applyStyle(*board, mSelection, mTile, getChild<LLCheckBoxCtrl>("apply_color")->get(), getChild<LLCheckBoxCtrl>("apply_size")->get()))
        { getChild<LLTextBox>("status")->setText(getString("choose_properties")); return; }
    }
    else if (mode == "global_defaults") { updatePreview(); data.defaults = mTile; }
    else if (mode == "defaults" && board) { updatePreview(); board->defaults = mTile; }
    else
    {
        std::string name = getChild<LLLineEditor>("board_name")->getText(); LLStringUtil::trim(name);
        if (!validText(name)) { getChild<LLTextBox>("status")->setText(getString("invalid_name")); return; }
        for (const auto& entry : data.boards) if (entry.name == name && (mode != "rename" || entry.id != mKey["board"].asString()))
        { getChild<LLTextBox>("status")->setText(getString("exists")); return; }
        if (mode == "rename" && board) board->name = name;
        else if (mode == "new" || (mode == "duplicate" && board))
        {
            if (data.boards.size() >= static_cast<size_t>(MAX_BOARDS)) { getChild<LLTextBox>("status")->setText(getString("full")); return; }
            Board created = mode == "duplicate" ? *board : Board{}; created.id = LLUUID::generateNewID().asString(); created.name = name;
            for (auto& tile : created.tiles) tile.id = LLUUID::generateNewID().asString();
            data.selected = created.id; data.boards.push_back(created);
        }
        else { controller.setStatus("changed"); getChild<LLTextBox>("status")->setText(getString("changed")); return; }
    }
    if (controller.commit(data, mExpected, mAccount, mSession))
    {
        if (mode == "new" || mode == "duplicate") if (auto* owner = ownerWindow(mKey)) owner->selectBoard(data.selected);
        closeFloater(false);
    }
    else getChild<LLTextBox>("status")->setText(getString(controller.status()));
}
