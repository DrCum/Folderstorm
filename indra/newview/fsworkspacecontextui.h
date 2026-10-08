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
#ifndef FS_WORKSPACE_CONTEXT_UI_H
#define FS_WORKSPACE_CONTEXT_UI_H
#include "fsworkspacecontext.h"
class LLView;
namespace FSWorkspaceContextUI
{
void initialize(LLView* view);
void setOptions(LLView* view, const FSWorkspaceContext::Options& options);
FSWorkspaceContext::Options options(LLView* view);
}
#endif
