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
#ifndef FS_FLOATER_WORKSPACES_H
#define FS_FLOATER_WORKSPACES_H

#include "llfloater.h"
#include "lltimer.h"
#include "lluuid.h"
#include <set>
#include <string>

class FSFloaterWorkspaces final : public LLFloater
{
public:
    explicit FSFloaterWorkspaces(const LLSD& key);
    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void draw() override;

private:
    void refresh(bool force = false);
    void updateButtons();
    void switchSelected();
    void toggleFavorite();
    void manageSelected();
    std::string selected() const;
    void addHeading(const std::string& label);
    void addEntry(const std::string& key, const std::string& label, bool layout);

    LLTimer mRefreshTimer;
    LLUUID mAccount, mSession;
    LLSD mProfiles, mLayouts, mFavoriteData;
    std::set<std::string> mFavorites;
    unsigned long mRevision = 0;
    bool mReady = false;
    bool mSwitchFailed = false;
};
#endif
