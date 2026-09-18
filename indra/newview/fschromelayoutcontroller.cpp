/**
 * @file fschromelayoutcontroller.cpp
 * @brief Independent chrome placement layer around the custom world viewport
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (c) 2026 The Phoenix Firestorm Project, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "fschromelayoutcontroller.h"

#include "llagentcamera.h"
#include "llcontrol.h"
#include "lllayoutstack.h"
#include "llnavigationbar.h"
#include "llstatusbar.h"
#include "llfloater.h"
#include "lltoolbar.h"
#include "lltoolbarview.h"
#include "llui.h"
#include "llview.h"
#include "llviewermenu.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"

#include <cctype>

namespace
{
constexpr const char* SETTING_LEFT_PLACEMENT = "FSChromeLeftToolbarPlacement";
constexpr const char* SETTING_RIGHT_PLACEMENT = "FSChromeRightToolbarPlacement";
constexpr const char* SETTING_LEFT_OFFSET = "FSChromeLeftToolbarOffset";
constexpr const char* SETTING_RIGHT_OFFSET = "FSChromeRightToolbarOffset";
constexpr const char* SETTING_BOTTOM_REGION = "FSChromeBottomDockRegion";
constexpr const char* SETTING_BOTTOM_START = "FSChromeBottomDockSpanStart";
constexpr const char* SETTING_BOTTOM_END = "FSChromeBottomDockSpanEnd";
constexpr const char* SETTING_BOTTOM_MARGIN_LEFT = "FSChromeBottomDockMarginLeft";
constexpr const char* SETTING_BOTTOM_MARGIN_RIGHT = "FSChromeBottomDockMarginRight";
constexpr const char* SETTING_NAV_REGION = "FSChromeNavFavoritesRegion";
constexpr const char* SETTING_NAV_START = "FSChromeNavFavoritesSpanStart";
constexpr const char* SETTING_NAV_END = "FSChromeNavFavoritesSpanEnd";
constexpr const char* SETTING_NAV_MARGIN_LEFT = "FSChromeNavFavoritesMarginLeft";
constexpr const char* SETTING_NAV_MARGIN_RIGHT = "FSChromeNavFavoritesMarginRight";
constexpr const char* SETTING_MENU_REGION = "FSChromeMenuStatusRegion";
constexpr const char* SETTING_MENU_START = "FSChromeMenuStatusSpanStart";
constexpr const char* SETTING_MENU_END = "FSChromeMenuStatusSpanEnd";
constexpr const char* SETTING_MENU_MARGIN_LEFT = "FSChromeMenuStatusMarginLeft";
constexpr const char* SETTING_MENU_MARGIN_RIGHT = "FSChromeMenuStatusMarginRight";
constexpr const char* SETTING_PROFILES = "FSChromeLayoutProfiles";
constexpr const char* SETTING_ACTIVE_PROFILE = "FSChromeActiveProfile";

const char* const kSettingNames[] = {
    SETTING_LEFT_PLACEMENT,
    SETTING_RIGHT_PLACEMENT,
    SETTING_LEFT_OFFSET,
    SETTING_RIGHT_OFFSET,
    SETTING_BOTTOM_REGION,
    SETTING_BOTTOM_START,
    SETTING_BOTTOM_END,
    SETTING_BOTTOM_MARGIN_LEFT,
    SETTING_BOTTOM_MARGIN_RIGHT,
    SETTING_NAV_REGION,
    SETTING_NAV_START,
    SETTING_NAV_END,
    SETTING_NAV_MARGIN_LEFT,
    SETTING_NAV_MARGIN_RIGHT,
    SETTING_MENU_REGION,
    SETTING_MENU_START,
    SETTING_MENU_END,
    SETTING_MENU_MARGIN_LEFT,
    SETTING_MENU_MARGIN_RIGHT,
    "FSWorldViewEnabled",
    "FSWorldViewInsetLeft",
    "FSWorldViewInsetRight",
    "FSWorldViewInsetTop",
    "FSWorldViewInsetBottom",
    "UIScaleFactor"
};

FSChromeLayout::SpanRequest readSpan(const char* region_key, const char* start_key,
                                     const char* end_key, const char* left_key, const char* right_key)
{
    FSChromeLayout::SpanRequest request;
    request.region = FSChromeLayout::clampRegion(gSavedSettings.getS32(region_key));
    request.custom_start_percent = gSavedSettings.getF32(start_key);
    request.custom_end_percent = gSavedSettings.getF32(end_key);
    request.margin_left = gSavedSettings.getS32(left_key);
    request.margin_right = gSavedSettings.getS32(right_key);
    return request;
}

void writeSpan(const FSChromeLayout::SpanRequest& request, const char* region_key,
               const char* start_key, const char* end_key, const char* left_key, const char* right_key)
{
    gSavedSettings.setS32(region_key, static_cast<S32>(request.region));
    gSavedSettings.setF32(start_key, request.custom_start_percent);
    gSavedSettings.setF32(end_key, request.custom_end_percent);
    gSavedSettings.setS32(left_key, request.margin_left);
    gSavedSettings.setS32(right_key, request.margin_right);
}

LLSD spanToLLSD(const FSChromeLayout::SpanRequest& request)
{
    LLSD data;
    data["region"] = static_cast<S32>(request.region);
    data["start"] = request.custom_start_percent;
    data["end"] = request.custom_end_percent;
    data["margin_left"] = request.margin_left;
    data["margin_right"] = request.margin_right;
    return data;
}

FSChromeLayout::SpanRequest spanFromLLSD(const LLSD& data)
{
    FSChromeLayout::SpanRequest request;
    if (data.has("region"))
    {
        request.region = FSChromeLayout::clampRegion(data["region"].asInteger());
    }
    if (data.has("start"))
    {
        request.custom_start_percent = static_cast<F32>(data["start"].asReal());
    }
    if (data.has("end"))
    {
        request.custom_end_percent = static_cast<F32>(data["end"].asReal());
    }
    if (data.has("margin_left"))
    {
        request.margin_left = data["margin_left"].asInteger();
    }
    if (data.has("margin_right"))
    {
        request.margin_right = data["margin_right"].asInteger();
    }
    return request;
}

} // namespace

FSChromeLayoutController::FSChromeLayoutController() = default;

FSChromeLayoutController::~FSChromeLayoutController()
{
    shutdown();
}

void FSChromeLayoutController::init()
{
    if (mInitialized)
    {
        return;
    }

    mShuttingDown = false;
    connectSignals();
    mInitialized = true;
    apply();
}

void FSChromeLayoutController::shutdown()
{
    mShuttingDown = true;
    disconnectSignals();
    mInitialized = false;
}

void FSChromeLayoutController::connectSignals()
{
    disconnectSignals();

    if (gViewerWindow)
    {
        mConnections.push_back(gViewerWindow->setOnWorldViewRectUpdated(
            boost::bind(&FSChromeLayoutController::onWorldViewRectUpdated, this, _1, _2)));
    }

    for (const char* name : kSettingNames)
    {
        LLControlVariable* control = gSavedSettings.getControl(name);
        if (!control)
        {
            LL_WARNS("FSChromeLayout") << "Missing setting " << name << LL_ENDL;
            continue;
        }
        mConnections.push_back(control->getSignal()->connect(
            boost::bind(&FSChromeLayoutController::onSettingChanged, this)));
    }
}

void FSChromeLayoutController::disconnectSignals()
{
    for (boost::signals2::connection& connection : mConnections)
    {
        connection.disconnect();
    }
    mConnections.clear();
}

void FSChromeLayoutController::onSettingChanged()
{
    if (mApplying || mShuttingDown)
    {
        return;
    }
    apply();
}

void FSChromeLayoutController::onWorldViewRectUpdated(const LLRect&, const LLRect&)
{
    apply();
}

void FSChromeLayoutController::requestApply()
{
    apply();
}

void FSChromeLayoutController::notifyToolbarMetrics(int left_width, int right_width)
{
    if (!isReady())
    {
        return;
    }
    if (left_width == mLastLeftToolbarWidth && right_width == mLastRightToolbarWidth)
    {
        return;
    }
    apply();
}

bool FSChromeLayoutController::isLeftToolbarHidden() const
{
    return FSChromeLayout::clampSidePlacement(gSavedSettings.getS32(SETTING_LEFT_PLACEMENT)) ==
           FSChromeLayout::SidePlacement::Hidden;
}

bool FSChromeLayoutController::isRightToolbarHidden() const
{
    return FSChromeLayout::clampSidePlacement(gSavedSettings.getS32(SETTING_RIGHT_PLACEMENT)) ==
           FSChromeLayout::SidePlacement::Hidden;
}

FSChromeLayout::Rect FSChromeLayoutController::toLayoutRect(const LLRect& rect) const
{
    FSChromeLayout::Rect result;
    result.left = rect.mLeft;
    result.bottom = rect.mBottom;
    result.right = rect.mRight;
    result.top = rect.mTop;
    return result;
}

void FSChromeLayoutController::readRequestsFromSettings()
{
    mLeftRequest.placement = FSChromeLayout::clampSidePlacement(gSavedSettings.getS32(SETTING_LEFT_PLACEMENT));
    mRightRequest.placement = FSChromeLayout::clampSidePlacement(gSavedSettings.getS32(SETTING_RIGHT_PLACEMENT));
    mLeftRequest.offset = gSavedSettings.getS32(SETTING_LEFT_OFFSET);
    mRightRequest.offset = gSavedSettings.getS32(SETTING_RIGHT_OFFSET);
    mBottomRequest = readSpan(SETTING_BOTTOM_REGION, SETTING_BOTTOM_START, SETTING_BOTTOM_END,
                              SETTING_BOTTOM_MARGIN_LEFT, SETTING_BOTTOM_MARGIN_RIGHT);
    mNavRequest = readSpan(SETTING_NAV_REGION, SETTING_NAV_START, SETTING_NAV_END,
                           SETTING_NAV_MARGIN_LEFT, SETTING_NAV_MARGIN_RIGHT);
    mMenuRequest = readSpan(SETTING_MENU_REGION, SETTING_MENU_START, SETTING_MENU_END,
                            SETTING_MENU_MARGIN_LEFT, SETTING_MENU_MARGIN_RIGHT);
}

void FSChromeLayoutController::apply()
{
    if (!mInitialized || mShuttingDown || mApplying || !gViewerWindow)
    {
        return;
    }

    mApplying = true;

    mWindow = toLayoutRect(gViewerWindow->getWindowRectScaled());
    mWorld = toLayoutRect(gViewerWindow->getWorldViewRectScaled());
    readRequestsFromSettings();

    if (gToolBarView)
    {
        mLeftRequest.toolbar_width =
            toolbarLayoutWidth(gToolBarView->getToolbar(LLToolBarEnums::TOOLBAR_LEFT));
        mRightRequest.toolbar_width =
            toolbarLayoutWidth(gToolBarView->getToolbar(LLToolBarEnums::TOOLBAR_RIGHT));
    }

    mLastLeftToolbarWidth = mLeftRequest.toolbar_width;
    mLastRightToolbarWidth = mRightRequest.toolbar_width;
    mLastSideLayout = FSChromeLayout::computeSideLayout(mWindow, mWorld, mLeftRequest, mRightRequest);
    mLastBottomSpan = FSChromeLayout::computeHorizontalSpan(mWindow, mWorld, mBottomRequest,
                                                            FSChromeLayout::kMinInteractiveWidth);
    mLastNavSpan = FSChromeLayout::computeHorizontalSpan(mWindow, mWorld, mNavRequest,
                                                         FSChromeLayout::kMinInteractiveWidth);
    mLastMenuSpan = FSChromeLayout::computeHorizontalSpan(mWindow, mWorld, mMenuRequest,
                                                          FSChromeLayout::kMinInteractiveWidth);

    if (mLastBottomSpan.used_fallback)
    {
        LL_DEBUGS("FSChromeLayout") << FSChromeLayout::describeFallback("bottom dock", mBottomRequest.region,
                                                                        mLastBottomSpan.resolved_region) << LL_ENDL;
    }
    if (mLastNavSpan.used_fallback)
    {
        LL_DEBUGS("FSChromeLayout") << FSChromeLayout::describeFallback("navigation", mNavRequest.region,
                                                                        mLastNavSpan.resolved_region) << LL_ENDL;
    }
    if (mLastMenuSpan.used_fallback)
    {
        LL_DEBUGS("FSChromeLayout") << FSChromeLayout::describeFallback("menu/status", mMenuRequest.region,
                                                                        mLastMenuSpan.resolved_region) << LL_ENDL;
    }

    applySideToolbars();
    applyBottomDock();
    applyNavigation();
    applyMenuStatus();
    applyFloaterSnapView();

    mApplying = false;
}

void FSChromeLayoutController::applySideToolbars()
{
    if (!gToolBarView)
    {
        return;
    }

    LLView* stack = gToolBarView->findChildView("vertical_toolbar_stack");
    LLLayoutPanel* left_spacer = findLayoutPanel(gToolBarView, "left_toolbar_outer_spacer");
    LLLayoutPanel* right_spacer = findLayoutPanel(gToolBarView, "right_toolbar_outer_spacer");
    if (!stack || !left_spacer || !right_spacer)
    {
        return;
    }

    const FSChromeLayout::Rect holder = toLayoutRect(stack->calcScreenRect());
    const FSChromeLayout::SideLayout layout =
        FSChromeLayout::computeSideLayout(holder, mWorld, mLeftRequest, mRightRequest);
    mLastSideLayout = layout;

    if (layout.left_used_fallback)
    {
        LL_DEBUGS("FSChromeLayout") << "Left toolbar outside placement did not fit the gutter; clamped"
                                    << LL_ENDL;
    }
    if (layout.right_used_fallback)
    {
        LL_DEBUGS("FSChromeLayout") << "Right toolbar outside placement did not fit the gutter; clamped"
                                    << LL_ENDL;
    }

    setSpacerWidth(left_spacer, layout.left_outer_spacer);
    setSpacerWidth(right_spacer, layout.right_outer_spacer);
}

void FSChromeLayoutController::applyBottomDock()
{
    if (!gToolBarView)
    {
        return;
    }

    LLView* stack = gToolBarView->findChildView("bottom_dock_region_stack");
    LLLayoutPanel* left_spacer = findLayoutPanel(gToolBarView, "bottom_dock_left_spacer");
    LLLayoutPanel* right_spacer = findLayoutPanel(gToolBarView, "bottom_dock_right_spacer");
    if (!stack || !left_spacer || !right_spacer)
    {
        return;
    }

    const LLRect holder = stack->calcScreenRect();
    const int left = llclamp(mLastBottomSpan.left, holder.mLeft, holder.mRight);
    const int right = llclamp(mLastBottomSpan.right, left, holder.mRight);
    setSpacerWidth(left_spacer, std::max(0, left - holder.mLeft));
    setSpacerWidth(right_spacer, std::max(0, holder.mRight - right));
}

void FSChromeLayoutController::applyNavigation()
{
    if (!LLNavigationBar::instanceExists())
    {
        return;
    }

    LLView* nav = LLNavigationBar::instance().getView();
    if (!nav || !nav->getParent())
    {
        return;
    }

    applySpanToView(nav, mLastNavSpan, FOLLOWS_LEFT | FOLLOWS_TOP);
}

void FSChromeLayoutController::applyMenuStatus()
{
    if (gStatusBar && gStatusBar->getParent())
    {
        applySpanToView(gStatusBar, mLastMenuSpan, FOLLOWS_LEFT | FOLLOWS_TOP | FOLLOWS_BOTTOM);
    }
    if (gMenuBarView && gMenuBarView->getParent())
    {
        applyMenuBarToSpan(gMenuBarView, mLastMenuSpan);
    }
}

void FSChromeLayoutController::applySpanToView(LLView* view, const FSChromeLayout::Span& span, U32 follows)
{
    if (!view)
    {
        return;
    }
    LLView* parent = view->getParent();
    if (!parent)
    {
        return;
    }

    const LLRect parent_screen = parent->calcScreenRect();
    LLRect local = view->getRect();
    local.mLeft = span.left - parent_screen.mLeft;
    local.mRight = span.right - parent_screen.mLeft;
    local.mLeft = llclamp(local.mLeft, 0, parent->getRect().getWidth());
    local.mRight = llclamp(local.mRight, local.mLeft + 1, parent->getRect().getWidth());
    if (local.getWidth() <= 0)
    {
        return;
    }

    if (view->getRect() == local && view->getFollows() == follows)
    {
        return;
    }

    view->setFollows(follows);
    view->setShape(local);
}

void FSChromeLayoutController::applyMenuBarToSpan(LLView* menu, const FSChromeLayout::Span& span)
{
    if (!menu)
    {
        return;
    }
    LLView* parent = menu->getParent();
    if (!parent)
    {
        return;
    }

    const LLRect parent_screen = parent->calcScreenRect();
    const S32 parent_width = parent->getRect().getWidth();
    const S32 span_left = llclamp(span.left - parent_screen.mLeft, 0, parent_width);
    const S32 span_right = llclamp(span.right - parent_screen.mLeft, span_left + 1, parent_width);

    S32 content_width = menu->getRect().getWidth();
    if (gMenuBarView)
    {
        content_width = std::max(content_width, gMenuBarView->getRightmostMenuEdge());
    }
    content_width = llclamp(content_width, 1, std::max(1, span_right - span_left));

    LLRect local = menu->getRect();
    local.mLeft = span_left;
    local.mRight = span_left + content_width;
    if (menu->getRect() == local && menu->getFollows() == (FOLLOWS_LEFT | FOLLOWS_TOP))
    {
        return;
    }

    menu->setFollows(FOLLOWS_LEFT | FOLLOWS_TOP);
    menu->setShape(local);
}

void FSChromeLayoutController::applyFloaterSnapView()
{
    if (!gFloaterView || !gToolBarView)
    {
        return;
    }

    // Toasts and chiclets still parent to floater_snap_region (the squeezed
    // non-toolbar panel). Floater edge-snap and relative positioning must
    // include utility gutters so windows can live on the other monitor.
    LLView* snap = gToolBarView->findChildView("floater_snap_region");
    const bool expand_for_gutters =
        gSavedSettings.getBOOL("FSWorldViewEnabled") &&
        (mLastSideLayout.left_outer_spacer > 0 || mLastSideLayout.right_outer_spacer > 0);
    if (expand_for_gutters)
    {
        if (LLView* stack = gToolBarView->findChildView("vertical_toolbar_stack"))
        {
            snap = stack;
        }
    }
    if (snap)
    {
        gFloaterView->setFloaterSnapView(snap->getHandle());
    }
}

int FSChromeLayoutController::toolbarLayoutWidth(LLToolBar* toolbar) const
{
    if (!toolbar)
    {
        return 0;
    }
    LLView* parent = toolbar->getParent();
    if (!toolbar->hasButtons() && parent && !parent->getVisible())
    {
        return 0;
    }
    return toolbar->getRect().getWidth();
}

void FSChromeLayoutController::setSpacerWidth(LLLayoutPanel* panel, int width)
{
    if (!panel)
    {
        return;
    }
    const int target = std::max(0, width);
    if (panel->getTargetDim() != target)
    {
        panel->setTargetDim(target);
    }
}

LLLayoutPanel* FSChromeLayoutController::findLayoutPanel(LLView* root, const char* name) const
{
    if (!root)
    {
        return nullptr;
    }
    return root->findChild<LLLayoutPanel>(name);
}

FSChromeLayout::Snapshot FSChromeLayoutController::captureSnapshot() const
{
    FSChromeLayout::Snapshot snapshot;
    snapshot.viewport_enabled = gSavedSettings.getBOOL("FSWorldViewEnabled");
    snapshot.inset_left = gSavedSettings.getF32("FSWorldViewInsetLeft");
    snapshot.inset_right = gSavedSettings.getF32("FSWorldViewInsetRight");
    snapshot.inset_top = gSavedSettings.getF32("FSWorldViewInsetTop");
    snapshot.inset_bottom = gSavedSettings.getF32("FSWorldViewInsetBottom");
    snapshot.left_placement = FSChromeLayout::clampSidePlacement(gSavedSettings.getS32(SETTING_LEFT_PLACEMENT));
    snapshot.right_placement = FSChromeLayout::clampSidePlacement(gSavedSettings.getS32(SETTING_RIGHT_PLACEMENT));
    snapshot.left_offset = gSavedSettings.getS32(SETTING_LEFT_OFFSET);
    snapshot.right_offset = gSavedSettings.getS32(SETTING_RIGHT_OFFSET);
    snapshot.bottom_dock = readSpan(SETTING_BOTTOM_REGION, SETTING_BOTTOM_START, SETTING_BOTTOM_END,
                                    SETTING_BOTTOM_MARGIN_LEFT, SETTING_BOTTOM_MARGIN_RIGHT);
    snapshot.nav_favorites = readSpan(SETTING_NAV_REGION, SETTING_NAV_START, SETTING_NAV_END,
                                      SETTING_NAV_MARGIN_LEFT, SETTING_NAV_MARGIN_RIGHT);
    snapshot.menu_status = readSpan(SETTING_MENU_REGION, SETTING_MENU_START, SETTING_MENU_END,
                                    SETTING_MENU_MARGIN_LEFT, SETTING_MENU_MARGIN_RIGHT);
    return FSChromeLayout::sanitizedSnapshot(snapshot);
}

void FSChromeLayoutController::applySnapshot(const FSChromeLayout::Snapshot& snapshot)
{
    const FSChromeLayout::Snapshot clean = FSChromeLayout::sanitizedSnapshot(snapshot);
    mApplying = true;
    gSavedSettings.setBOOL("FSWorldViewEnabled", clean.viewport_enabled);
    gSavedSettings.setF32("FSWorldViewInsetLeft", clean.inset_left);
    gSavedSettings.setF32("FSWorldViewInsetRight", clean.inset_right);
    gSavedSettings.setF32("FSWorldViewInsetTop", clean.inset_top);
    gSavedSettings.setF32("FSWorldViewInsetBottom", clean.inset_bottom);
    gSavedSettings.setS32(SETTING_LEFT_PLACEMENT, static_cast<S32>(clean.left_placement));
    gSavedSettings.setS32(SETTING_RIGHT_PLACEMENT, static_cast<S32>(clean.right_placement));
    gSavedSettings.setS32(SETTING_LEFT_OFFSET, clean.left_offset);
    gSavedSettings.setS32(SETTING_RIGHT_OFFSET, clean.right_offset);
    writeSpan(clean.bottom_dock, SETTING_BOTTOM_REGION, SETTING_BOTTOM_START, SETTING_BOTTOM_END,
              SETTING_BOTTOM_MARGIN_LEFT, SETTING_BOTTOM_MARGIN_RIGHT);
    writeSpan(clean.nav_favorites, SETTING_NAV_REGION, SETTING_NAV_START, SETTING_NAV_END,
              SETTING_NAV_MARGIN_LEFT, SETTING_NAV_MARGIN_RIGHT);
    writeSpan(clean.menu_status, SETTING_MENU_REGION, SETTING_MENU_START, SETTING_MENU_END,
              SETTING_MENU_MARGIN_LEFT, SETTING_MENU_MARGIN_RIGHT);
    if (gViewerWindow)
    {
        gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
    }
    mApplying = false;
    apply();
}

std::vector<std::string> FSChromeLayoutController::profileNames() const
{
    std::vector<std::string> names = FSChromeLayout::builtinProfileIds();
    const LLSD profiles = gSavedSettings.getLLSD(SETTING_PROFILES);
    if (profiles.isMap())
    {
        for (LLSD::map_const_iterator it = profiles.beginMap(); it != profiles.endMap(); ++it)
        {
            if (!it->first.empty() && !FSChromeLayout::isBuiltinProfileId(it->first))
            {
                names.push_back(it->first);
            }
        }
    }
    return names;
}

bool FSChromeLayoutController::applyProfile(const std::string& id)
{
    if (FSChromeLayout::isBuiltinProfileId(id))
    {
        applySnapshot(FSChromeLayout::builtinSnapshot(id));
        gSavedSettings.setString(SETTING_ACTIVE_PROFILE, id);
        return true;
    }

    const LLSD profiles = gSavedSettings.getLLSD(SETTING_PROFILES);
    if (!profiles.isMap() || !profiles.has(id))
    {
        LL_WARNS("FSChromeLayout") << "Unknown layout profile " << id << LL_ENDL;
        return false;
    }

    applySnapshot(snapshotFromLLSD(profiles[id]));
    gSavedSettings.setString(SETTING_ACTIVE_PROFILE, id);
    return true;
}

bool FSChromeLayoutController::saveUserProfile(const std::string& name)
{
    if (!isSafeProfileName(name) || FSChromeLayout::isBuiltinProfileId(name))
    {
        return false;
    }

    LLSD profiles = gSavedSettings.getLLSD(SETTING_PROFILES);
    if (!profiles.isMap())
    {
        profiles = LLSD::emptyMap();
    }
    profiles[name] = snapshotToLLSD(captureSnapshot());
    gSavedSettings.setLLSD(SETTING_PROFILES, profiles);
    gSavedSettings.setString(SETTING_ACTIVE_PROFILE, name);
    return true;
}

bool FSChromeLayoutController::renameUserProfile(const std::string& old_name, const std::string& new_name)
{
    if (!isUserProfile(old_name) || !isSafeProfileName(new_name) || FSChromeLayout::isBuiltinProfileId(new_name))
    {
        return false;
    }

    LLSD profiles = gSavedSettings.getLLSD(SETTING_PROFILES);
    if (!profiles.isMap() || !profiles.has(old_name))
    {
        return false;
    }
    if (profiles.has(new_name))
    {
        return false;
    }

    profiles[new_name] = profiles[old_name];
    profiles.erase(old_name);
    gSavedSettings.setLLSD(SETTING_PROFILES, profiles);
    if (gSavedSettings.getString(SETTING_ACTIVE_PROFILE) == old_name)
    {
        gSavedSettings.setString(SETTING_ACTIVE_PROFILE, new_name);
    }
    return true;
}

bool FSChromeLayoutController::deleteUserProfile(const std::string& name)
{
    if (!isUserProfile(name))
    {
        return false;
    }

    LLSD profiles = gSavedSettings.getLLSD(SETTING_PROFILES);
    if (!profiles.isMap())
    {
        return false;
    }
    profiles.erase(name);
    gSavedSettings.setLLSD(SETTING_PROFILES, profiles);
    if (gSavedSettings.getString(SETTING_ACTIVE_PROFILE) == name)
    {
        gSavedSettings.setString(SETTING_ACTIVE_PROFILE, "standard_window");
    }
    return true;
}

void FSChromeLayoutController::resetCurrentLayout()
{
    applyProfile("standard_window");
}

bool FSChromeLayoutController::isUserProfile(const std::string& id) const
{
    if (id.empty() || FSChromeLayout::isBuiltinProfileId(id))
    {
        return false;
    }
    const LLSD profiles = gSavedSettings.getLLSD(SETTING_PROFILES);
    return profiles.isMap() && profiles.has(id);
}

std::string FSChromeLayoutController::activeProfileId() const
{
    return gSavedSettings.getString(SETTING_ACTIVE_PROFILE);
}

LLSD FSChromeLayoutController::snapshotToLLSD(const FSChromeLayout::Snapshot& snapshot)
{
    const FSChromeLayout::Snapshot clean = FSChromeLayout::sanitizedSnapshot(snapshot);
    LLSD data;
    data["schema"] = clean.schema_version;
    data["viewport_enabled"] = clean.viewport_enabled;
    data["inset_left"] = clean.inset_left;
    data["inset_right"] = clean.inset_right;
    data["inset_top"] = clean.inset_top;
    data["inset_bottom"] = clean.inset_bottom;
    data["left_placement"] = static_cast<S32>(clean.left_placement);
    data["right_placement"] = static_cast<S32>(clean.right_placement);
    data["left_offset"] = clean.left_offset;
    data["right_offset"] = clean.right_offset;
    data["bottom_dock"] = spanToLLSD(clean.bottom_dock);
    data["nav_favorites"] = spanToLLSD(clean.nav_favorites);
    data["menu_status"] = spanToLLSD(clean.menu_status);
    return data;
}

FSChromeLayout::Snapshot FSChromeLayoutController::snapshotFromLLSD(const LLSD& data)
{
    FSChromeLayout::Snapshot snapshot;
    if (!data.isMap())
    {
        return FSChromeLayout::sanitizedSnapshot(snapshot);
    }
    snapshot.viewport_enabled = data["viewport_enabled"].asBoolean();
    snapshot.inset_left = static_cast<F32>(data["inset_left"].asReal());
    snapshot.inset_right = static_cast<F32>(data["inset_right"].asReal());
    snapshot.inset_top = static_cast<F32>(data["inset_top"].asReal());
    snapshot.inset_bottom = static_cast<F32>(data["inset_bottom"].asReal());
    snapshot.left_placement = FSChromeLayout::clampSidePlacement(data["left_placement"].asInteger());
    snapshot.right_placement = FSChromeLayout::clampSidePlacement(data["right_placement"].asInteger());
    snapshot.left_offset = data["left_offset"].asInteger();
    snapshot.right_offset = data["right_offset"].asInteger();
    snapshot.bottom_dock = spanFromLLSD(data["bottom_dock"]);
    snapshot.nav_favorites = spanFromLLSD(data["nav_favorites"]);
    snapshot.menu_status = spanFromLLSD(data["menu_status"]);
    return FSChromeLayout::sanitizedSnapshot(snapshot);
}

bool FSChromeLayoutController::isSafeProfileName(const std::string& name)
{
    if (name.empty() || name.size() > 64)
    {
        return false;
    }
    for (unsigned char ch : name)
    {
        if (!(std::isalnum(ch) || ch == ' ' || ch == '-' || ch == '_' || ch == '.'))
        {
            return false;
        }
    }
    return true;
}
