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
#ifndef FS_FLOATER_WORKSPACE_TOOLS_H
#define FS_FLOATER_WORKSPACE_TOOLS_H
#include "llfloater.h"
#include "lluuid.h"
#include <vector>

class FSFloaterWorkspaceTools final : public LLFloater
{
public:
    explicit FSFloaterWorkspaceTools(const LLSD& key) : LLFloater(key) {}
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
private:
    void refreshWindows();
    void arrange();
    void exportWorkspace();
    void importWorkspace();
    void reviewImport(const std::string& filename);
    void applyImport();
    unsigned long mPickerGeneration = 0, mImportRevision = 0;
    LLSD mAccepted, mOriginals;
    bool currentSession() const;
    LLUUID mAccount, mSession;
    std::vector<LLHandle<LLFloater>> mWindows;
};
#endif
