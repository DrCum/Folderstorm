/**
 * @file fsworkspacecontroller.cpp
 * @brief Manual workspace adapters and reversible Preferences previews
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 The Phoenix Firestorm Project, Inc.
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
#include "fsworkspacelistener.h"
#include "fsworkspacecontroller.h"
#include "fsworkspacecontextadapter.h"
#include "fsworkspacequickaccess.h"
#include "fschromelayoutcontroller.h"
#include "llagent.h"
#include "lleventapi.h"
#include "llviewercontrol.h"
#include "llsdutil.h"
#include "fsassistantpolicy.h"
#include <algorithm>
namespace
{
LLSD state()
{
    auto& controller = FSWorkspaceController::instance();
    LLSD result; result["workspace_tools"] = 1; result["available"] = controller.available();
    result["can_switch"] = controller.canQuickSwitch(); result["active"] = controller.activeId();
    result["modified"] = controller.modified(); result["has_previous"] = controller.hasPrevious();
    const auto report = controller.restoreReport(); result["report"]["name"] = report.name; result["report"]["complete"] = report.complete;
    result["report"]["applied"] = report.applied; result["report"]["adjusted"] = report.adjusted; result["report"]["skipped"] = report.skipped;
    result["report"]["revision"] = static_cast<S32>(controller.reportRevision());
    result["report"]["details"] = LLSD::emptyArray();
    for (const auto& line : report.lines) { LLSD row; row["subject"] = line.subject; row["reason"] = line.reason; row["count"] = line.count; result["report"]["details"].append(row); }
    return result;
}
bool target(const LLSD& request, FSWorkspaceLayout::Workspace& workspace, std::string& error)
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.available()) { error = "Workspaces require a logged-in viewer"; return false; }
    if (request["op"].asString() == "previous")
    { if (!controller.previousWorkspace(workspace)) { error = "Previous arrangement is unavailable"; return false; } return true; }
    if (!request["kind"].isString() || !request["id"].isString() || request["id"].asString().empty() || request["id"].asString().size() > 128)
    { error = "kind and an exact saved id are required"; return false; }
    const auto id = request["id"].asString(), kind = request["kind"].asString();
    if (kind == "workspace" && controller.readProfile(id, workspace)) return true;
    if (kind == "layout")
    {
        const auto names = FSChromeLayoutController::instance().profileNames();
        if (std::find(names.begin(), names.end(), id) != names.end())
        {
            workspace.components = FSWorkspaceLayout::Chrome;
            workspace.chrome = FSChromeLayout::isBuiltinProfileId(id) ? FSChromeLayout::builtinSnapshot(id) :
                FSChromeLayoutController::snapshotFromLLSD(gSavedSettings.getLLSD("FSChromeLayoutProfiles")[id]); return true;
        }
    }
    error = "Saved workspace or layout was not found"; return false;
}
LLSD evidence(const LLSD& request, std::string& error)
{
    FSWorkspaceLayout::Workspace workspace; if (!target(request, workspace, error)) return LLSD();
    LLSD result; result["account"] = gAgent.getID(); result["session"] = gAgent.getSessionID();
    result["definition"] = FSWorkspaceLayout::toLLSD(workspace);
    for (bool camera : {false, true})
    {
        const auto& group = camera ? workspace.context.camera : workspace.context.graphics;
        if (group.mode == FSWorkspaceContext::Mode::Off) continue;
        FSWorkspaceContext::Values values; std::string reason;
        if (!FSWorkspaceContext::resolveGroup(group, camera, values, reason)) { error = reason; return LLSD(); }
        for (const auto& control : FSWorkspaceContext::controls(camera))
            if (values.count(control.name)) result[camera ? "camera_values" : "graphics_values"][control.name] = FSWorkspaceContext::controlValue(control, values.at(control.name));
    }
    result["huds"] = LLSD::emptyArray();
    for (const auto& hud : workspace.context.huds)
    { LLSD row; row["item"] = hud.item; row["point"] = hud.point; row["worn"] = FSWorkspaceContext::hudWorn(hud); result["huds"].append(row); }
    return result;
}
class FSWorkspaceListener : public LLEventAPI
{
public:
    FSWorkspaceListener() : LLEventAPI("LLWorkspace", "Bounded workspace and layout access")
    {
        for (const char* op : {"list", "status", "preview", "apply", "previous"}) add(op, "Workspace tools", &FSWorkspaceListener::dispatch);
    }
    void dispatch(const LLSD& request)
    {
        Response response(LLSD(), request);
        const auto op = request["op"].asString();
        if (op == "apply" || op == "previous") return response.error("Workspace mutation requires the bridge's prepared execution context");
        auto& controller = FSWorkspaceController::instance();
        if (op == "status") { response.setResponse(state()); return; }
        if (!controller.available()) return response.error("Workspaces require a logged-in viewer");
        if (op == "list")
        {
            LLSD result = state(); result["entries"] = LLSD::emptyArray();
            const auto entries = FSWorkspaceQuickAccess::entries(); const auto favorites = FSWorkspaceQuickAccess::favorites(entries);
            for (const auto& entry : entries)
            {
                LLSD row; row["kind"] = entry.layout ? "layout" : "workspace"; row["id"] = entry.key.substr(entry.layout ? 7 : 10);
                row["name"] = entry.label; row["favorite"] = favorites.count(entry.key) != 0;
                FSWorkspaceLayout::Workspace saved; if (!entry.layout && controller.readProfile(row["id"].asString(), saved)) row["components"] = saved.components;
                result["entries"].append(row);
            }
            response.setResponse(result); return;
        }
        FSWorkspaceLayout::Workspace workspace; std::string error;
        if (!target(request, workspace, error)) return response.error(error);
        const auto diagram = controller.diagram(workspace); LLSD result;
        result["kind"] = request["kind"]; result["id"] = request["id"]; result["components"] = workspace.components;
        result["adjusted"] = diagram.adjusted; result["skipped"] = diagram.skipped; result["missing_folders"] = diagram.missing_folders;
        result["frame"]["width"] = diagram.frame.width(); result["frame"]["height"] = diagram.frame.height(); result["windows"] = LLSD::emptyArray();
        for (const auto& window : diagram.windows)
        { LLSD row; row["name"] = window.label; row["left"] = window.rect.left; row["bottom"] = window.rect.bottom; row["width"] = window.rect.width(); row["height"] = window.rect.height(); result["windows"].append(row); }
        result["graphics_preset"] = workspace.context.graphics.preset; result["camera_preset"] = workspace.context.camera.preset;
        result["hud_count"] = static_cast<S32>(workspace.context.huds.size());
        result["chrome"] = FSChromeLayoutController::snapshotToLLSD(diagram.chrome);
        result["huds"] = LLSD::emptyArray();
        for (const auto& hud : workspace.context.huds)
        { LLSD row; row["name"] = hud.name; row["point"] = hud.point; row["worn"] = FSWorkspaceContext::hudWorn(hud); result["huds"].append(row); }
        response.setResponse(result);
    }
};
}
void fs_initialize_workspace_api()
{
    static FSWorkspaceListener listener;
}
bool fs_prepare_workspace(FSAssistantPreparedOperation& operation, std::string& error)
{
    auto& controller = FSWorkspaceController::instance();
    if (!controller.canQuickSwitch()) { error = "Log in and finish Preferences before switching workspaces"; return false; }
    operation.validation = evidence(operation.request, error); if (!error.empty()) return false;
    const auto& definition = operation.validation["definition"];
    operation.requiredClasses = fs_assistant::workspace_classes(definition.has("camera"), definition.has("huds") && definition["huds"].size());
    LLSD summary; summary["op"] = operation.request["op"].asString() == "previous" ? "workspace_previous" : "workspace_apply";
    summary["subject_count"] = 1; summary["subjects"] = LLSD::emptyArray(); summary["consequences"] = LLSD::emptyArray();
    LLSD row; row["name"] = operation.request["op"].asString() == "previous" ? "Previous arrangement" : operation.request["id"].asString();
    std::string groups;
    for (const auto& group : std::vector<std::pair<int, const char*>>{{FSWorkspaceLayout::Chrome, "viewport and bars"},
        {FSWorkspaceLayout::Inventory, "Inventory windows"}, {FSWorkspaceLayout::Maps, "maps"}, {FSWorkspaceLayout::Chat, "compatible chat"},
        {FSWorkspaceLayout::Graphics, "graphics settings"}, {FSWorkspaceLayout::Camera, "camera settings"}, {FSWorkspaceLayout::HUDs, "selected HUDs"}})
        if (definition["components"].asInteger() & group.first) { if (!groups.empty()) groups += ", "; groups += group.second; }
    if (definition["remember_toolbars"].asBoolean()) { if (!groups.empty()) groups += ", "; groups += "toolbar buttons"; }
    row["after_name"] = "Restore: " + groups; summary["subjects"].append(row);
    if (definition.has("graphics")) summary["consequences"].append("workspace_graphics");
    if (definition.has("camera")) summary["consequences"].append("workspace_camera");
    for (const auto& hud : llsd::inArray(definition["huds"]))
    { LLSD subject; subject["name"] = hud["name"]; subject["after_name"] = std::string("Add if missing at HUD ") + FSWorkspaceContext::hudPointName(hud["point"].asInteger()); summary["subjects"].append(subject); }
    summary["subject_count"] = static_cast<S32>(summary["subjects"].size()); operation.summary = summary; return true;
}
bool fs_validate_workspace(const FSAssistantPreparedOperation& operation, std::string& error)
{
    if (!FSWorkspaceController::instance().canQuickSwitch()) { error = "Log in and finish Preferences before switching workspaces"; return false; }
    const auto current = evidence(operation.request, error);
    if (!error.empty()) return false;
    if (current != operation.validation) { error = "Workspace, preset, attachments or session changed; request a fresh switch"; return false; }
    return true;
}
LLSD fs_execute_workspace(const FSAssistantPreparedOperation& operation, const FSAssistantExecutionContext& context, std::string& error)
{
    if (!context.allowed || !context.allowed() || !fs_validate_workspace(operation, error))
    { if (error.empty()) error = "Workspace permissions were revoked"; return LLSD(); }
    auto& controller = FSWorkspaceController::instance();
    const auto& request = operation.request;
    const bool applied = request["op"].asString() == "previous" ? controller.returnPrevious() : request["kind"].asString() == "layout" ?
        controller.quickSwitchLayout(request["id"].asString()) : controller.quickSwitchWorkspace(request["id"].asString());
    if (!applied) { error = "Workspace could not be applied"; return LLSD(); }
    controller.acceptAssistantSwitch(context.allowed);
    LLSD result = state(); result["accepted"] = true; result["pending"] = !result["report"]["complete"].asBoolean(); return result;
}
