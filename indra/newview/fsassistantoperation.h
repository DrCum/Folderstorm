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

/** Viewer-owned preparation and execution context. Never accepted from HTTP. */
#ifndef FS_ASSISTANT_OPERATION_H
#define FS_ASSISTANT_OPERATION_H

#include "llsd.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct FSLinkReplacementSelection;

struct FSAssistantExecutionContext
{
    F64 deadline = 0.0; // totalTime() / 1e6; zero means no bridge deadline.
    std::function<bool()> allowed;
    // Texture uploads must not charge more than the price the user approved.
    S32 approvedSnapshotCost = -1;
};

struct FSAssistantPreparedOperation
{
    LLSD request;
    LLSD summary;
    LLSD validation;
    std::vector<std::string> requiredClasses;
    std::shared_ptr<const FSLinkReplacementSelection> linkSelection;
};

// Ordinary inventory, appearance, and camera summaries. Copy-plan/link
// preparation remains in LLInventoryListener, which owns their exact scope.
bool fs_prepare_assistant_operation(FSAssistantPreparedOperation& operation, std::string& error);
bool fs_validate_assistant_operation(const FSAssistantPreparedOperation& operation, std::string& error);

#endif
