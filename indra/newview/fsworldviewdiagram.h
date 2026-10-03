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

#ifndef FS_WORLD_VIEW_DIAGRAM_H
#define FS_WORLD_VIEW_DIAGRAM_H

#include "lluictrl.h"
#include "lltimer.h"
#include "llwindow.h"
#include "fsworldviewgeometry.h"
#include <array>

// Edits the existing viewport inset settings. No monitor identity is persisted.
class FSWorldViewDiagram : public LLUICtrl
{
public:
    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params> {};
    explicit FSWorldViewDiagram(const Params& params);
    ~FSWorldViewDiagram() override;
    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleKeyHere(KEY key, MASK mask) override;
    void onMouseCaptureLost() override;
    void onVisibilityChange(bool visible) override;
    bool fitSelectedMonitor();
    void selectMonitor(const LLSD& choice);
    void cancelDrag();

private:
    enum Edge { NONE, LEFT, RIGHT, TOP, BOTTOM };
    void refreshGeometry();
    void updateControls();
    LLRect diagramRect(const LLRect& raw) const;
    FSWorldViewGeometry::Insets readInsets() const;
    void writeInsets(const FSWorldViewGeometry::Insets& insets, bool enabled = true);
    std::string text(const std::string& key) const;
    LLSD monitorChoice(const std::string& id) const;

    LLTimer mRefreshTimer;
    LLRect mClient, mBase;
    std::vector<LLWindow::MonitorRect> mMonitors;
    std::string mSelectedMonitor;
    bool mMonitorGeometryAvailable = false;
    bool mGeometryInitialized = false;
    bool mNeedsMonitorSelection = false;
    U32 mMonitorChoiceRevision = 0;
    Edge mEdge = NONE;
    FSWorldViewGeometry::Insets mDragStart;
    std::array<F32, 4> mDragStartPercent{};
    bool mDragStartEnabled = false;
    S32 mDragStartX = 0, mDragStartY = 0;
    bool mDragMoved = false;
};
#endif
