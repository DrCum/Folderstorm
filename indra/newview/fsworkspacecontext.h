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
#ifndef FS_WORKSPACE_CONTEXT_H
#define FS_WORKSPACE_CONTEXT_H
#include <cmath>
#include <map>
#include <string>
#include <vector>
namespace FSWorkspaceContext
{
enum class Mode { Off, Current, Preset };
enum class Kind { Boolean, Integer, Real, Vector };
struct Control { const char* name; Kind kind; double minimum, maximum; };
inline const std::vector<Control>& controls(bool camera)
{
    static const std::vector<Control> graphics = {
        {"RenderFarClip", Kind::Real, 16, 4096},
        {"RenderShadowDetail", Kind::Integer, 0, 2},
        {"RenderDeferredSSAO", Kind::Boolean, 0, 1},
        {"RenderReflectionsEnabled", Kind::Boolean, 0, 1},
        {"RenderReflectionProbeDetail", Kind::Integer, 0, 4},
        {"RenderAvatarMaxNonImpostors", Kind::Integer, 0, 256},
        {"RenderAvatarMaxComplexity", Kind::Integer, 0, 10000000},
        {"RenderMaxPartCount", Kind::Integer, 0, 262144},
        {"RenderVolumeLODFactor", Kind::Real, .125, 8},
        {"RenderAvatarLODFactor", Kind::Real, .125, 8},
        {"RenderGlow", Kind::Boolean, 0, 1},
        {"RenderDepthOfField", Kind::Boolean, 0, 1},
        {"FSLimitFramerate", Kind::Boolean, 0, 1},
        {"FramePerSecondLimit", Kind::Integer, 1, 1000}
    };
    static const std::vector<Control> view = {
        {"CameraAngle", Kind::Real, .1, 3.0},
        {"CameraPresetType", Kind::Integer, 0, 4},
        {"CameraZoomFraction", Kind::Real, 0, 1},
        {"CameraOffsetScale", Kind::Real, .01, 20},
        {"CameraOffsetRearView", Kind::Vector, -128, 128},
        {"FocusOffsetRearView", Kind::Vector, -128, 128},
        {"CameraPositionSmoothing", Kind::Real, 0, 10},
        {"ZoomTime", Kind::Real, 0, 10},
        {"TrackFocusObject", Kind::Boolean, 0, 1}
    };
    return camera ? view : graphics;
}
using Values = std::map<std::string, std::vector<double>>;
struct Group { Mode mode = Mode::Off; std::string preset; Values values; };
struct HUD { std::string item, name; int point = 0; };
constexpr int MAX_HUDS = 64;
struct Options { Group graphics, camera; std::vector<HUD> huds; };
inline bool validValue(const Control& control, const std::vector<double>& values)
{
    if (values.size() != (control.kind == Kind::Vector ? 3u : 1u)) return false;
    for (double value : values)
        if (!std::isfinite(value) || value < control.minimum || value > control.maximum ||
            ((control.kind == Kind::Integer || control.kind == Kind::Boolean) && std::floor(value) != value)) return false;
    return true;
}
inline Options liveOptions()
{
    Options options; options.graphics.mode = options.camera.mode = Mode::Current; return options;
}
}
#endif
