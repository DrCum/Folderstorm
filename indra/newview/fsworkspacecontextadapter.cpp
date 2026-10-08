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
#include "fsworkspacecontextadapter.h"
#include "fsworkspacefile.h"
#include "llagentcamera.h"
#include "llagent.h"
#include "llpresetsmanager.h"
#include "llviewercontrol.h"
#include "llsdserialize.h"
#include "lluri.h"
#include "llfile.h"
#include "lldir.h"
#include "llviewerobject.h"
#include "lltrans.h"
#include <algorithm>
#include "llfollowcam.h"
#include "llvoavatarself.h"
#include "llinventorymodel.h"
#include "llinventorybridge.h"
#include "llviewerjointattachment.h"
#include "rlvactions.h"
#include "rlvlocks.h"
#include <fstream>
#include <sstream>
namespace FSWorkspaceContext
{
namespace
{
bool extract(const Control& control, const LLSD& value, std::vector<double>& result)
{
    if (control.kind == Kind::Vector)
    {
        if (!value.isArray() || value.size() != 3) return false;
        for (S32 i = 0; i < 3; ++i) { if (!value[i].isReal() && !value[i].isInteger()) return false; result.push_back(value[i].asReal()); }
    }
    else
    {
        if ((control.kind == Kind::Boolean && !value.isBoolean()) ||
            (control.kind == Kind::Integer && !value.isInteger()) ||
            (control.kind == Kind::Real && !value.isInteger() && !value.isReal())) return false;
        result.push_back(control.kind == Kind::Boolean ? (value.asBoolean() ? 1. : 0.) : value.asReal());
    }
    return validValue(control, result);
}
}
LLSD controlValue(const Control& control, const std::vector<double>& values)
{
    if (!validValue(control, values)) return LLSD();
    if (control.kind == Kind::Vector) { LLSD result = LLSD::emptyArray(); for (double value : values) result.append(value); return result; }
    if (control.kind == Kind::Boolean) return LLSD(values[0] != 0.);
    if (control.kind == Kind::Integer) return LLSD(static_cast<S32>(values[0]));
    return LLSD(values[0]);
}
Group captureGroup(bool camera)
{
    Group group; group.mode = Mode::Current;
    for (const auto& definition : controls(camera))
        if (LLControlVariable* control = gSavedSettings.getControl(definition.name))
        {
            std::vector<double> value;
            if (extract(definition, control->getValue(), value)) group.values[definition.name] = value;
        }
    if (camera) group.values["CameraZoomFraction"] = {static_cast<double>(gAgentCamera.getWorkspaceCameraZoom())};
    return group;
}
bool resolveGroup(const Group& group, bool camera, Values& values, std::string& error)
{
    values.clear(); error.clear();
    if (group.mode == Mode::Off) return true;
    if (group.mode == Mode::Current) { values = group.values; return true; }
    const auto directory = camera ? PRESETS_CAMERA : PRESETS_GRAPHIC;
    LLPresetsManager::preset_name_list_t names;
    LLPresetsManager::instance().loadPresetNamesFromDir(directory, names, DEFAULT_SHOW);
    if (std::find(names.begin(), names.end(), group.preset) == names.end()) { error = "preset_unavailable"; return false; }
    const auto path = LLPresetsManager::getPresetsDir(directory) + gDirUtilp->getDirDelimiter() + LLURI::escape(group.preset == LLTrans::getString(PRESETS_DEFAULT) ? PRESETS_DEFAULT : group.preset) + ".xml";
    llifstream input(path.c_str(), std::ios::binary);
    std::string document(FSWorkspaceFile::MAX_TRANSFER_BYTES + 1, '\0');
    input.read(&document[0], static_cast<std::streamsize>(document.size())); document.resize(static_cast<size_t>(input.gcount()));
    LLSD data; std::istringstream stream(document);
    if (!input.eof() || !FSWorkspaceFile::boundedXML(document) || LLSDSerialize::fromXML(data, stream) <= 0 || !data.isMap())
    { error = "preset_unavailable"; return false; }
    for (const auto& control : controls(camera))
        if (data.has(control.name))
        {
            std::vector<double> value;
            if (!data[control.name].isMap() || !extract(control, data[control.name]["Value"], value))
            { error = "preset_invalid"; values.clear(); return false; }
            values[control.name] = value;
        }
    if (values.empty()) { error = "preset_invalid"; return false; }
    return true;
}
bool cameraAllowed()
{
    return !RlvActions::isCameraPresetLocked() && !RlvActions::isCameraDistanceClamped() && !RlvActions::isCameraFOVClamped() &&
        gAgentCamera.getCameraMode() == CAMERA_MODE_THIRD_PERSON && !LLFollowCamMgr::instance().getActiveFollowCamParams();
}
std::vector<HUD> attachedHUDs()
{
    std::vector<HUD> result;
    if (!isAgentAvatarValid()) return result;
    for (const auto& entry : gAgentAvatarp->mAttachmentPoints)
        if (entry.second && entry.second->getIsHUDAttachment())
            for (const auto& object : entry.second->mAttachedObjects)
                if (object)
                    if (const auto* item = gInventory.getItem(object->getAttachmentItemID()))
                        result.push_back({item->getLinkedUUID().asString(), utf8str_truncate(item->getName(), 128), entry.first});
    return result;
}
bool hudWorn(const HUD& hud)
{
    for (const auto& worn : attachedHUDs()) if (worn.item == hud.item && worn.point == hud.point) return true;
    return false;
}
bool hudAvailable(const HUD& hud, std::string& error)
{
    if (!isAgentAvatarValid() || !gInventory.isInventoryUsable()) { error = "hud_not_ready"; return false; }
    const auto* item = gInventory.getItem(LLUUID(hud.item));
    auto point = gAgentAvatarp->mAttachmentPoints.find(hud.point);
    if (!item || !item->isFinished() || item->getPermissions().getOwner() != gAgent.getID() || item->getType() != LLAssetType::AT_OBJECT || point == gAgentAvatarp->mAttachmentPoints.end() ||
        !point->second || !point->second->getIsHUDAttachment()) { error = "hud_unavailable"; return false; }
    if (gAgentAvatarp->isWearingAttachment(item->getLinkedUUID()) && !hudWorn(hud)) { error = "hud_other_point"; return false; }
    if (RlvActions::isRlvEnabled() && (!(gRlvAttachmentLocks.canAttach(item) & RLV_WEAR_ADD) ||
        !(gRlvAttachmentLocks.canAttach(point->second) & RLV_WEAR_ADD))) { error = "restricted"; return false; }
    return true;
}
void attachHUD(const HUD& hud)
{
    std::string error;
    if (!hudAvailable(hud, error) || hudWorn(hud)) return;
    auto* item = gInventory.getItem(LLUUID(hud.item));
    auto point = gAgentAvatarp->mAttachmentPoints.find(hud.point);
    rez_attachment(item, point->second, false);
}
}
