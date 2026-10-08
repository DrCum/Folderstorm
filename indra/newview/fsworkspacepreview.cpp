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
#include "fsworkspacepreview.h"
#include "fsworkspacequickaccess.h"
#include "fschromelayoutcontroller.h"
#include "fsworldviewgeometry.h"
#include "llagent.h"
#include "llbutton.h"
#include "llfontgl.h"
#include "llrender2dutils.h"
#include "lltextbox.h"
#include "lluictrlfactory.h"
#include "lluicolortable.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "lltoolbarview.h"
#include <algorithm>

static LLDefaultChildRegistry::Register<FSWorkspaceDiagram> t_workspace_diagram("workspace_diagram");

bool FSFloaterWorkspacePreview::postBuild()
{
    getChild<LLButton>("switch")->setCommitCallback([this](LLUICtrl*, const LLSD&)
    { if (FSWorkspaceQuickAccess::apply(mEntry, mAccount, mSession)) closeFloater(false); });
    return LLFloater::postBuild();
}
void FSFloaterWorkspacePreview::onOpen(const LLSD& key) { select(key.asString()); }
void FSFloaterWorkspacePreview::select(const std::string& key)
{
    mAccount = gAgent.getID(); mSession = gAgent.getSessionID(); mEntry = key;
    refresh();
}
void FSFloaterWorkspacePreview::draw()
{
    if (mTimer.getElapsedTimeF32() >= .5f) refresh();
    LLFloater::draw();
}
void FSFloaterWorkspacePreview::refresh()
{
    mTimer.reset();
    auto& controller = FSWorkspaceController::instance();
    FSWorkspaceLayout::Workspace workspace;
    bool valid = controller.available() && mAccount == gAgent.getID() && mSession == gAgent.getSessionID();
    const bool layout = mEntry.compare(0, 7, "layout:") == 0;
    std::string label;
    bool exists = false;
    for (const auto& entry : FSWorkspaceQuickAccess::entries())
        if (entry.key == mEntry) { label = entry.label; exists = true; break; }
    valid = valid && exists;
    if (valid && layout)
    {
        const auto id = mEntry.substr(7);
        workspace.components = FSWorkspaceLayout::Chrome;
        workspace.chrome = FSChromeLayout::isBuiltinProfileId(id) ? FSChromeLayout::builtinSnapshot(id) :
            FSChromeLayoutController::snapshotFromLLSD(gSavedSettings.getLLSD("FSChromeLayoutProfiles")[id]);
    }
    else if (valid) valid = controller.readProfile(mEntry.substr(10), workspace);
    getChild<LLButton>("switch")->setEnabled(valid && controller.canQuickSwitch());
    getChild<LLTextBox>("entry")->setText(valid ? label : getString("invalid"));
    auto* canvas = getChild<FSWorkspaceDiagram>("diagram"); canvas->setVisible(valid);
    if (!valid) { getChild<LLTextBox>("details")->setText(getString("invalid")); return; }
    const auto diagram = controller.diagram(workspace); canvas->setDiagram(diagram);
    std::string groups = "Restores: ";
    if (workspace.components & FSWorkspaceLayout::Chrome) groups += "viewport/bars; ";
    if (workspace.components & FSWorkspaceLayout::Inventory) groups += "Inventory; ";
    if (workspace.components & FSWorkspaceLayout::Maps) groups += "maps; ";
    if (workspace.components & FSWorkspaceLayout::Chat) groups += "compatible chat; ";
    if (workspace.components & FSWorkspaceLayout::Graphics) groups += "graphics settings; ";
    if (workspace.components & FSWorkspaceLayout::Camera) groups += "camera settings; ";
    if (workspace.components & FSWorkspaceLayout::HUDs) groups += std::to_string(workspace.context.huds.size()) + " selected HUDs (reviewed); ";
    if (workspace.remember_toolbars) groups += "toolbar buttons; ";
    groups += "other groups stay as they are.\n";
    groups += std::to_string(diagram.adjusted) + " windows adjusted to fit; " + std::to_string(diagram.skipped) + " skipped; " + std::to_string(diagram.missing_folders) + " missing folders.\n";
    groups += getString("geometry_help");
    getChild<LLTextBox>("details")->setText(groups);
    std::string context;
    for (bool camera : {false, true})
    {
        const auto& group = camera ? workspace.context.camera : workspace.context.graphics;
        if (group.mode != FSWorkspaceContext::Mode::Off)
            context += std::string(camera ? "Camera: " : "Graphics: ") +
                (group.mode == FSWorkspaceContext::Mode::Preset ? group.preset : "captured settings") + "\n";
    }
    for (const auto& hud : workspace.context.huds) context += "HUD: " + hud.name + " (add if missing)\n";
    getChild<LLTextBox>("details")->setToolTip(context);
}
void FSWorkspaceDiagram::draw()
{
    const auto& frame = mDiagram.frame;
    if (frame.width() <= 0.f || frame.height() <= 0.f) return;
    const F32 width = std::max(1.f, static_cast<F32>(getRect().getWidth() - 20));
    const F32 height = std::max(1.f, static_cast<F32>(getRect().getHeight() - 20));
    const F32 scale = std::min(width / frame.width(), height / frame.height());
    const F32 left = 10.f + (width - frame.width() * scale) * .5f;
    const F32 bottom = 10.f + (height - frame.height() * scale) * .5f;
    auto project = [&](const FSWorkspaceLayout::Rect& r)
    {
        return LLRect(ll_round(left + (std::clamp(r.left, frame.left, frame.right) - frame.left) * scale),
                      ll_round(bottom + (std::clamp(r.top, frame.bottom, frame.top) - frame.bottom) * scale),
                      ll_round(left + (std::clamp(r.right, frame.left, frame.right) - frame.left) * scale),
                      ll_round(bottom + (std::clamp(r.bottom, frame.bottom, frame.top) - frame.bottom) * scale));
    };
    const LLColor4 text = LLUIColorTable::instance().getColor("LabelTextColor");
    auto label = [&](const LLRect& rect, const std::string& value)
    {
        LLFontGL::getFontSansSerifSmall()->renderUTF8(value, 0, static_cast<F32>(rect.mLeft + 3), static_cast<F32>(rect.mTop - 13),
            text, LLFontGL::LEFT, LLFontGL::BASELINE, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,
            S32_MAX, std::max(1, rect.getWidth() - 6), nullptr, true);
    };
    gl_rect_2d(project(frame), text, false);
    const auto chrome = (mDiagram.components & FSWorkspaceLayout::Chrome) ? mDiagram.chrome : FSChromeLayoutController::instance().captureSnapshot();
    FSWorkspaceLayout::Rect viewport = frame;
    if (gViewerWindow)
    {
        const auto raw = gViewerWindow->getWorldViewBaseRectRaw();
        const auto world = FSWorldViewGeometry::apply({raw.mLeft, raw.mBottom, raw.mRight, raw.mTop}, chrome.viewport_enabled ?
            FSWorldViewGeometry::Insets{chrome.inset_left * .01f, chrome.inset_right * .01f, chrome.inset_top * .01f, chrome.inset_bottom * .01f} : FSWorldViewGeometry::Insets{});
        const auto display = gViewerWindow->getDisplayScale();
        S32 x = 0, y = 0; gFloaterView->localPointToScreen(0, 0, &x, &y);
        viewport = {world.left / display.mV[VX] - static_cast<F32>(x), world.bottom / display.mV[VY] - static_cast<F32>(y),
                    world.right / display.mV[VX] - static_cast<F32>(x), world.top / display.mV[VY] - static_cast<F32>(y)};
    }
    const LLColor4 viewport_color(.45f, .7f, 1.f, 1.f);
    gl_rect_2d(project(viewport), viewport_color, false);
    label(project(viewport), (mDiagram.components & FSWorkspaceLayout::Chrome) ? "Viewport" : "Viewport stays");
    if (mDiagram.components & FSWorkspaceLayout::Chrome)
    {
        const FSChromeLayout::Rect holder{ll_round(frame.left), ll_round(frame.bottom), ll_round(frame.right), ll_round(frame.top)};
        const FSChromeLayout::Rect world{ll_round(viewport.left), ll_round(viewport.bottom), ll_round(viewport.right), ll_round(viewport.top)};
        auto span = [&](const FSChromeLayout::SpanRequest& request, F32 y, const std::string& name)
        {
            const auto fitted = FSChromeLayout::computeHorizontalSpan(holder, world, request, FSChromeLayout::kMinInteractiveWidth);
            const auto r = project({static_cast<F32>(fitted.left), y, static_cast<F32>(fitted.right), y + 16.f});
            gl_rect_2d(r, viewport_color, false); label(r, name);
        };
        span(chrome.bottom_dock, frame.bottom, "Dock");
        span(chrome.nav_favorites, frame.top - 32.f, "Navigation");
        span(chrome.menu_status, frame.top - 16.f, "Menu/status");
        int lw = 32, rw = 32;
        if (gToolBarView)
        {
            if (auto* bar = gToolBarView->getToolbar(LLToolBarEnums::TOOLBAR_LEFT)) lw = bar->getRect().getWidth();
            if (auto* bar = gToolBarView->getToolbar(LLToolBarEnums::TOOLBAR_RIGHT)) rw = bar->getRect().getWidth();
        }
        const auto sides = FSChromeLayout::computeSideLayout(holder, world, {chrome.left_placement, lw, chrome.left_offset}, {chrome.right_placement, rw, chrome.right_offset});
        if (sides.left_visible) gl_rect_2d(project({frame.left + static_cast<F32>(sides.left_outer_spacer), frame.bottom,
            frame.left + static_cast<F32>(sides.left_outer_spacer + sides.left_toolbar_width), frame.top}), viewport_color, false);
        if (sides.right_visible) gl_rect_2d(project({frame.right - static_cast<F32>(sides.right_outer_spacer + sides.right_toolbar_width), frame.bottom,
            frame.right - static_cast<F32>(sides.right_outer_spacer), frame.top}), viewport_color, false);
    }
    const LLColor4 window_color(1.f, .8f, .35f, 1.f);
    for (const auto& window : mDiagram.windows)
    {
        const auto r = project(window.rect); gl_rect_2d(r, window_color, false);
        label(r, window.label + (window.minimized ? " (minimized)" : ""));
    }
    LLUICtrl::draw();
}
