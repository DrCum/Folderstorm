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
#include "fsworldviewdiagram.h"

#include "llagentcamera.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llpanel.h"
#include "llrender.h"
#include "llrender2dutils.h"
#include "llsdutil.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "fschromelayoutcontroller.h"

#include <algorithm>

static LLDefaultChildRegistry::Register<FSWorldViewDiagram> world_view_diagram("world_view_diagram");

namespace
{
FSWorldViewGeometry::Rect geometry(const LLRect& rect)
{
    return {rect.mLeft, rect.mBottom, rect.mRight, rect.mTop};
}
LLRect uiRect(const FSWorldViewGeometry::Rect& rect)
{
    return LLRect(rect.left, rect.top, rect.right, rect.bottom);
}
LLRect clipped(const LLRect& a, const LLRect& b)
{
    return uiRect(FSWorldViewGeometry::intersection(geometry(a), geometry(b)));
}
bool hasArea(const LLRect& rect) { return rect.getWidth() > 0 && rect.getHeight() > 0; }
}

FSWorldViewDiagram::FSWorldViewDiagram(const Params& params) : LLUICtrl(params) {}
FSWorldViewDiagram::~FSWorldViewDiagram()
{
    // Preferences owns rollback; destruction must not restore stale settings.
    mEdge = NONE;
    if (hasMouseCapture()) gFocusMgr.setMouseCapture(nullptr);
}

std::string FSWorldViewDiagram::text(const std::string& key) const
{
    const LLPanel* panel = dynamic_cast<const LLPanel*>(getParent());
    return panel ? panel->getString(key) : std::string();
}

FSWorldViewGeometry::Insets FSWorldViewDiagram::readInsets() const
{
    return {gSavedSettings.getF32("FSWorldViewInsetLeft") * .01f,
            gSavedSettings.getF32("FSWorldViewInsetRight") * .01f,
            gSavedSettings.getF32("FSWorldViewInsetTop") * .01f,
            gSavedSettings.getF32("FSWorldViewInsetBottom") * .01f};
}

void FSWorldViewDiagram::writeInsets(const FSWorldViewGeometry::Insets& insets, bool enabled)
{
    // The controller batches settings and updates world/chrome rectangles once.
    if (FSChromeLayoutController::instanceExists())
    {
        FSChromeLayoutController::instance().applyViewportInsets(enabled, insets.left * 100.f,
            insets.right * 100.f, insets.top * 100.f, insets.bottom * 100.f);
    }
    else
    {
        gSavedSettings.setF32("FSWorldViewInsetLeft", insets.left * 100.f);
        gSavedSettings.setF32("FSWorldViewInsetRight", insets.right * 100.f);
        gSavedSettings.setF32("FSWorldViewInsetTop", insets.top * 100.f);
        gSavedSettings.setF32("FSWorldViewInsetBottom", insets.bottom * 100.f);
        gSavedSettings.setBOOL("FSWorldViewEnabled", enabled);
        if (gViewerWindow) gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
    }
    updateControls();
}

void FSWorldViewDiagram::refreshGeometry()
{
    if (!gViewerWindow) return;
    const LLRect client = gViewerWindow->getWindowRectRaw();
    const LLRect base = gViewerWindow->getWorldViewBaseRectRaw();
    std::vector<LLWindow::MonitorRect> monitors;
    const bool available = gViewerWindow->getWindow()->getMonitorRectsInClient(monitors);
    bool topology_changed = available != mMonitorGeometryAvailable || monitors.size() != mMonitors.size();
    if (!topology_changed)
    {
        for (size_t i = 0; i < monitors.size(); ++i)
        {
            if (monitors[i].id != mMonitors[i].id || monitors[i].name != mMonitors[i].name ||
                !FSWorldViewGeometry::sameRelativeMonitorRect(geometry(monitors[i].rect), geometry(monitors.front().rect),
                    geometry(mMonitors[i].rect), geometry(mMonitors.front().rect)))
            {
                topology_changed = true;
                break;
            }
        }
    }
    if (topology_changed && !mSelectedMonitor.empty())
    {
        mSelectedMonitor.clear();
        mNeedsMonitorSelection = true;
    }
    bool choices_changed = !mGeometryInitialized || topology_changed;
    if (!choices_changed)
    {
        for (size_t i = 0; i < monitors.size(); ++i)
        {
            if (monitors[i].id != mMonitors[i].id ||
                hasArea(clipped(monitors[i].rect, client)) != hasArea(clipped(mMonitors[i].rect, mClient)))
            {
                choices_changed = true;
                break;
            }
        }
    }
    bool changed = client != mClient || base != mBase || available != mMonitorGeometryAvailable ||
                   monitors.size() != mMonitors.size();
    if (!changed)
    {
        for (size_t i = 0; i < monitors.size(); ++i)
        {
            if (monitors[i].id != mMonitors[i].id || monitors[i].rect != mMonitors[i].rect)
            {
                changed = true;
                break;
            }
        }
    }
    if (changed && mEdge != NONE) cancelDrag();
    mClient = client;
    mBase = base;
    mMonitors.swap(monitors);
    mMonitorGeometryAvailable = available;
    if (mSelectedMonitor.empty() && !mNeedsMonitorSelection)
    {
        const auto world = FSWorldViewGeometry::apply(geometry(mBase),
            gSavedSettings.getBOOL("FSWorldViewEnabled") ? readInsets() : FSWorldViewGeometry::Insets{});
        int64_t largest = 0;
        for (const auto& monitor : mMonitors)
        {
            const auto overlap = FSWorldViewGeometry::intersection(world, geometry(monitor.rect));
            const int64_t area = overlap.empty() ? 0 : static_cast<int64_t>(overlap.width()) * overlap.height();
            if (area > largest) { largest = area; mSelectedMonitor = monitor.id; }
        }
    }
    mGeometryInitialized = true;
    mRefreshTimer.reset();
    if (choices_changed && getParent())
    {
        ++mMonitorChoiceRevision;
        if (LLComboBox* combo = getParent()->findChild<LLComboBox>("world_view_monitor_combo"))
        {
            combo->clearRows();
            for (size_t i = 0; i < mMonitors.size(); ++i)
            {
                if (!hasArea(clipped(mMonitors[i].rect, mClient))) continue;
                LLStringUtil::format_map_t args;
                args["[NUMBER]"] = std::to_string(i + 1);
                std::string label = text("world_view_display_label");
                LLStringUtil::format(label, args);
                combo->add(label, monitorChoice(mMonitors[i].id));
            }
            combo->setEnabled(mMonitorGeometryAvailable && combo->getItemCount() > 0);
            if (mSelectedMonitor.empty()) { combo->clear(); combo->setLabel(text("world_view_select_display")); }
            else combo->setValue(monitorChoice(mSelectedMonitor));
        }
    }
    updateControls();
}

LLRect FSWorldViewDiagram::diagramRect(const LLRect& raw) const
{
    if (!hasArea(mClient)) return LLRect();
    const F32 width = llmax(1, getRect().getWidth() - 20);
    const F32 height = llmax(1, getRect().getHeight() - 24);
    const F32 scale = llmin(width / mClient.getWidth(), height / mClient.getHeight());
    const F32 left = (getRect().getWidth() - mClient.getWidth() * scale) * .5f;
    const F32 bottom = 10.f + (height - mClient.getHeight() * scale) * .5f;
    return LLRect(ll_round(left + (raw.mLeft - mClient.mLeft) * scale),
                  ll_round(bottom + (raw.mTop - mClient.mBottom) * scale),
                  ll_round(left + (raw.mRight - mClient.mLeft) * scale),
                  ll_round(bottom + (raw.mBottom - mClient.mBottom) * scale));
}

void FSWorldViewDiagram::updateControls()
{
    if (!getParent()) return;
    FSWorldViewGeometry::Insets insets;
    auto selected = std::find_if(mMonitors.begin(), mMonitors.end(), [this](const LLWindow::MonitorRect& monitor) {
        return monitor.id == mSelectedMonitor;
    });
    const bool can_fit = selected != mMonitors.end() &&
        FSWorldViewGeometry::fit(geometry(mBase), geometry(selected->rect), insets);
    if (LLButton* button = getParent()->findChild<LLButton>("world_view_use_monitor"))
    {
        button->setEnabled(can_fit && mMonitorGeometryAvailable);
    }
    std::string status = text(!mMonitorGeometryAvailable ? "world_view_monitors_unavailable" :
                             mNeedsMonitorSelection ? "world_view_topology_changed" :
                             can_fit ? "world_view_monitor_help" : "world_view_monitor_no_fit");
    if (mMonitorGeometryAvailable && selected != mMonitors.end())
    {
        const size_t index = std::distance(mMonitors.begin(), selected) + 1;
        LLStringUtil::format_map_t args;
        args["[NUMBER]"] = std::to_string(index);
        std::string label = text("world_view_display_label");
        LLStringUtil::format(label, args);
        status = label + (selected->name.empty() ? "" : " (" + selected->name + ")") + ": " + status;
    }
    if (LLTextBox* label = getParent()->findChild<LLTextBox>("world_view_monitor_status")) label->setText(status);
    if (LLTextBox* label = getParent()->findChild<LLTextBox>("world_view_profile_status"))
    {
        std::string profile_status;
        const std::string active = gSavedSettings.getString("FSChromeActiveProfile");
        if (!active.empty() && FSChromeLayoutController::instanceExists())
        {
            LLSD saved;
            if (FSChromeLayout::isBuiltinProfileId(active))
                saved = FSChromeLayoutController::snapshotToLLSD(FSChromeLayout::builtinSnapshot(active));
            else
            {
                const LLSD profiles = gSavedSettings.getLLSD("FSChromeLayoutProfiles");
                if (profiles.has(active)) saved = FSChromeLayoutController::snapshotToLLSD(
                    FSChromeLayoutController::snapshotFromLLSD(profiles[active]));
            }
            const LLSD current = FSChromeLayoutController::snapshotToLLSD(FSChromeLayoutController::instance().captureSnapshot());
            if (saved.isMap() && !llsd_equals(saved, current)) profile_status = text("world_view_profile_modified");
        }
        label->setText(profile_status);
    }
}

void FSWorldViewDiagram::draw()
{
    if (!mGeometryInitialized || mRefreshTimer.getElapsedTimeF32() >= .5f) refreshGeometry();
    else updateControls();
    if (!hasArea(mClient)) { LLUICtrl::draw(); return; }

    gl_rect_2d(diagramRect(mClient), LLColor4(.12f, .12f, .12f, 1.f), true);
    const LLRect base = diagramRect(mBase);
    gl_rect_2d(base, LLColor4(.28f, .28f, .28f, 1.f), true);
    const auto insets = gSavedSettings.getBOOL("FSWorldViewEnabled") ? readInsets() : FSWorldViewGeometry::Insets{};
    const LLRect viewport = diagramRect(uiRect(FSWorldViewGeometry::apply(geometry(mBase), insets)));
    gl_rect_2d(viewport, LLColor4(.08f, .35f, .48f, 1.f), true);
    for (size_t i = 0; i < mMonitors.size(); ++i)
    {
        const auto& monitor = mMonitors[i];
        const LLRect overlap = clipped(monitor.rect, mClient);
        if (!hasArea(overlap)) continue;
        const LLRect rect = diagramRect(overlap);
        const bool selected = monitor.id == mSelectedMonitor;
        gl_rect_2d(rect, selected ? LLColor4(1.f, .85f, .25f, 1.f) : LLColor4(.7f, .7f, .7f, 1.f), false);
        if (selected && rect.getWidth() > 4 && rect.getHeight() > 4)
            gl_rect_2d(LLRect(rect.mLeft + 2, rect.mTop - 2, rect.mRight - 2, rect.mBottom + 2), LLColor4(1.f, .85f, .25f, 1.f), false);
        LLStringUtil::format_map_t args;
        args["[NUMBER]"] = std::to_string(i + 1);
        std::string label = text("world_view_display_label");
        LLStringUtil::format(label, args);
        LLFontGL::getFontSansSerifSmall()->renderUTF8(label, 0, static_cast<F32>(rect.mLeft + 5), static_cast<F32>(rect.mTop - 14),
            LLColor4::white, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
            S32_MAX, llmax(1, rect.getWidth() - 10), nullptr, true);
    }
    gl_rect_2d(viewport, LLColor4::white, false);
    const S32 cx = viewport.getCenterX(), cy = viewport.getCenterY();
    for (const LLRect& handle : {LLRect(viewport.mLeft - 3, cy + 5, viewport.mLeft + 3, cy - 5),
                                LLRect(viewport.mRight - 3, cy + 5, viewport.mRight + 3, cy - 5),
                                LLRect(cx - 5, viewport.mTop + 3, cx + 5, viewport.mTop - 3),
                                LLRect(cx - 5, viewport.mBottom + 3, cx + 5, viewport.mBottom - 3)})
        gl_rect_2d(handle, LLColor4::white, true);
    LLFontGL::getFontSansSerifSmall()->renderUTF8(text("world_view_diagram_label"), 0,
        static_cast<F32>(viewport.getCenterX()), static_cast<F32>(viewport.getCenterY()), LLColor4::white,
        LLFontGL::HCENTER, LLFontGL::VCENTER, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
        S32_MAX, llmax(1, viewport.getWidth() - 12), nullptr, true);
    LLUICtrl::draw();
}

bool FSWorldViewDiagram::handleMouseDown(S32 x, S32 y, MASK mask)
{
    refreshGeometry();
    if (!hasArea(mBase)) return true;
    const auto insets = gSavedSettings.getBOOL("FSWorldViewEnabled") ? readInsets() : FSWorldViewGeometry::Insets{};
    const LLRect viewport = diagramRect(uiRect(FSWorldViewGeometry::apply(geometry(mBase), insets)));
    if (y >= viewport.mBottom - 6 && y <= viewport.mTop + 6)
    {
        if (std::abs(x - viewport.mLeft) <= 6) mEdge = LEFT;
        else if (std::abs(x - viewport.mRight) <= 6) mEdge = RIGHT;
    }
    if (mEdge == NONE && x >= viewport.mLeft - 6 && x <= viewport.mRight + 6)
    {
        if (std::abs(y - viewport.mTop) <= 6) mEdge = TOP;
        else if (std::abs(y - viewport.mBottom) <= 6) mEdge = BOTTOM;
    }
    if (mEdge != NONE)
    {
        mDragStart = readInsets();
        mDragStartPercent = {gSavedSettings.getF32("FSWorldViewInsetLeft"), gSavedSettings.getF32("FSWorldViewInsetRight"),
                             gSavedSettings.getF32("FSWorldViewInsetTop"), gSavedSettings.getF32("FSWorldViewInsetBottom")};
        mDragStartEnabled = gSavedSettings.getBOOL("FSWorldViewEnabled");
        mDragStartX = x;
        mDragStartY = y;
        mDragMoved = false;
        gFocusMgr.setKeyboardFocus(this);
        gFocusMgr.setMouseCapture(this);
    }
    else
    {
        for (const auto& monitor : mMonitors)
        {
            const LLRect overlap = clipped(monitor.rect, mClient);
            if (hasArea(overlap) && diagramRect(overlap).pointInRect(x, y))
            {
                mSelectedMonitor = monitor.id;
                mNeedsMonitorSelection = false;
                if (LLComboBox* combo = getParent()->findChild<LLComboBox>("world_view_monitor_combo"))
                    combo->setValue(monitorChoice(mSelectedMonitor));
                updateControls();
                break;
            }
        }
    }
    return true;
}

bool FSWorldViewDiagram::handleHover(S32 x, S32 y, MASK mask)
{
    if (mEdge == NONE) return true;
    if (mRefreshTimer.getElapsedTimeF32() >= .5f) refreshGeometry();
    if (mEdge == NONE) return true;
    const LLRect base = diagramRect(mBase);
    if (!hasArea(base)) return true;
    const S32 delta = (mEdge == LEFT || mEdge == RIGHT) ? x - mDragStartX : y - mDragStartY;
    if (!delta && !mDragMoved) return true;
    mDragMoved = true;
    auto insets = FSWorldViewGeometry::normalize(mDragStartEnabled ? mDragStart : FSWorldViewGeometry::Insets{});
    switch (mEdge)
    {
    case LEFT: insets.left = llclamp(insets.left + static_cast<F32>(delta) / base.getWidth(), 0.f, .95f - insets.right); break;
    case RIGHT: insets.right = llclamp(insets.right - static_cast<F32>(delta) / base.getWidth(), 0.f, .95f - insets.left); break;
    case TOP: insets.top = llclamp(insets.top - static_cast<F32>(delta) / base.getHeight(), 0.f, .95f - insets.bottom); break;
    case BOTTOM: insets.bottom = llclamp(insets.bottom + static_cast<F32>(delta) / base.getHeight(), 0.f, .95f - insets.top); break;
    case NONE: return true;
    }
    writeInsets(insets);
    return true;
}

bool FSWorldViewDiagram::handleMouseUp(S32 x, S32 y, MASK mask)
{
    if (mEdge != NONE) handleHover(x, y, mask);
    mEdge = NONE;
    if (hasMouseCapture()) gFocusMgr.setMouseCapture(nullptr);
    return true;
}

void FSWorldViewDiagram::cancelDrag()
{
    if (mEdge != NONE)
    {
        mEdge = NONE;
        if (FSChromeLayoutController::instanceExists())
            FSChromeLayoutController::instance().applyViewportInsets(mDragStartEnabled, mDragStartPercent[0],
                mDragStartPercent[1], mDragStartPercent[2], mDragStartPercent[3]);
        else
        {
            gSavedSettings.setBOOL("FSWorldViewEnabled", mDragStartEnabled);
            gSavedSettings.setF32("FSWorldViewInsetLeft", mDragStartPercent[0]);
            gSavedSettings.setF32("FSWorldViewInsetRight", mDragStartPercent[1]);
            gSavedSettings.setF32("FSWorldViewInsetTop", mDragStartPercent[2]);
            gSavedSettings.setF32("FSWorldViewInsetBottom", mDragStartPercent[3]);
            if (gViewerWindow) gViewerWindow->updateWorldViewRect(gAgentCamera.cameraMouselook());
        }
    }
    if (hasMouseCapture()) gFocusMgr.setMouseCapture(nullptr);
}
void FSWorldViewDiagram::onMouseCaptureLost() { cancelDrag(); }
void FSWorldViewDiagram::onVisibilityChange(bool visible)
{
    if (!visible) cancelDrag();
    else mGeometryInitialized = false;
    LLUICtrl::onVisibilityChange(visible);
}
bool FSWorldViewDiagram::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_ESCAPE && mEdge != NONE) { cancelDrag(); return true; }
    return LLUICtrl::handleKeyHere(key, mask);
}

bool FSWorldViewDiagram::fitSelectedMonitor()
{
    cancelDrag();
    const std::string selected_id = mSelectedMonitor;
    refreshGeometry();
    if (!mMonitorGeometryAvailable || mSelectedMonitor != selected_id) return false;
    for (const auto& monitor : mMonitors)
    {
        if (monitor.id != selected_id) continue;
        FSWorldViewGeometry::Insets insets;
        if (!FSWorldViewGeometry::fit(geometry(mBase), geometry(monitor.rect), insets)) return false;
        writeInsets(insets);
        return true;
    }
    return false;
}

LLSD FSWorldViewDiagram::monitorChoice(const std::string& id) const
{
    // LLComboBox compares values as strings, so maps cannot identify rows.
    return LLSD(std::to_string(mMonitorChoiceRevision) + ":" + id);
}

void FSWorldViewDiagram::selectMonitor(const LLSD& choice)
{
    refreshGeometry();
    // An open dropdown may still deliver a choice from before a display was
    // unplugged. In particular, SDL display indices can identify a new monitor.
    const std::string value = choice.asString();
    const std::string prefix = std::to_string(mMonitorChoiceRevision) + ":";
    if (value.compare(0, prefix.size(), prefix) != 0) return;
    const std::string id = value.substr(prefix.size());
    auto monitor = std::find_if(mMonitors.begin(), mMonitors.end(), [&](const LLWindow::MonitorRect& item) { return item.id == id; });
    if (monitor == mMonitors.end() || !hasArea(clipped(monitor->rect, mClient))) return;
    mSelectedMonitor = id;
    mNeedsMonitorSelection = false;
    updateControls();
}
