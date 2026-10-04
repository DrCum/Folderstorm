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
#include "fsworkspacefavoritesbar.h"
#include "fsworkspacecontroller.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llbutton.h"
#include "llcallbacklist.h"
#include "llfloaterreg.h"
#include "lllayoutstack.h"
#include "llfavoritesbar.h"
#include "llmenubutton.h"
#include "llmenugl.h"
#include "llnotificationsutil.h"
#include "lltextbox.h"
#include "lltoggleablemenu.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include <algorithm>

namespace
{
constexpr S32 MORE_WIDTH = 15;
constexpr const char* SHOW_SETTING = "FSShowWorkspaceFavoritesStrip";
}
void FSWorkspaceFavoritesBar::install(LLView* navigation)
{
    if (!navigation || navigation->findChildView("workspace_favorites_strip")) return;
    auto* container = navigation->findChild<LLLayoutPanel>("favorites_bar_visibility_panel");
    auto* landmarks = navigation->findChild<LLFavoritesBarCtrl>("favorite");
    if (!container || !landmarks) return;
    auto* bar = new FSWorkspaceFavoritesBar();
    if (!bar->buildFromFile("panel_workspace_favorites.xml")) { delete bar; return; }
    bar->mNavigation = navigation->getHandle(); bar->mContainer = container->getHandle();
    bar->mLandmarks = landmarks->getHandle();
    container->addChild(bar); bar->setVisible(false);
    bar->refresh();
    gIdleCallbacks.addFunction(idle, bar);
}
FSWorkspaceFavoritesBar::~FSWorkspaceFavoritesBar()
{
    gIdleCallbacks.deleteFunction(idle, this);
}
bool FSWorkspaceFavoritesBar::postBuild()
{
    mMore = getChild<LLMenuButton>("more");
    return LLPanel::postBuild();
}
void FSWorkspaceFavoritesBar::idle(void* userdata)
{
    auto* bar = static_cast<FSWorkspaceFavoritesBar*>(userdata);
    if (bar->mRefreshTimer.getElapsedTimeF32() < .25f) return;
    bar->mRefreshTimer.reset();
    bar->refresh();
}
void FSWorkspaceFavoritesBar::refresh()
{
    auto* navigation = mNavigation.get(); auto* container = mContainer.get();
    auto* landmarks = static_cast<LLFavoritesBarCtrl*>(mLandmarks.get());
    if (!navigation || !container || !landmarks) return;
    auto& controller = FSWorkspaceController::instance();
    const bool ready = controller.available();
    const bool ui_shown = ready && gViewerWindow && gViewerWindow->getUIVisibility() && !gAgentCamera.cameraMouselook();
    const bool shown = ui_shown && gSavedSettings.getBOOL(SHOW_SETTING);
    const bool landmarks_shown = ui_shown && gSavedSettings.getBOOL("ShowNavbarFavoritesPanel");
    const bool can_switch = controller.canQuickSwitch();
    const bool divider = shown && landmarks_shown;
    // One existing row, even when only workspace favorites are enabled.
    const bool row_visible = landmarks_shown || shown;
    if (gSavedSettings.getBOOL("FSInternalShowNavbarFavoritesPanel") != row_visible)
        gSavedSettings.setBOOL("FSInternalShowNavbarFavoritesPanel", row_visible);
    landmarks->setVisible(landmarks_shown); setVisible(shown);
    const LLSD profiles = ready ? gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles") : LLSD();
    const LLSD layouts = ready ? gSavedSettings.getLLSD("FSChromeLayoutProfiles") : LLSD();
    const LLSD favorites = ready ? gSavedPerAccountSettings.getLLSD(FSWorkspaceQuickAccess::FAVORITES_SETTING) : LLSD();
    const auto active = controller.activeId();
    const bool modified = controller.modified(), previous = controller.hasPrevious();
    const bool changed = shown != mShown || can_switch != mCanSwitch || divider != mDivider ||
        gAgent.getID() != mAccount || gAgent.getSessionID() != mSession || profiles != mProfiles ||
        layouts != mLayouts || favorites != mFavorites || active != mActive || modified != mModified || previous != mHasPrevious;
    if (changed)
    {
        mEntries.clear();
        const auto entries = FSWorkspaceQuickAccess::entries(); const auto valid = FSWorkspaceQuickAccess::favorites(entries);
        for (const auto& entry : entries) if (valid.count(entry.key)) mEntries.push_back(entry);
    }
    const auto* font = landmarks->favoriteFont();
    const S32 maximum = std::max(36, static_cast<S32>(landmarks->favoriteButtonParams().rect.width));
    S32 desired = MORE_WIDTH + 8 + (divider ? 12 : 0);
    for (const auto& entry : mEntries) desired += llclamp(font->getWidth(entry.label) + 30, 36, maximum) + 4;
    const S32 available = std::max(0, container->getRect().getWidth());
    // Keep ample room for landmarks on crowded rows; both groups retain their
    // own overflow menus. On roomy rows the divider follows the last landmark.
    const S32 reserved = std::min(desired, landmarks_shown ? std::max(MORE_WIDTH + 16, available / 2) : available);
    const S32 landmark_width = !shown ? available : landmarks_shown ? std::min(landmarks->preferredWidth(), std::max(0, available - reserved)) : 0;
    LLRect landmark_bounds = landmarks->getRect();
    landmark_bounds.mRight = landmark_bounds.mLeft + landmark_width;
    if (landmarks->getRect() != landmark_bounds) landmarks->setShape(landmark_bounds);
    const S32 width = shown ? std::min(desired, std::max(0, available - landmark_width)) : 0;
    const LLRect bounds(landmark_width, landmark_bounds.mTop, landmark_width + width, landmark_bounds.mBottom);
    const bool resized = width != mWidth || bounds != getRect();
    if (resized) setShape(bounds);
    if (changed || resized)
    {
        if (!shown) mMore->hideMenu();
        mWidth = width; mShown = shown; mCanSwitch = can_switch; mDivider = divider;
        mActive = active; mModified = modified; mHasPrevious = previous;
        mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
        mProfiles = profiles; mLayouts = layouts; mFavorites = favorites;
        rebuild();
    }
}
void FSWorkspaceFavoritesBar::rebuild()
{
    mMore->hideMenu();
    for (LLView* child : mDynamicChildren) { removeChild(child); delete child; }
    mDynamicChildren.clear();
    auto* landmarks = static_cast<LLFavoritesBarCtrl*>(mLandmarks.get());
    const auto* font = landmarks ? landmarks->favoriteFont() : LLFontGL::getFontSansSerifSmall();
    const S32 height = std::max(1, getRect().getHeight());
    const S32 more_width = std::min(MORE_WIDTH, std::max(0, mWidth - 4));
    mMore->setShape(LLRect(std::max(0, mWidth - more_width), height, mWidth, 0));
    S32 x = mDivider ? 12 : 0;
    const S32 limit = mMore->getRect().mLeft - 4;
    if (mDivider)
    {
        LLTextBox::Params p; p.name = "workspace_divider"; p.font = font; p.text = "|";
        p.rect = LLRect(2, height, 12, 0);
        mDynamicChildren.push_back(LLUICtrlFactory::create<LLTextBox>(p, this));
    }
    std::vector<FSWorkspaceQuickAccess::Entry> overflow;
    for (const auto& entry : mEntries)
    {
        LLButton::Params p;
        if (landmarks) p = landmarks->favoriteButtonParams();
        const bool active = entry.key == "workspace:" + mActive;
        const std::string label = (active ? "• " : "") + entry.label + (active && mModified ? " *" : "");
        const S32 maximum = std::max(36, static_cast<S32>(p.rect.width));
        const S32 button_width = llclamp(font->getWidth(label) + 20, 36, maximum);
        if (x + button_width > limit) { overflow.push_back(entry); continue; }
        p.name = entry.key; p.label = label; p.font = font; p.use_ellipses = true;
        p.rect = LLRect(x, height, x + button_width, 0);
        auto* button = LLUICtrlFactory::create<LLButton>(p, this);
        button->setEnabled(mCanSwitch);
        button->setToolTip(getString(entry.layout ? "layout_type" : "workspace_type") + ": " + entry.label +
            (active ? (mModified ? " — current, modified" : " — current") : "") +
            (mCanSwitch ? "" : " — " + getString("preferences_open")));
        const auto key = entry.key;
        button->setCommitCallback([this, key](LLUICtrl*, const LLSD&) { switchEntry(key); });
        mDynamicChildren.push_back(button); x += button_width + 4;
    }
    LLToggleableMenu::Params p;
    p.name = "workspace_favorites_overflow"; p.visible = false; p.can_tear_off = false;
    p.scrollable = true; p.max_scrollable_items = 12;
    auto* menu = LLUICtrlFactory::create<LLToggleableMenu>(p);
    for (const auto& entry : overflow)
    {
        LLMenuItemCallGL::Params item;
        item.name = entry.key;
        item.label = getString(entry.layout ? "layout_type" : "workspace_type") + ": " + entry.label;
        item.enabled = mCanSwitch;
        const auto key = entry.key;
        item.on_click.function([this, key](LLUICtrl*, const LLSD&) { switchEntry(key); });
        menu->append(LLUICtrlFactory::create<LLMenuItemCallGL>(item));
    }
    if (!overflow.empty())
    {
        LLMenuItemSeparatorGL::Params separator;
        menu->append(LLUICtrlFactory::create<LLMenuItemSeparatorGL>(separator));
    }
    LLMenuItemCallGL::Params previous;
    previous.name = "previous_arrangement"; previous.label = "Previous arrangement";
    previous.enabled = FSWorkspaceController::instance().hasPrevious();
    previous.on_click.function([](LLUICtrl*, const LLSD&) { FSWorkspaceController::instance().returnPrevious(); });
    menu->append(LLUICtrlFactory::create<LLMenuItemCallGL>(previous));
    LLMenuItemCallGL::Params manage;
    manage.name = "manage_workspaces"; manage.label = getString("manage");
    manage.on_click.function([](LLUICtrl*, const LLSD&) { LLFloaterReg::showInstance("workspace_switch"); });
    menu->append(LLUICtrlFactory::create<LLMenuItemCallGL>(manage));
    LLMenuItemCallGL::Params hide;
    hide.name = "hide_strip"; hide.label = getString("hide"); hide.enabled = mCanSwitch;
    hide.on_click.function([](LLUICtrl*, const LLSD&)
    {
        if (FSWorkspaceController::instance().canQuickSwitch()) gSavedSettings.setBOOL(SHOW_SETTING, false);
    });
    menu->append(LLUICtrlFactory::create<LLMenuItemCallGL>(hide));
    mMore->setMenu(menu, LLMenuButton::MP_BOTTOM_RIGHT, true);
    mMore->setToolTip(getString(mCanSwitch ? "more_hint" : "preferences_open"));
}
void FSWorkspaceFavoritesBar::switchEntry(const std::string& key)
{
    // A favorite removed while its old overflow menu is open must not switch.
    const auto entries = FSWorkspaceQuickAccess::entries();
    if (!FSWorkspaceQuickAccess::favorites(entries).count(key)) return;
    if (mAccount != gAgent.getID() || mSession != gAgent.getSessionID() ||
        !FSWorkspaceController::instance().canQuickSwitch()) return;
    if (!FSWorkspaceQuickAccess::apply(key, mAccount, mSession))
    {
        LLSD args; args["MESSAGE"] = getString("switch_failed");
        LLNotificationsUtil::add("GenericAlert", args);
    }
}
