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
constexpr S32 ROW_HEIGHT = 24;
constexpr S32 MORE_WIDTH = 64;
constexpr const char* SHOW_SETTING = "FSShowWorkspaceFavoritesStrip";
}
void FSWorkspaceFavoritesBar::install(LLView* navigation)
{
    if (!navigation || navigation->findChildView("workspace_favorites_row")) return;
    auto* stack = navigation->findChild<LLLayoutStack>("navigation_favorites_bar_visibility_stack");
    if (!stack) return;
    auto* bar = new FSWorkspaceFavoritesBar();
    if (!bar->buildFromFile("panel_workspace_favorites.xml")) { delete bar; return; }
    bar->mNavigation = navigation->getHandle(); bar->mStack = stack->getHandle();
    bar->mNavigationHeight = navigation->getRect().getHeight();
    bar->mStackHeight = stack->getRect().getHeight();
    LLLayoutPanel::Params p;
    p.name = "workspace_favorites_row";
    p.rect = LLRect(0, ROW_HEIGHT, stack->getRect().getWidth(), 0);
    p.auto_resize = false; p.user_resize = false; p.min_dim = ROW_HEIGHT; p.visible = false;
    auto* container = LLUICtrlFactory::create<LLLayoutPanel>(p);
    container->addChild(bar);
    bar->setShape(container->getLocalRect());
    bar->mContainer = container->getHandle();
    stack->addPanel(container);
    bar->refresh();
    // Polling continues while hidden, so login/account/UI-mode changes and
    // favorites edited through the switcher never leave stale buttons behind.
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
    auto* navigation = mNavigation.get();
    auto* stack = static_cast<LLLayoutStack*>(mStack.get());
    auto* container = mContainer.get();
    if (!navigation || !stack || !container) return;
    auto& controller = FSWorkspaceController::instance();
    const bool ready = controller.available();
    const bool shown = ready && gSavedSettings.getBOOL(SHOW_SETTING) &&
        gViewerWindow && gViewerWindow->getUIVisibility() && !gAgentCamera.cameraMouselook();
    const bool can_switch = controller.canQuickSwitch();
    if (shown != mShown)
    {
        if (!shown) mMore->hideMenu();
        container->setVisible(shown);
        LLRect bounds = navigation->getRect();
        bounds.mBottom = bounds.mTop - mNavigationHeight - (shown ? ROW_HEIGHT : 0);
        navigation->setShape(bounds);
        bounds = stack->getRect();
        bounds.mBottom = bounds.mTop - mStackHeight - (shown ? ROW_HEIGHT : 0);
        stack->setShape(bounds);
        stack->updateLayout();
    }
    const LLSD profiles = ready ? gSavedPerAccountSettings.getLLSD("FSWorkspaceProfiles") : LLSD();
    const LLSD layouts = ready ? gSavedSettings.getLLSD("FSChromeLayoutProfiles") : LLSD();
    const LLSD favorites = ready ? gSavedPerAccountSettings.getLLSD(FSWorkspaceQuickAccess::FAVORITES_SETTING) : LLSD();
    const auto active = controller.activeId();
    const bool modified = controller.modified(), previous = controller.hasPrevious();
    if (active != mActive || modified != mModified || previous != mHasPrevious || mWidth != getRect().getWidth() || shown != mShown || can_switch != mCanSwitch ||
        gAgent.getID() != mAccount || gAgent.getSessionID() != mSession ||
        profiles != mProfiles || layouts != mLayouts || favorites != mFavorites)
    {
        mActive = active; mModified = modified; mHasPrevious = previous;
        mWidth = getRect().getWidth(); mShown = shown; mCanSwitch = can_switch;
        mAccount = gAgent.getID(); mSession = gAgent.getSessionID();
        mProfiles = profiles; mLayouts = layouts; mFavorites = favorites;
        mEntries.clear();
        const auto entries = FSWorkspaceQuickAccess::entries();
        const auto valid = FSWorkspaceQuickAccess::favorites(entries);
        for (const auto& entry : entries)
            if (valid.count(entry.key)) mEntries.push_back(entry);
        rebuild();
    }
}
void FSWorkspaceFavoritesBar::rebuild()
{
    mMore->hideMenu();
    for (LLView* child : mDynamicChildren) { removeChild(child); delete child; }
    mDynamicChildren.clear();
    const auto* font = LLFontGL::getFontSansSerifSmall();
    const S32 more_width = std::min(MORE_WIDTH, std::max(0, mWidth - 8));
    mMore->setShape(LLRect(std::max(4, mWidth - more_width - 4), ROW_HEIGHT - 2,
                         std::max(4, mWidth - 4), 2));
    S32 x = 6;
    const S32 limit = mMore->getRect().mLeft - 6;
    std::vector<FSWorkspaceQuickAccess::Entry> overflow;
    for (bool layout : {false, true})
    {
        bool labeled = false;
        for (const auto& entry : mEntries)
        {
            if (entry.layout != layout) continue;
            const auto group = getString(layout ? "layouts" : "workspaces");
            const S32 label_width = labeled ? 0 : font->getWidth(group) + 8;
            const S32 button_width = llclamp(font->getWidth(entry.label) + 20, 72, 170);
            if (x + label_width + button_width > limit)
            { overflow.push_back(entry); continue; }
            if (!labeled)
            {
                LLTextBox::Params p;
                p.name = layout ? "layout_group" : "workspace_group";
                p.rect = LLRect(x, ROW_HEIGHT - 4, x + label_width, 2);
                p.font = font; p.text = group;
                auto* label = LLUICtrlFactory::create<LLTextBox>(p, this);
                mDynamicChildren.push_back(label); x += label_width; labeled = true;
            }
            LLButton::Params p;
            const bool active = entry.key == "workspace:" + mActive;
            p.name = entry.key; p.label = (active ? "• " : "") + entry.label + (active && mModified ? " *" : ""); p.font = font; p.use_ellipses = true;
            p.rect = LLRect(x, ROW_HEIGHT - 2, x + button_width, 2);
            auto* button = LLUICtrlFactory::create<LLButton>(p, this);
            button->setEnabled(mCanSwitch);
            button->setToggleState(active);
            button->setToolTip(entry.label + (mCanSwitch ? "" : " — " + getString("preferences_open")));
            const auto key = entry.key;
            button->setCommitCallback([this, key](LLUICtrl*, const LLSD&) { switchEntry(key); });
            mDynamicChildren.push_back(button); x += button_width + 4;
        }
    }
    auto* hint = getChild<LLTextBox>("hint");
    hint->setShape(LLRect(6, ROW_HEIGHT - 4, std::max(6, limit), 2));
    hint->setText(getString(mEntries.empty() ? "empty" : "overflow_hint"));
    hint->setVisible(mDynamicChildren.empty());
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
