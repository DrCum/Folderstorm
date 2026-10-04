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
#ifndef FS_WORKSPACE_FAVORITES_BAR_H
#define FS_WORKSPACE_FAVORITES_BAR_H
#include "llpanel.h"
#include "lltimer.h"
#include "lluuid.h"
#include "fsworkspacequickaccess.h"

class LLMenuButton;
class FSWorkspaceFavoritesBar final : public LLPanel
{
public:
    static void install(LLView* navigation);
    ~FSWorkspaceFavoritesBar() override;
    bool postBuild() override;
private:
    static void idle(void* userdata);
    void refresh();
    void rebuild();
    void switchEntry(const std::string& key);
    LLHandle<LLView> mNavigation, mStack, mContainer;
    S32 mNavigationHeight = 0, mStackHeight = 0, mWidth = -1;
    bool mShown = false, mCanSwitch = false;
    LLUUID mAccount, mSession;
    LLSD mProfiles, mLayouts, mFavorites;
    LLTimer mRefreshTimer;
    LLMenuButton* mMore = nullptr;
    std::vector<FSWorkspaceQuickAccess::Entry> mEntries;
    std::vector<LLView*> mDynamicChildren;
};
#endif
