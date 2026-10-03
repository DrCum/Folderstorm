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
#ifndef FS_WORKSPACE_INVENTORY_FLOATER_H
#define FS_WORKSPACE_INVENTORY_FLOATER_H

#include "llfloatersidepanelcontainer.h"

// A distinct registration/group gives the one owned extra its own controls.
class FSWorkspaceInventoryFloater : public LLFloaterSidePanelContainer
{
public:
    explicit FSWorkspaceInventoryFloater(const LLSD& key);
    void closeFloater(bool app_quitting = false) override;
};
#endif
