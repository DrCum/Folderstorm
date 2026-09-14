/**
 * @file fschromelayoutcontroller.h
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

#ifndef FSCHROMELAYOUTCONTROLLER_H
#define FSCHROMELAYOUTCONTROLLER_H

#include "fschromelayout.h"
#include "llrect.h"
#include "llsingleton.h"
#include "llsd.h"
#include "stdtypes.h"

#include <boost/signals2.hpp>
#include <string>
#include <vector>

class LLLayoutPanel;
class LLLayoutStack;
class LLToolBar;
class LLView;

class FSChromeLayoutController : public LLSingleton<FSChromeLayoutController>
{
    LLSINGLETON(FSChromeLayoutController);
    ~FSChromeLayoutController();

public:
    void init();
    void shutdown();
    bool isReady() const { return mInitialized && !mShuttingDown; }

    void apply();
    void requestApply();
    void notifyToolbarMetrics(int left_width, int right_width);

    bool isLeftToolbarHidden() const;
    bool isRightToolbarHidden() const;

    FSChromeLayout::Snapshot captureSnapshot() const;
    void applySnapshot(const FSChromeLayout::Snapshot& snapshot);

    std::vector<std::string> profileNames() const;
    bool applyProfile(const std::string& id);
    bool saveUserProfile(const std::string& name);
    bool renameUserProfile(const std::string& old_name, const std::string& new_name);
    bool deleteUserProfile(const std::string& name);
    void resetCurrentLayout();
    bool isUserProfile(const std::string& id) const;
    std::string activeProfileId() const;

    static LLSD snapshotToLLSD(const FSChromeLayout::Snapshot& snapshot);
    static FSChromeLayout::Snapshot snapshotFromLLSD(const LLSD& data);
    static bool isSafeProfileName(const std::string& name);

private:
    void connectSignals();
    void disconnectSignals();
    void onSettingChanged();
    void onWorldViewRectUpdated(const LLRect& old_rect, const LLRect& new_rect);

    FSChromeLayout::Rect toLayoutRect(const LLRect& rect) const;
    void readRequestsFromSettings();
    void applySideToolbars();
    void applyBottomDock();
    void applyNavigation();
    void applyMenuStatus();
    void applySpanToView(LLView* view, const FSChromeLayout::Span& span, U32 follows);
    void applyMenuBarToSpan(LLView* menu, const FSChromeLayout::Span& span);
    void setSpacerWidth(LLLayoutPanel* panel, int width);
    int toolbarLayoutWidth(LLToolBar* toolbar) const;
    LLLayoutPanel* findLayoutPanel(LLView* root, const char* name) const;

    bool mInitialized = false;
    bool mShuttingDown = false;
    bool mApplying = false;
    int mLastLeftToolbarWidth = -1;
    int mLastRightToolbarWidth = -1;

    FSChromeLayout::Rect mWindow;
    FSChromeLayout::Rect mWorld;
    FSChromeLayout::SideRequest mLeftRequest;
    FSChromeLayout::SideRequest mRightRequest;
    FSChromeLayout::SpanRequest mBottomRequest;
    FSChromeLayout::SpanRequest mNavRequest;
    FSChromeLayout::SpanRequest mMenuRequest;
    FSChromeLayout::SideLayout mLastSideLayout;
    FSChromeLayout::Span mLastBottomSpan;
    FSChromeLayout::Span mLastNavSpan;
    FSChromeLayout::Span mLastMenuSpan;

    std::vector<boost::signals2::connection> mConnections;
};

#endif
