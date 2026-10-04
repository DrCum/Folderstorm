/**
 * @file fspanelpreferenceworkspaces.h
 * @brief Manual workspace preferences and scoped confirmations
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
#ifndef FS_PANEL_PREFERENCE_WORKSPACES_H
#define FS_PANEL_PREFERENCE_WORKSPACES_H
#include "llpanel.h"
#include "lltimer.h"
#include "lluuid.h"

class FSPanelPreferenceWorkspaces : public LLPanel
{
public:
    bool postBuild() override;
    void draw() override;
private:
    void refresh();
    void action(const std::string& action);
    void confirm(const std::string& action, const std::string& name);
    std::string selected() const;
    std::string enteredName() const;
    LLTimer mRefreshTimer;
    unsigned long mObservedRevision = 0;
    std::string mFolderOptionWorkspace;
    LLUUID mFolderOptionAccount, mFolderOptionSession;
};
#endif
