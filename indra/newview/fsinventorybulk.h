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

/** Viewer-owned inventory previews and bounded session history. */
#ifndef FS_INVENTORY_BULK_H
#define FS_INVENTORY_BULK_H
#include "llsd.h"
#include "fsassistantoperation.h"
#include <functional>
#include <string>

namespace FSInventoryBulk
{
// preview accepts the corresponding Event API request: previewBatchRename,
// previewBatchMove, or previewBulkUndo. Errors return an undefined LLSD.
LLSD preview(const LLSD& request, std::string& error);
LLSD plan(const std::string& plan_id, std::string& error);
LLSD history(const LLSD& request, std::string& error);
std::string requiredClass(const std::string& plan_id, std::string& error);
bool validate(const std::string& plan_id, std::string& error);
LLSD summary(const std::string& plan_id, std::string& error);
// Callback returns the standard executeBulkPlan result; trusted context is
// constructed by the bridge only after applying the plan's required class.
void execute(const std::string& plan_id, const FSAssistantExecutionContext& context,
             std::function<void(const LLSD&)> completed);
// The native review floater calls this only from its explicit Execute button.
// That button is one-request consent for Ask, never a saved permission change.
void executeNative(const std::string& plan_id, std::function<void(const LLSD&)> completed);
void clear();
void stop(const std::string& operation_id);
void permissionsChanged();
// Internal legacy adapter: prepares the exact same frozen plan without Read
// permission and preserves the legacy response fields on completion.
bool prepareLegacy(const LLSD& request, std::string& plan_id, std::string& error);
LLSD legacyResult(const LLSD& request, const LLSD& result);
}
#endif
