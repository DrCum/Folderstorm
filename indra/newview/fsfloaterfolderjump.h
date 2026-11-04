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
#ifndef FS_FLOATER_FOLDER_JUMP_H
#define FS_FLOATER_FOLDER_JUMP_H

#include "llfloater.h"
#include "llinventoryobserver.h"

#include <set>
#include <vector>

class LLFilterEditor;
class LLInventoryFilter;
class LLPanelMainInventory;
class LLScrollListCtrl;
class LLTextBox;

class FSFloaterFolderJump final : public LLFloater, public LLInventoryObserver
{
public:
    explicit FSFloaterFolderJump(const LLSD& key) : LLFloater(key) {}
    ~FSFloaterFolderJump() override;
    static void showFrom(LLPanelMainInventory& inventory);

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    void draw() override;
    bool handleKeyHere(KEY key, MASK mask) override;
    void changed(U32 mask) override;

private:
    struct Folder
    {
        LLUUID id;
        std::string name;
        std::string parent_path;
        std::string folded_name;
        std::string folded_path;
        bool top_level;
    };

    LLPanelMainInventory* owner() const;
    void beginIndex();
    void extendIndex(const LLInventoryFilter& filter);
    void refreshResults(bool select_first);
    void openSelected();
    void clearIndex();
    bool isReachable(const LLUUID& id, const LLInventoryFilter& filter) const;

    LLHandle<LLView> mOwner;
    LLUUID mSession;
    LLFilterEditor* mQuery = nullptr;
    LLScrollListCtrl* mResults = nullptr;
    LLTextBox* mStatus = nullptr;
    LLButton* mOpen = nullptr;
    std::vector<Folder> mFolders;
    std::set<LLUUID> mSeen;
    size_t mNextFolder = 0;
    size_t mNextChild = 0;
    std::string mRequestedQuery;
    S32 mGeneration = -1;
    bool mIndexDirty = true;
    bool mRefreshNeeded = true;
    bool mChooseFirst = true;
};

#endif
