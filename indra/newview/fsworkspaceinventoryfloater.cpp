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
#include "fsworkspaceinventoryfloater.h"

FSWorkspaceInventoryFloater::FSWorkspaceInventoryFloater(const LLSD&)
    : LLFloaterSidePanelContainer(LLSD().with("is_secondary", true).with("workspace_secondary", true))
{
    setIsSingleInstance(true);
}

void FSWorkspaceInventoryFloater::closeFloater(bool app_quitting)
{
    // This registration is deliberately outside inventory/secondary_inventory.
    // Closing it cannot destroy a manually opened keyed Inventory window.
    LLFloaterSidePanelContainer::closeFloater(app_quitting);
    if (!LLView::getVisible()) destroy();
}
