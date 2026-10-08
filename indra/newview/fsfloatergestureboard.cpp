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
#include "llscrollcontainer.h"
#include "llsdutil.h"
#include "lltextbox.h"
#include "lltooldraganddrop.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "lluictrlfactory.h"
#include <algorithm>
#include <cmath>
namespace
{
using namespace FSGestureBoard;
LLColor4 color(const Tile& tile) { return LLColor4(tile.color[0], tile.color[1], tile.color[2], 1.f); }
LLColor4 foregroundColor(const Tile& tile) { const float c = foreground(tile.color); return LLColor4(c, c, c, 1.f); }
void style(LLButton* button, const Tile& tile)
{
    if (!tile.custom_color) return; // Fresh buttons use the current skin.
    button->setImageColor(color(tile)); button->setDisabledImageColor(color(tile));
    button->setUnselectedLabelColor(foregroundColor(tile)); button->setSelectedLabelColor(foregroundColor(tile));
}
LLSD pickerKey(const std::string& board, const LLSD& expected)
{ LLSD key; key["board"] = board; key["expected"] = expected; return key; }
bool sameSession(const LLUUID& account, const LLUUID& session)
{ return account == gAgent.getID() && session == gAgent.getSessionID() && FSGestureBoardController::instance().available(); }
}
class FSGestureBoardTile final : public LLButton
{
public:
    FSGestureBoardTile(const Params& p, FSFloaterGestureBoard* board_owner, const Tile& tile)
        : LLButton(p), mOwner(board_owner->getHandle()), mID(tile.id)
    {
        initFromParams(p);
        style(this, tile);
        LLTextBox::Params text; text.name = "tile_text"; text.rect = LLRect(8, 68, p.rect().getWidth() - 8, 28);
        text.mouse_opaque = false; text.tab_stop = false; text.enabled = false; text.wrap = true; text.parse_urls = false; text.plain_text = true;
        text.font = LLFontGL::getFontSansSerif();
        if (tile.custom_color) { text.text_color = foregroundColor(tile); text.text_readonly_color = foregroundColor(tile); }
        mText = LLUICtrlFactory::create<LLTextBox>(text, this);
        text.name = "tile_state"; text.rect = LLRect(8, 24, p.rect().getWidth() - 8, 4); text.wrap = false;
        mState = LLUICtrlFactory::create<LLTextBox>(text, this);
        setCommitCallback([this](LLUICtrl*, const LLSD&) { if (auto* board = this->owner()) board->playTile(mID); });
    }
    const std::string& id() const { return mID; }
    void update(const Tile& tile)
    {
        auto& controller = FSGestureBoardController::instance();
        LLUUID item, asset; std::string name, reason;
        if (!controller.resolve(LLUUID(tile.item), item, asset, name, reason)) name = owner() ? owner()->getString("missing_tile") : "";
        const std::string label = tile.label.empty() ? name : tile.label;
        mText->setText(LLStringExplicit(label));
        const auto state = controller.tileState(tile);
        mState->setText(LLStringExplicit(owner() ? owner()->getString("tile_" + state) : ""));
        setToolTip(label + "\n" + name + "\n" + make_inventory_path(LLUUID(tile.item)));
        setToggleState(state == "playing");
    }
    bool handleRightMouseDown(S32 x, S32 y, MASK) override
    { if (auto* board = owner()) board->tileMenu(mID, this, x, y); return true; }
    bool handleMouseDown(S32 x, S32 y, MASK mask) override
    { mStartX = x; mStartY = y; mDragging = false; return LLButton::handleMouseDown(x, y, mask); }
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
    LLTextBox *mText = nullptr, *mState = nullptr;
    S32 mStartX = 0, mStartY = 0;
    bool mDragging = false;
};
FSFloaterGestureBoard::~FSFloaterGestureBoard()
{ FSGestureBoardController::instance().cancelPending(); if (auto* menu = mMenu.get()) menu->die(); }
bool FSFloaterGestureBoard::postBuild()
{
    for (const char* mode : {"new", "rename", "duplicate"})
        getChild<LLButton>(std::string("board_") + mode)->setCommitCallback([this, mode](LLUICtrl*, const LLSD&) { editBoard(mode); });
    getChild<LLButton>("board_delete")->setCommitCallback([this](LLUICtrl*, const LLSD&) { deleteBoard(); });
    getChild<LLButton>("add_gestures")->setCommitCallback([this](LLUICtrl*, const LLSD&) { addGestures(); });
    getChild<LLButton>("stop")->setCommitCallback([this](LLUICtrl*, const LLSD&) { if (current()) FSGestureBoardController::instance().stopBoard(); });
    getChild<LLComboBox>("boards")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    {
        if (!current()) return;
        closeEditors(); FSGestureBoardController::instance().selectBoard(getChild<LLComboBox>("boards")->getValue().asString()); refresh(true);
    });
    mBuilt = true; refresh(true); return LLFloater::postBuild();
}
void FSFloaterGestureBoard::onOpen(const LLSD&)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (controller.valid() && controller.data().boards.empty() && controller.snapshot().size() == 0)
    {
        Collection data; Board board; board.id = LLUUID::generateNewID().asString(); board.name = getString("default_board");
        data.selected = board.id; data.boards.push_back(board);
        controller.commit(data, controller.snapshot(), gAgent.getID(), gAgent.getSessionID());
    }
    refresh(true);
}
void FSFloaterGestureBoard::closeEditors()
{
    FSGestureBoardController::instance().cancelPending();
    for (const char* name : {"gesture_board_picker", "gesture_board_editor"})
        if (auto* floater = LLFloaterReg::findInstance(name)) floater->closeFloater(false);
    if (auto* menu = mMenu.get()) menu->hide();
}
void FSFloaterGestureBoard::onClose(bool) { closeEditors(); }
bool FSFloaterGestureBoard::current() const
{ return sameSession(mAccount, mSession) && FSGestureBoardController::instance().valid() && LLView::getVisible(); }
void FSFloaterGestureBoard::reshape(S32 width, S32 height, bool from_parent)
{
    LLFloater::reshape(width, height, from_parent);
    if (mBuilt && getChild<LLView>("grid")->getRect().getWidth() != mGridWidth) rebuild();
}
void FSFloaterGestureBoard::draw()
{
    if (mRefreshTimer.getElapsedTimeF32() >= .25f) { refresh(); mRefreshTimer.reset(); }
    LLFloater::draw();
}
void FSFloaterGestureBoard::refresh(bool force)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (mAccount != gAgent.getID() || mSession != gAgent.getSessionID())
    { closeEditors(); mFocusReference.setNull(); mAccount = gAgent.getID(); mSession = gAgent.getSessionID(); force = true; }
    if (force || mDisplayed != controller.snapshot())
    {
        mDisplayed = controller.snapshot();
        auto* boards = getChild<LLComboBox>("boards"); boards->removeall();
        for (const auto& board : controller.data().boards) boards->add(board.name, LLSD(board.id));
        boards->setValue(controller.data().selected); rebuild();
    }
    const bool ready = controller.valid();
    const auto* board = findBoard(controller.data(), controller.data().selected);
    for (const char* name : {"board_rename", "board_duplicate", "board_delete", "add_gestures", "stop"}) getChild<LLButton>(name)->setEnabled(ready && board);
    getChild<LLButton>("board_new")->setEnabled(ready && controller.data().boards.size() < static_cast<size_t>(MAX_BOARDS));
    getChild<LLComboBox>("boards")->setEnabled(ready && board);
    getChild<LLTextBox>("empty")->setVisible(!board || board->tiles.empty());
    getChild<LLTextBox>("status")->setText(getString(ready ? controller.status() : controller.available() ? "invalid_data" : "logged_out"));
    if (board) for (auto* view : mTiles) if (const auto* tile = findTile(*board, view->id()))
    {
        view->update(*tile);
        if (mFocusReference.notNull() && gInventory.getLinkedItemID(LLUUID(tile->item)) == gInventory.getLinkedItemID(mFocusReference))
        {
            view->setFocus(true); getChild<LLScrollContainer>("tiles_scroll")->scrollToShowRect(view->getRect()); mFocusReference.setNull();
        }
    }
}
void FSFloaterGestureBoard::rebuild()
{
    auto* grid = getChild<LLView>("grid");
    mTiles.clear(); grid->deleteAllChildren();
    mGridWidth = grid->getRect().getWidth();
    const auto& data = FSGestureBoardController::instance().data();
    const auto* board = findBoard(data, data.selected);
    const S32 columns = std::max(1, (mGridWidth - 8) / 148);
    const S32 count = board ? static_cast<S32>(board->tiles.size()) : 0;
    const S32 rows = std::max(1, (count + columns - 1) / columns), height = rows * 88 + 8;
    grid->reshape(mGridWidth, height);
    if (board)
    {
        const S32 width = (mGridWidth - 8) / columns - 8;
        for (S32 i = 0; i < count; ++i)
        {
            LLButton::Params p(LLUICtrlFactory::getDefaultParams<LLButton>()); p.name = board->tiles[i].id; p.label = ""; p.auto_resize = false; p.commit_on_capture_lost = false;
            const S32 left = 8 + (i % columns) * (width + 8), top = height - 8 - (i / columns) * 88;
            p.rect = LLRect(left, top, left + width, top - 80);
            auto* tile = new FSGestureBoardTile(p, this, board->tiles[i]); grid->addChild(tile); mTiles.push_back(tile); tile->update(board->tiles[i]);
        }
    }
    auto* scroll = getChild<LLScrollContainer>("tiles_scroll"); scroll->reshape(scroll->getRect().getWidth(), scroll->getRect().getHeight());
}
void FSFloaterGestureBoard::playTile(const std::string& tile)
{
    if (!current()) return;
    auto& controller = FSGestureBoardController::instance(); controller.play(controller.data().selected, tile);
}
void FSFloaterGestureBoard::addGestures()
{
    if (!current()) return;
    closeEditors(); auto& controller = FSGestureBoardController::instance();
    LLFloaterReg::showInstance("gesture_board_picker", pickerKey(controller.data().selected, controller.snapshot()));
}
void FSFloaterGestureBoard::editBoard(const std::string& mode)
{
    if (!current()) return;
    closeEditors(); auto& controller = FSGestureBoardController::instance();
    auto key = pickerKey(controller.data().selected, controller.snapshot()); key["mode"] = mode;
    LLFloaterReg::showInstance("gesture_board_editor", key);
}
void FSFloaterGestureBoard::deleteBoard()
{
    if (!current()) return;
    closeEditors(); auto& controller = FSGestureBoardController::instance();
    const auto* board = findBoard(controller.data(), controller.data().selected); if (!board) return;
    const auto id = board->id; const LLSD expected = controller.snapshot(); const auto account = mAccount, session = mSession;
    LLSD args; args["NAME"] = board->name;
    LLNotificationsUtil::add("ConfirmGestureBoardDelete", args, LLSD(), [id, expected, account, session](const LLSD& notification, const LLSD& response)
    {
        if (LLNotificationsUtil::getSelectedOption(notification, response) != 0 || !sameSession(account, session)) return false;
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
    const auto board = controller.data().selected; const LLSD expected = controller.snapshot(); const auto handle = getHandle(); const auto account = mAccount, session = mSession;
    LLContextMenu::Params p; p.name = "gesture_board_tile_menu";
    auto* menu = LLUICtrlFactory::create<LLContextMenu>(p); gMenuHolder->addChild(menu); mMenu = menu->getHandle();
    for (const char* action : {"edit", "replace", "up", "down", "remove"})
    {
        LLMenuItemCallGL::Params item; item.name = action; item.label = getString(std::string("action_") + action);
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
    if (controller.snapshot() != expected || controller.data().selected != id) { controller.setStatus("changed"); return; }
    if (action == "edit" || action == "replace")
    {
        closeEditors(); auto key = pickerKey(id, expected); key["mode"] = "tile"; key["tile"] = tile_id;
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
            auto data = controller.data(); auto* board = findBoard(data, data.selected); if (!board) return;
            const auto it = std::find_if(board->tiles.begin(), board->tiles.end(), [&](const Tile& tile) { return tile.id == view->id(); });
            if (it != board->tiles.end() && moveTile(*board, id, static_cast<int>(it - board->tiles.begin()))) controller.commit(data, controller.snapshot(), mAccount, mSession);
            return;
        }
    }
}
bool FSFloaterGestureBoard::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop, EDragAndDropType type, void* cargo, EAcceptance* accept, std::string& tooltip)
{
    if (childrenHandleDragAndDrop(x, y, mask, drop, type, cargo, accept, tooltip)) return true;
    *accept = ACCEPT_NO;
    auto& controller = FSGestureBoardController::instance();
    const auto source = LLToolDragAndDrop::getInstance()->getSource();
    if (!current() || !findBoard(controller.data(), controller.data().selected) ||
        (source != LLToolDragAndDrop::SOURCE_AGENT && source != LLToolDragAndDrop::SOURCE_LIBRARY) ||
        (type != DAD_GESTURE && type != DAD_LINK) || !cargo) return true;
    const auto* reference = static_cast<LLInventoryItem*>(cargo);
    LLUUID item, asset; std::string name, reason;
    if (!controller.resolve(reference->getUUID(), item, asset, name, reason)) { tooltip = getString(reason); return true; }
    *accept = ACCEPT_YES_COPY_MULTI;
    if (drop)
    {
        if (controller.addItems(controller.data().selected, {reference->getUUID()}, controller.snapshot(), mAccount, mSession)) focusGesture(reference->getUUID());
        else *accept = ACCEPT_NO;
    }
    return true; // Inventory drags add; Replace is an explicit picker action.
}

bool FSFloaterGesturePicker::postBuild()
{
    auto* panel = getChild<LLInventoryPanel>("inventory");
    panel->setFilterTypes(1ULL << LLInventoryType::IT_GESTURE);
    panel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS); panel->setSuppressOpenItemAction(true);
    getChild<LLFilterEditor>("search")->setCommitCallback([panel](LLUICtrl*, const LLSD& value) { panel->setFilterSubString(value.asString()); });
    getChild<LLButton>("add")->setCommitCallback([this](LLUICtrl*, const LLSD&) { acceptSelection(); });
    getChild<LLButton>("cancel")->setCommitCallback([this](LLUICtrl*, const LLSD&) { closeFloater(false); });
    return LLFloater::postBuild();
}
void FSFloaterGesturePicker::onOpen(const LLSD& key)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh(); controller.cancelPending();
    mKey = key; mExpected = key["expected"]; mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
    getChild<LLInventoryPanel>("inventory")->clearSelection();
    getChild<LLTextBox>("status")->setText(getString("picker_help"));
}
void FSFloaterGesturePicker::onClose(bool) { FSGestureBoardController::instance().cancelPending(); }
void FSFloaterGesturePicker::draw()
{
    const bool current = sameSession(mAccount, mSession) && FSGestureBoardController::instance().valid();
    getChild<LLButton>("add")->setEnabled(current && gInventory.isInventoryUsable() && !getChild<LLInventoryPanel>("inventory")->getSelectedItems().empty());
    if (!current) { closeFloater(false); return; }
    LLFloater::draw();
}
void FSFloaterGesturePicker::acceptSelection()
{
    if (!sameSession(mAccount, mSession)) return;
    std::vector<LLUUID> selected;
    for (auto* item : getChild<LLInventoryPanel>("inventory")->getSelectedItems())
        if (const auto* model = dynamic_cast<LLFolderViewModelItemInventory*>(item->getViewModelItem())) selected.push_back(model->getUUID());
    auto& controller = FSGestureBoardController::instance();
    if (mKey.has("editor_token"))
    {
        if (selected.size() != 1) { getChild<LLTextBox>("status")->setText(getString("select_one")); return; }
        auto* editor = LLFloaterReg::findTypedInstance<FSFloaterGestureTileEditor>("gesture_board_editor");
        if (editor && editor->replaceGesture(selected.front(), mKey)) closeFloater(false);
        else getChild<LLTextBox>("status")->setText(getString(controller.status()));
    }
    else if (controller.addItems(mKey["board"].asString(), selected, mExpected, mAccount, mSession))
    {
        if (!selected.empty()) if (auto* board = LLFloaterReg::findTypedInstance<FSFloaterGestureBoard>("gesture_board")) board->focusGesture(selected.front());
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
    getChild<LLCheckBoxCtrl>("custom_color")->setCommitCallback([this](LLUICtrl*, const LLSD&) { updatePreview(); });
    auto* swatch = getChild<LLColorSwatchCtrl>("color");
    swatch->setCommitCallback([this](LLUICtrl*, const LLSD&) { if (LLView::getVisible()) updatePreview(); });
    swatch->setPreviewCallback([this](LLUICtrl*, const LLSD&) { if (LLView::getVisible()) updatePreview(); });
    getChild<LLLineEditor>("label")->setKeystrokeCallback([](LLLineEditor*, void* self) { static_cast<FSFloaterGestureTileEditor*>(self)->updatePreview(); }, this);
    return LLFloater::postBuild();
}
void FSFloaterGestureTileEditor::onOpen(const LLSD& key)
{
    getChild<LLColorSwatchCtrl>("color")->closeFloaterColorPicker();
    auto& controller = FSGestureBoardController::instance(); controller.refresh(); controller.cancelPending();
    mKey = key; mExpected = key["expected"]; mAccount = gAgent.getID(); mSession = gAgent.getSessionID(); mToken = LLUUID::generateNewID();
    const auto* board = findBoard(controller.data(), key["board"].asString());
    const bool tile_mode = key["mode"].asString() == "tile";
    getChild<LLView>("tile_controls")->setVisible(tile_mode); getChild<LLView>("board_controls")->setVisible(!tile_mode);
    getChild<LLLineEditor>("board_name")->setText(LLStringExplicit(board && key["mode"].asString() != "new" ? board->name : ""));
    const auto* tile = board ? findTile(*board, key["tile"].asString()) : nullptr;
    mTile = tile ? *tile : Tile{};
    getChild<LLLineEditor>("label")->setText(LLStringExplicit(mTile.label));
    getChild<LLCheckBoxCtrl>("custom_color")->set(mTile.custom_color); getChild<LLColorSwatchCtrl>("color")->set(color(mTile));
    getChild<LLTextBox>("status")->setText(getString(tile_mode ? "editor_help" : "board_help"));
    updatePreview(); if (tile_mode && key["replace"].asBoolean()) replace();
}
void FSFloaterGestureTileEditor::onClose(bool)
{
    mToken.setNull(); getChild<LLColorSwatchCtrl>("color")->closeFloaterColorPicker(); FSGestureBoardController::instance().cancelPending();
    if (auto* picker = LLFloaterReg::findInstance("gesture_board_picker")) picker->closeFloater(false);
}
void FSFloaterGestureTileEditor::draw()
{
    if (!sameSession(mAccount, mSession)) { closeFloater(false); return; }
    LLFloater::draw();
}
void FSFloaterGestureTileEditor::replace()
{
    if (!sameSession(mAccount, mSession) || mTile.id.empty()) return;
    auto key = pickerKey(mKey["board"].asString(), mExpected); key["tile"] = mTile.id; key["editor_token"] = mToken;
    FSGestureBoardController::instance().cancelPending(); LLFloaterReg::showInstance("gesture_board_picker", key);
}
bool FSFloaterGestureTileEditor::replaceGesture(const LLUUID& reference, const LLSD& key)
{
    auto& controller = FSGestureBoardController::instance(); controller.refresh();
    if (!LLView::getVisible() || !sameSession(mAccount, mSession) || mToken.isNull() || key["editor_token"].asUUID() != mToken ||
        key["tile"].asString() != mTile.id || key["board"] != mKey["board"] || controller.snapshot() != mExpected)
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
    mTile.label = getChild<LLLineEditor>("label")->getText(); mTile.custom_color = getChild<LLCheckBoxCtrl>("custom_color")->get();
    const auto& picked = getChild<LLColorSwatchCtrl>("color")->get(); for (int i = 0; i < 3; ++i) mTile.color[i] = picked.mV[i];
    getChild<LLColorSwatchCtrl>("color")->setEnabled(mTile.custom_color);
    LLUUID item, asset; std::string name, reason;
    if (!FSGestureBoardController::instance().resolve(LLUUID(mTile.item), item, asset, name, reason)) name = getString("missing_tile");
    getChild<LLTextBox>("gesture_name")->setText(LLStringExplicit(name));
    // Recreate only the editor's sample so theme reset restores every button
    // state, rather than retaining a prior custom tint.
    auto* sample = getChild<LLView>("sample"); sample->deleteAllChildren();
    LLButton::Params p; p.name = "preview_tile"; p.label = mTile.label.empty() ? name : mTile.label; p.use_ellipses = true;
    p.rect = sample->getLocalRect(); p.tab_stop = false;
    auto* button = LLUICtrlFactory::create<LLButton>(p, sample); style(button, mTile);
}
void FSFloaterGestureTileEditor::save()
{
    if (!sameSession(mAccount, mSession)) return;
    auto& controller = FSGestureBoardController::instance(); controller.refresh(); auto data = controller.data();
    const auto mode = mKey["mode"].asString(); auto* board = findBoard(data, mKey["board"].asString());
    if (mode == "tile")
    {
        updatePreview(); auto* tile = board ? findTile(*board, mTile.id) : nullptr;
        if (!tile) { controller.setStatus("changed"); getChild<LLTextBox>("status")->setText(getString("changed")); return; }
        *tile = mTile;
    }
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
    if (controller.commit(data, mExpected, mAccount, mSession)) closeFloater(false);
    else getChild<LLTextBox>("status")->setText(getString(controller.status()));
}
