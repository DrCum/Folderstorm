/**
 * @file llcameralistener.cpp
 * @brief Local Event API for camera pose and snapshots.
 */

#include "llviewerprecompiledheaders.h"

#include "llcameralistener.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "lldir.h"
#include "llfile.h"
#include "llsnapshotmodel.h"
#include "lluuid.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"

#include <algorithm>
#include <cmath>

namespace
{
LLCameraListener sCameraListener;

const S32 kDefaultMaxEdge = 1024;
const S32 kHardMaxEdge = 2048;

LLSD vec3(const LLVector3& value)
{
    return llsd::array(value.mV[VX], value.mV[VY], value.mV[VZ]);
}

LLSD vec3d(const LLVector3d& value)
{
    return llsd::array(value.mdV[VX], value.mdV[VY], value.mdV[VZ]);
}

bool read_vector(const LLSD& value, LLVector3& out, std::string& error)
{
    if (!value.isArray() || value.size() < 3)
    {
        error = "Expected a region position array [x, y, z]";
        return false;
    }
    out.setVec((F32)value[0].asReal(), (F32)value[1].asReal(), (F32)value[2].asReal());
    return true;
}

LLSD camera_state()
{
    const LLVector3d camera_global = gAgentCamera.getCameraPositionGlobal();
    const LLVector3d focus_global = gAgentCamera.getFocusGlobal();
    const LLVector3 camera_region = gAgent.getPosAgentFromGlobal(camera_global);
    const LLVector3 focus_region = gAgent.getPosAgentFromGlobal(focus_global);
    const LLVector3 avatar = gAgent.getPositionAgent();
    LLSD state;
    state["avatar_id"] = gAgent.getID();
    state["region_position"] = vec3(camera_region);
    state["focus_region"] = vec3(focus_region);
    state["agent_relative"] = vec3(camera_region - avatar);
    state["focus_agent_relative"] = vec3(focus_region - avatar);
    state["global_position"] = vec3d(camera_global);
    state["focus_global"] = vec3d(focus_global);
    state["distance"] = (F64)(camera_region - focus_region).magVec();
    return state;
}

bool pose_offset(const std::string& preset, LLVector3& offset, LLVector3& focus_offset, std::string& error)
{
    if (preset == "portrait")
    {
        offset.setVec(1.6f, 0.f, 1.55f);
        focus_offset.setVec(0.f, 0.f, 1.65f);
    }
    else if (preset == "full_body")
    {
        offset.setVec(3.8f, 0.f, 1.1f);
        focus_offset.setVec(0.f, 0.f, 0.9f);
    }
    else if (preset == "front")
    {
        offset.setVec(2.6f, 0.f, 1.3f);
        focus_offset.setVec(0.f, 0.f, 1.2f);
    }
    else if (preset == "back")
    {
        offset.setVec(-2.6f, 0.f, 1.3f);
        focus_offset.setVec(0.f, 0.f, 1.2f);
    }
    else if (preset == "left")
    {
        offset.setVec(0.f, 2.6f, 1.3f);
        focus_offset.setVec(0.f, 0.f, 1.2f);
    }
    else if (preset == "right")
    {
        offset.setVec(0.f, -2.6f, 1.3f);
        focus_offset.setVec(0.f, 0.f, 1.2f);
    }
    else
    {
        error = "Unknown camera preset";
        return false;
    }
    return true;
}

void apply_pose(const LLVector3d& camera_global, const LLVector3d& focus_global)
{
    gAgentCamera.changeCameraToThirdPerson(false);
    gAgentCamera.setCameraPosAndFocusGlobal(camera_global, focus_global, gAgent.getID());
}
}

LLCameraListener::LLCameraListener()
  : LLEventAPI("LLCamera",
               "Read and pose the local camera, and capture a snapshot to a temp file")
{
    add("get", "Return the current camera pose",
        &LLCameraListener::get, llsd::map("reply", LLSD()));
    add("setPose", "Place the camera using a named preset relative to the avatar",
        &LLCameraListener::setPose, llsd::map("preset", LLSD(), "reply", LLSD()));
    add("set", "Place the camera at a region position looking at a region focus",
        &LLCameraListener::set, llsd::map("position", LLSD(), "focus", LLSD(), "reply", LLSD()));
    add("reset", "Return the camera to the default third-person pose",
        &LLCameraListener::reset, llsd::map("reply", LLSD()));
    add("snapshot", "Write a JPEG snapshot to a temporary file and return its path",
        &LLCameraListener::snapshot, llsd::map("reply", LLSD()));
}

void LLCameraListener::get(LLSD const& data)
{
    Response response(camera_state(), data);
}

void LLCameraListener::setPose(LLSD const& data)
{
    Response response(LLSD(), data);
    std::string preset = data["preset"].asString();
    LLStringUtil::trim(preset);
    LLStringUtil::toLower(preset);
    LLVector3 offset;
    LLVector3 focus_offset;
    std::string error;
    if (!pose_offset(preset, offset, focus_offset, error))
    {
        return response.error(error);
    }
    if (!isAgentAvatarValid())
    {
        return response.error("Avatar is not loaded");
    }

    const LLQuaternion rotation = gAgent.getQuat();
    LLVector3 forward = LLVector3::x_axis * rotation;
    forward.mV[VZ] = 0.f;
    if (forward.normalize() < 0.001f)
    {
        forward = LLVector3::x_axis;
    }
    LLVector3 left = LLVector3::z_axis % forward;
    left.normalize();
    const LLVector3 placed = forward * offset.mV[VX] + left * offset.mV[VY] + LLVector3(0.f, 0.f, offset.mV[VZ]);
    const LLVector3 focus_local = LLVector3(0.f, 0.f, focus_offset.mV[VZ]);
    const LLVector3d avatar_global = gAgent.getPositionGlobal();
    apply_pose(avatar_global + LLVector3d(placed), avatar_global + LLVector3d(focus_local));

    response["ok"] = true;
    response["preset"] = preset;
    response["camera"] = camera_state();
}

void LLCameraListener::set(LLSD const& data)
{
    Response response(LLSD(), data);
    LLVector3 position;
    LLVector3 focus;
    std::string error;
    if (!read_vector(data["position"], position, error) ||
        !read_vector(data["focus"], focus, error))
    {
        return response.error(error.empty() ? "position and focus are required" : error);
    }
    if (!gAgent.getRegion())
    {
        return response.error("Agent is not in a region");
    }
    apply_pose(gAgent.getPosGlobalFromAgent(position), gAgent.getPosGlobalFromAgent(focus));
    response["ok"] = true;
    response["camera"] = camera_state();
}

void LLCameraListener::reset(LLSD const& data)
{
    Response response(LLSD(), data);
    gAgentCamera.changeCameraToDefault();
    response["ok"] = true;
    response["camera"] = camera_state();
}

void LLCameraListener::snapshot(LLSD const& data)
{
    Response response(LLSD(), data);
    if (!gViewerWindow)
    {
        return response.error("Viewer window is not available");
    }

    S32 max_edge = data.has("max_edge") ? data["max_edge"].asInteger() : kDefaultMaxEdge;
    max_edge = std::clamp(max_edge, 64, kHardMaxEdge);
    S32 width = gViewerWindow->getWindowWidthRaw();
    S32 height = gViewerWindow->getWindowHeightRaw();
    if (width < 1 || height < 1)
    {
        return response.error("Viewer window has no drawable size");
    }
    const S32 long_edge = std::max(width, height);
    if (long_edge > max_edge)
    {
        const F32 scale = (F32)max_edge / (F32)long_edge;
        width = std::max(1, (S32)std::lround(width * scale));
        height = std::max(1, (S32)std::lround(height * scale));
    }

    const bool show_ui = data.has("show_ui") ? data["show_ui"].asBoolean() : false;
    const bool show_hud = data.has("show_hud") ? data["show_hud"].asBoolean() : false;
    const std::string directory = gDirUtilp->getExpandedFilename(LL_PATH_TEMP, "fs-mcp-snapshots");
    LLFile::mkdir(directory);
    const std::string path = directory + gDirUtilp->getDirDelimiter() +
        LLUUID::generateNewID().asString() + ".jpg";
    const bool ok = gViewerWindow->saveSnapshot(
        path,
        width,
        height,
        show_ui,
        show_hud,
        true,
        false,
        LLSnapshotModel::SNAPSHOT_TYPE_COLOR,
        LLSnapshotModel::SNAPSHOT_FORMAT_JPEG);
    if (!ok || !LLFile::isfile(path))
    {
        LLFile::remove(path);
        return response.error("Snapshot failed");
    }

    response["ok"] = true;
    response["path"] = path;
    response["width"] = width;
    response["height"] = height;
    response["mime"] = "image/jpeg";
    response["camera"] = camera_state();
}
