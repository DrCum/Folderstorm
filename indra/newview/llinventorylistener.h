/**
 * @file llinventorylistener.h
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */


#ifndef LL_LLINVENTORYLISTENER_H
#define LL_LLINVENTORYLISTENER_H

#include "lleventapi.h"
#include "llinventoryfunctions.h"

#include <map>

class LLInventoryListener : public LLEventAPI
{
public:
    LLInventoryListener();

private:
    void getItemsInfo(LLSD const &data);
    void getFolderTypeNames(LLSD const &data);
    void getAssetTypeNames(LLSD const &data);
    void getBasicFolderID(LLSD const &data);
    void getDirectDescendants(LLSD const &data);
    void collectDescendantsIf(LLSD const &data);

    void status(LLSD const& data);
    void get(LLSD const& data);
    void list(LLSD const& data);
    void search(LLSD const& data);
    void systemFolder(LLSD const& data);
    void createFolder(LLSD const& data);
    void move(LLSD const& data);
    void rename(LLSD const& data);
    void copy(LLSD const& data);
    void confirmCopy(LLSD const& data);

    void listAfterFetch(LLSD data);
    void searchAfterFetch(LLSD data);
    bool validateDestination(const LLUUID& id, std::string& error) const;

public:
    struct NoCopyMove
    {
        LLUUID item_id;
        LLUUID old_parent_id;
        LLUUID new_parent_id;
    };

    struct CopyPlan
    {
        LLUUID id;
        LLUUID source_id;
        LLUUID destination_root_id;
        F64 expires_at;
        std::vector<NoCopyMove> moves;
    };

private:
    void pruneCopyPlans();
    std::map<LLUUID, CopyPlan> mCopyPlans;
};

#endif // LL_LLINVENTORYLISTENER_H

