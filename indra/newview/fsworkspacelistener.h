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
#ifndef FS_WORKSPACE_LISTENER_H
#define FS_WORKSPACE_LISTENER_H
#include "fsassistantoperation.h"
void fs_initialize_workspace_api();
bool fs_prepare_workspace(FSAssistantPreparedOperation& operation, std::string& error);
bool fs_validate_workspace(const FSAssistantPreparedOperation& operation, std::string& error);
LLSD fs_execute_workspace(const FSAssistantPreparedOperation& operation, const FSAssistantExecutionContext& context, std::string& error);
#endif
