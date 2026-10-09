/**
 * @file fsworkspacecontextadapter.h
 * @brief Bounded graphics, camera and additive HUD adapters
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
#ifndef FS_WORKSPACE_CONTEXT_ADAPTER_H
#define FS_WORKSPACE_CONTEXT_ADAPTER_H
#include "fsworkspacecontext.h"
#include "llsd.h"
namespace FSWorkspaceContext
{
Group captureGroup(bool camera);
bool resolveGroup(const Group& group, bool camera, Values& values, std::string& error, bool fresh = true);
LLSD controlValue(const Control& control, const std::vector<double>& values);
bool cameraAllowed();
std::vector<HUD> attachedHUDs();
bool hudAvailable(const HUD& hud, std::string& error);
bool hudWorn(const HUD& hud);
void attachHUD(const HUD& hud);
}
#endif
