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

/** Scrollable per-request confirmation for the local assistant bridge. */
#ifndef FS_ASSISTANT_APPROVAL_H
#define FS_ASSISTANT_APPROVAL_H

#include "llfloater.h"
#include <functional>

class FSAssistantApproval final : public LLFloater
{
public:
    explicit FSAssistantApproval(const LLSD& key);
    bool postBuild() override;
    void onClose(bool app_quitting) override;
    void present(const LLSD& summary, std::function<void(bool)> callback);
    void dismiss();

private:
    void answer(bool allow);
    std::string describe(const LLSD& summary);
    void updatePage();
    std::function<void(bool)> mCallback;
    LLSD mSummary;
    S32 mPage = 0;
};

#endif
