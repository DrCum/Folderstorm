/**
 * @file llinventorylistener.cpp
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

#include "llviewerprecompiledheaders.h"

#include "llinventorylistener.h"

#include "llappearancemgr.h"
#include "llcallbacklist.h"
#include "llinventorycopypolicy.h"
#include "llinventoryfunctions.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llinventoryobserver.h"
#include "lltransutil.h"
#include "llviewerinventory.h"
#include "llwearableitemslist.h"
#include "llagent.h"
#include "llagentui.h"
#include "llapp.h"
#include "llassetstorage.h"
#include "llavatarnamecache.h"
#include "lldate.h"
#include "llfilesystem.h"
#include "lllandmark.h"
#include "lllandmarkactions.h"
#include "lllandmarklist.h"
#include "llnotecard.h"
#include "llwearabletype.h"
#include "llworldmap.h"
#include "llviewerassetstorage.h"
#include "llviewercontrol.h"
#include "llcameralistener.h"
#include "fssnapshotupload.h"
#include "fsassistantoperation.h"
#include "fslinkreplacement.h"
#include "llsdutil.h"
#include "llfloatersimplesnapshot.h"
#include "llfloaterperms.h"
#include "llviewermenufile.h"
#include "llviewerassetupload.h"
#include "llviewertexturelist.h"
#include "llimagej2c.h"
#include "llagentbenefits.h"
#include "llbuycurrencyhtml.h"
#include "llstatusbar.h"
#include "llviewerregion.h"
#include "llpermissions.h"
#include "llresourcedata.h"
#include "llstring.h"
#include "message.h"
#include "lltransactiontypes.h"
#include "rlvactions.h"
#include "rlvlocks.h"
#include "stringize.h"

#include <sstream>

#include <memory>
#include <set>

LLInventoryListener::LLInventoryListener()
  : LLEventAPI("LLInventory",
               "API for interactions with viewer Inventory items")
{
    add("getItemsInfo",
        "Return information about items or folders defined in [\"item_ids\"]:\n"
        "reply will contain [\"items\"] and [\"categories\"] result set keys",
        &LLInventoryListener::getItemsInfo,
        llsd::map("item_ids", LLSD(), "reply", LLSD()));

    add("getFolderTypeNames",
        "Return the table of folder type names, contained in [\"names\"]\n",
        &LLInventoryListener::getFolderTypeNames,
        llsd::map("reply", LLSD()));

    add("getAssetTypeNames",
        "Return the table of asset type names, contained in [\"names\"]\n",
        &LLInventoryListener::getAssetTypeNames,
        llsd::map("reply", LLSD()));

    add("getBasicFolderID",
        "Return the UUID of the folder by specified folder type name, for example:\n"
        "\"Textures\", \"My outfits\", \"Sounds\" and other basic folders which have associated type",
        &LLInventoryListener::getBasicFolderID,
        llsd::map("ft_name", LLSD(), "reply", LLSD()));

    add("getDirectDescendants",
        "Return result set keys [\"categories\"] and [\"items\"] for the direct\n"
        "descendants of the [\"folder_id\"]",
        &LLInventoryListener::getDirectDescendants,
        llsd::map("folder_id", LLSD(), "reply", LLSD()));

    add("collectDescendantsIf",
        "Return result set keys [\"categories\"] and [\"items\"] for the descendants\n"
        "of the [\"folder_id\"], if it passes specified filters:\n"
        "[\"name\"] is a substring of object's name,\n"
        "[\"desc\"] is a substring of object's description,\n"
        "asset [\"type\"] corresponds to the string name of the object's asset type\n"
        "[\"limit\"] sets item count limit in result set (default unlimited)\n"
        "[\"filter_links\"]: EXCLUDE_LINKS - don't show links, ONLY_LINKS - only show links, INCLUDE_LINKS - show links too (default)",
        &LLInventoryListener::collectDescendantsIf,
        llsd::map("folder_id", LLSD(), "reply", LLSD()));

    add("status", "Return login and inventory synchronization status",
        &LLInventoryListener::status, llsd::map("reply", LLSD()));
    add("get", "Fetch and return one inventory object by id",
        &LLInventoryListener::get, llsd::map("id", LLSD(), "reply", LLSD()));
    add("list", "Fetch and return the direct children of folder_id",
        &LLInventoryListener::list, llsd::map("folder_id", LLSD(), "reply", LLSD()));
    add("search", "Fetch recursively and search inventory",
        &LLInventoryListener::search, llsd::map("reply", LLSD()));
    add("systemFolder", "Return a system-folder UUID by type name",
        &LLInventoryListener::systemFolder,
        llsd::map("ft_name", LLSD(), "reply", LLSD()));
    add("createFolder", "Create a normal folder below parent_id",
        &LLInventoryListener::createFolder,
        llsd::map("parent_id", LLSD(), "name", LLSD(), "reply", LLSD()));
    add("move", "Move an inventory item or folder to dest_folder_id",
        &LLInventoryListener::move,
        llsd::map("id", LLSD(), "parent_id", LLSD(), "reply", LLSD()));
    add("rename", "Rename an inventory item or folder",
        &LLInventoryListener::rename,
        llsd::map("id", LLSD(), "name", LLSD(), "reply", LLSD()));
    add("copy", "Copy an item or folder; may return a no-copy move plan",
        &LLInventoryListener::copy,
        llsd::map("id", LLSD(), "parent_id", LLSD(), "reply", LLSD()));
    add("confirmCopy", "Confirm or decline no-copy moves from a completed copy",
        &LLInventoryListener::confirmCopy,
        llsd::map("plan_id", LLSD(), "confirm", LLSD(), "reply", LLSD()));
    add("types", "Return folder type names and asset type names",
        &LLInventoryListener::types, llsd::map("reply", LLSD()));
    add("getMany", "Fetch and return several inventory objects by id",
        &LLInventoryListener::getMany, llsd::map("ids", LLSD(), "reply", LLSD()));
    add("resolvePath", "Return every inventory UUID matching a path",
        &LLInventoryListener::resolvePath, llsd::map("path", LLSD(), "reply", LLSD()));
    add("protectedFolders", "List protected folder ids without changing them",
        &LLInventoryListener::protectedFolders, llsd::map("reply", LLSD()));
    add("setDescription", "Set an item or folder description",
        &LLInventoryListener::setDescription,
        llsd::map("id", LLSD(), "desc", LLSD(), "reply", LLSD()));
    add("setThumbnail", "Set an item or folder thumbnail asset id",
        &LLInventoryListener::setThumbnail,
        llsd::map("id", LLSD(), "thumbnail_id", LLSD(), "reply", LLSD()));
    add("snapshotUpload", "Capture a snapshot and set it as a folder or item image",
        &LLInventoryListener::snapshotUpload,
        llsd::map("id", LLSD(), "reply", LLSD()));
    add("setFavorite", "Set or clear the favorite flag",
        &LLInventoryListener::setFavorite,
        llsd::map("id", LLSD(), "favorite", LLSD(), "reply", LLSD()));
    add("link", "Create an inventory link",
        &LLInventoryListener::link,
        llsd::map("id", LLSD(), "parent_id", LLSD(), "reply", LLSD()));
    add("replaceLinks", "Replace links that point at source_id. Requires confirm",
        &LLInventoryListener::replaceLinks,
        llsd::map("source_id", LLSD(), "target_id", LLSD(), "reply", LLSD()));
    add("createItem", "Create a notecard, script, gesture, material, clothing, body part, or landmark",
        &LLInventoryListener::createItem,
        llsd::map("type", LLSD(), "reply", LLSD()));
    add("batchMove", "Move up to 50 inventory objects",
        &LLInventoryListener::batchMove, llsd::map("ids", LLSD(), "parent_id", LLSD(), "reply", LLSD()));
    add("batchRename", "Rename up to 50 inventory objects",
        &LLInventoryListener::batchRename, llsd::map("items", LLSD(), "reply", LLSD()));
    add("batchCopy", "Copy up to 50 inventory objects",
        &LLInventoryListener::batchCopy, llsd::map("ids", LLSD(), "parent_id", LLSD(), "reply", LLSD()));
    add("trash", "Move an inventory object into Trash",
        &LLInventoryListener::trash, llsd::map("id", LLSD(), "reply", LLSD()));
    add("restore", "Move an inventory object out of Trash into its type folder",
        &LLInventoryListener::restore, llsd::map("id", LLSD(), "reply", LLSD()));
    add("emptyTrash", "Permanently empty Trash. Requires confirm",
        &LLInventoryListener::emptyTrash, llsd::map("reply", LLSD()));
    add("purge", "Permanently delete one inventory object. Requires confirm",
        &LLInventoryListener::purge, llsd::map("id", LLSD(), "reply", LLSD()));
    add("readNotecard", "Read notecard text",
        &LLInventoryListener::readNotecard, llsd::map("id", LLSD(), "reply", LLSD()));
    add("readScript", "Read LSL script source",
        &LLInventoryListener::readScript, llsd::map("id", LLSD(), "reply", LLSD()));
    add("landmark", "Read a landmark region and global position",
        &LLInventoryListener::landmark, llsd::map("id", LLSD(), "reply", LLSD()));
    add("changes", "Return inventory changes since a generation cursor",
        &LLInventoryListener::changes, llsd::map("reply", LLSD()));
}

struct InventoryChangeFeed
{
    LLInventoryListener* owner = nullptr;
};

class InventoryChangeObserver final : public LLInventoryObserver
{
public:
    explicit InventoryChangeObserver(std::shared_ptr<InventoryChangeFeed> feed)
        : mFeed(std::move(feed))
    {
    }

    void changed(U32 mask) override
    {
        if (mFeed && mFeed->owner)
        {
            mFeed->owner->noteInventoryChanged(mask);
        }
    }

private:
    std::shared_ptr<InventoryChangeFeed> mFeed;
};

LLInventoryListener::~LLInventoryListener()
{
    if (mFeed)
    {
        mFeed->owner = nullptr;
    }
}

void LLInventoryListener::ensureObserving()
{
    if (mFeed)
    {
        return;
    }
    mFeed = std::make_shared<InventoryChangeFeed>();
    mFeed->owner = this;
    // The inventory model deletes leftover observers during cleanup.
    gInventory.addObserver(new InventoryChangeObserver(mFeed));
}

void LLInventoryListener::noteInventoryChanged(U32 mask)
{
    ++mGeneration;
    const LLInventoryModel::changed_items_t& ids = gInventory.getChangedIDs();
    if (ids.empty())
    {
        mChanges.push_back({mGeneration, LLUUID::null, mask});
    }
    else
    {
        for (const LLUUID& id : ids)
        {
            mChanges.push_back({mGeneration, id, mask});
        }
    }
    while (mChanges.size() > 200)
    {
        mChanges.pop_front();
    }
}

void add_cat_info(LLEventAPI::Response& response, LLViewerInventoryCategory* cat)
{
    response["categories"].insert(cat->getUUID().asString(),
                                  llsd::map("id", cat->getUUID(),
                                            "name", cat->getName(),
                                            "parent_id", cat->getParentUUID(),
                                            "type", LLFolderType::lookup(cat->getPreferredType()),
                                            "complete", gInventory.isCategoryComplete(cat->getUUID()),
                                            "version", cat->getVersion(),
                                            "descendent_count", cat->getDescendentCount(),
                                            "thumbnail_id", cat->getThumbnailUUID(),
                                            "path", make_path(cat)));

};

void add_item_info(LLEventAPI::Response& response, LLViewerInventoryItem* item)
{
    const LLPermissions& permissions = item->getPermissions();
    response["items"].insert(item->getUUID().asString(),
                             llsd::map("id", item->getUUID(),
                                       "name", item->getName(),
                                       "parent_id", item->getParentUUID(),
                                       "desc", item->getDescription(),
                                       "inv_type", LLInventoryType::lookup(item->getInventoryType()),
                                       "asset_type", LLAssetType::lookup(item->getType()),
                                       "creation_date", LLSD::Integer(item->getCreationDate()),
                                       "asset_id", item->getAssetUUID(),
                                       "is_link", item->getIsLinkType(),
                                       "linked_id", item->getLinkedUUID(),
                                       "complete", item->isFinished(),
                                       "copyable", item->getIsLinkType() ||
                                           permissions.allowCopyBy(gAgent.getID()),
                                       "modifiable", permissions.allowModifyBy(gAgent.getID()),
                                       "worn", get_is_item_worn(item),
                                       "thumbnail_id", item->getThumbnailUUID(),
                                       "path", make_path(item),
                                       "creator_id", item->getCreatorUUID(),
                                       "favorite", get_is_favorite(item),
                                       "transferable", permissions.allowTransferTo(gAgent.getID()),
                                       "wearable_type", item->isWearableType()
                                           ? LLWearableType::getInstance()->getTypeName(item->getWearableType())
                                           : std::string(),
                                       "sale_price", LLSD::Integer(item->getSaleInfo().getSalePrice()),
                                       "sale_type", LLSD::Integer(item->getSaleInfo().getSaleType()),
                                       "permissions", llsd::map(
                                           "base", LLSD::Integer(permissions.getMaskBase()),
                                           "owner", LLSD::Integer(permissions.getMaskOwner()),
                                           "group", LLSD::Integer(permissions.getMaskGroup()),
                                           "everyone", LLSD::Integer(permissions.getMaskEveryone()),
                                           "next_owner", LLSD::Integer(permissions.getMaskNextOwner()))));
}

void add_objects_info(LLEventAPI::Response& response, LLInventoryModel::cat_array_t cat_array, LLInventoryModel::item_array_t item_array)
{
    for (auto& p : item_array)
    {
        add_item_info(response, p);
    }
    for (auto& p : cat_array)
    {
        add_cat_info(response, p);
    }
}

void LLInventoryListener::getItemsInfo(LLSD const &data)
{
    Response response(LLSD(), data);
    uuid_vec_t ids = LLSDParam<uuid_vec_t>(data["item_ids"]);
    for (auto &it : ids)
    {
        LLViewerInventoryItem* item = gInventory.getItem(it);
        if (item)
        {
            add_item_info(response, item);
        }
        else
        {
            LLViewerInventoryCategory *cat = gInventory.getCategory(it);
            if (cat)
            {
                add_cat_info(response, cat);
            }
        }
    }
}

void LLInventoryListener::getFolderTypeNames(LLSD const &data)
{
    Response response(llsd::map("names", LLFolderType::getTypeNames()), data);
}

void LLInventoryListener::getAssetTypeNames(LLSD const &data)
{
    Response response(llsd::map("names", LLAssetType::getTypeNames()), data);
}

void LLInventoryListener::getBasicFolderID(LLSD const &data)
{
    Response response(llsd::map("id", gInventory.findCategoryUUIDForType(LLFolderType::lookup(data["ft_name"].asString()))), data);
}


void LLInventoryListener::getDirectDescendants(LLSD const &data)
{
    Response response(LLSD(), data);
    LLUUID folder_id(data["folder_id"].asUUID());
    LLViewerInventoryCategory* cat = gInventory.getCategory(folder_id);
    if (!cat)
    {
        return response.error(stringize("Folder ", std::quoted(data["folder_id"].asString()), " was not found"));
    }
    LLInventoryModel::cat_array_t* cats;
    LLInventoryModel::item_array_t* items;
    gInventory.getDirectDescendentsOf(folder_id, cats, items);
    if (!cats || !items)
    {
        return response.error("Folder contents are not available");
    }

    const S32 total = static_cast<S32>(cats->size() + items->size());
    S32 offset = std::max(data["offset"].asInteger(), 0);
    const S32 limit = data["limit"].asInteger();
    if (limit <= 0 && offset <= 0)
    {
        add_objects_info(response, *cats, *items);
        response["total_matches"] = total;
        response["truncated"] = false;
        return;
    }

    LLInventoryModel::cat_array_t selected_cats;
    LLInventoryModel::item_array_t selected_items;
    S32 index = 0;
    auto accept = [&]()
    {
        if (index++ < offset)
        {
            return false;
        }
        return limit <= 0 || static_cast<S32>(selected_cats.size() + selected_items.size()) < limit;
    };
    for (LLViewerInventoryCategory* category : *cats)
    {
        if (accept())
        {
            selected_cats.push_back(category);
        }
    }
    for (LLViewerInventoryItem* item : *items)
    {
        if (accept())
        {
            selected_items.push_back(item);
        }
    }
    add_objects_info(response, selected_cats, selected_items);
    response["total_matches"] = total;
    response["truncated"] = offset + static_cast<S32>(selected_cats.size() + selected_items.size()) < total;
}

struct LLFilteredCollector : public LLInventoryCollectFunctor
{
    enum EFilterLink
    {
        INCLUDE_LINKS,  // show links too
        EXCLUDE_LINKS,  // don't show links
        ONLY_LINKS      // only show links
    };

    LLFilteredCollector(LLSD const &data);
    virtual ~LLFilteredCollector() {}
    virtual bool operator()(LLInventoryCategory *cat, LLInventoryItem *item) override;
    virtual bool exceedsLimit() override
    {
        // mItemLimit == 0 means unlimited
        return (mItemLimit && mItemLimit <= mItemCount);
    }

  protected:
    bool checkagainstType(LLInventoryCategory *cat, LLInventoryItem *item);
    bool checkagainstNameDesc(LLInventoryCategory *cat, LLInventoryItem *item);
    bool checkagainstLinks(LLInventoryCategory *cat, LLInventoryItem *item);
    bool checkagainstExtended(LLInventoryCategory *cat, LLInventoryItem *item);

    LLAssetType::EType mType;
    LLInventoryType::EType mInvType;
    std::string mName;
    std::string mDesc;
    std::string mCreatorName;
    LLUUID mCreatorId;
    LLUUID mLinkedId;
    bool mHasWorn = false;
    bool mWorn = false;
    bool mHasCopyable = false;
    bool mCopyable = false;
    bool mHasModifiable = false;
    bool mModifiable = false;
    bool mHasFavorite = false;
    bool mFavorite = false;
    bool mHasCreatedAfter = false;
    bool mHasCreatedBefore = false;
    S32 mCreatedAfter = 0;
    S32 mCreatedBefore = 0;
    EFilterLink mLinkFilter;

    S32 mItemLimit;
    S32 mItemCount;
};

void LLInventoryListener::collectDescendantsIf(LLSD const &data)
{
    Response response(LLSD(), data);
    LLUUID folder_id(data["folder_id"].asUUID());
    LLViewerInventoryCategory *cat = gInventory.getCategory(folder_id);
    if (!cat)
    {
        return response.error(stringize("Folder ", std::quoted(data["folder_id"].asString()), " was not found"));
    }
    LLInventoryModel::cat_array_t  cat_array;
    LLInventoryModel::item_array_t item_array;

    LLFilteredCollector collector = LLFilteredCollector(data);

    gInventory.collectDescendentsIf(folder_id, cat_array, item_array, LLInventoryModel::EXCLUDE_TRASH, collector);

    add_objects_info(response, cat_array, item_array);
}

LLFilteredCollector::LLFilteredCollector(LLSD const &data) :
    mType(LLAssetType::EType::AT_UNKNOWN),
    mInvType(LLInventoryType::IT_NONE),
    mLinkFilter(INCLUDE_LINKS),
    mItemLimit(0),
    mItemCount(0)
{

    mName = data["name"].asString();
    mDesc = data["desc"].asString();
    mCreatorName = data["creator_name"].asString();
    mCreatorId = data["creator_id"].asUUID();
    mLinkedId = data["linked_id"].asUUID();
    if (data.has("worn"))
    {
        mHasWorn = true;
        mWorn = data["worn"].asBoolean();
    }
    if (data.has("copyable"))
    {
        mHasCopyable = true;
        mCopyable = data["copyable"].asBoolean();
    }
    if (data.has("modifiable"))
    {
        mHasModifiable = true;
        mModifiable = data["modifiable"].asBoolean();
    }
    if (data.has("favorite"))
    {
        mHasFavorite = true;
        mFavorite = data["favorite"].asBoolean();
    }
    if (data.has("created_after"))
    {
        mHasCreatedAfter = true;
        mCreatedAfter = data["created_after"].asInteger();
    }
    if (data.has("created_before"))
    {
        mHasCreatedBefore = true;
        mCreatedBefore = data["created_before"].asInteger();
    }
    if (data.has("inv_type"))
    {
        mInvType = LLInventoryType::lookup(data["inv_type"].asString());
    }

    if (data.has("type"))
    {
        mType = LLAssetType::lookup(data["type"]);
    }
    if (data.has("filter_links"))
    {
        if (data["filter_links"] == "EXCLUDE_LINKS")
        {
            mLinkFilter = EXCLUDE_LINKS;
        }
        else if (data["filter_links"] == "ONLY_LINKS")
        {
            mLinkFilter = ONLY_LINKS;
        }
    }
    if (data["limit"].isInteger())
    {
        mItemLimit = std::max(data["limit"].asInteger(), 1);
    }
}

bool LLFilteredCollector::operator()(LLInventoryCategory *cat, LLInventoryItem *item)
{
    bool passed = checkagainstType(cat, item);
    passed = passed && checkagainstNameDesc(cat, item);
    passed = passed && checkagainstLinks(cat, item);
    passed = passed && checkagainstExtended(cat, item);

    if (passed)
    {
        ++mItemCount;
    }
    return passed;
}

bool LLFilteredCollector::checkagainstNameDesc(LLInventoryCategory *cat, LLInventoryItem *item)
{
    std::string name, desc;
    bool passed(true);
    if (cat)
    {
        if (!mDesc.empty()) return false;
        name = cat->getName();
    }
    if (item)
    {
        name = item->getName();
        passed = (mDesc.empty() || (item->getDescription().find(mDesc) != std::string::npos));
    }

    return passed && (mName.empty() || name.find(mName) != std::string::npos);
}

bool LLFilteredCollector::checkagainstType(LLInventoryCategory *cat, LLInventoryItem *item)
{
    if (mType == LLAssetType::AT_UNKNOWN)
    {
        return true;
    }
    if (cat && (mType == LLAssetType::AT_CATEGORY))
    {
        return true;
    }
    if (item && item->getType() == mType)
    {
        return true;
    }
    return false;
}

bool LLFilteredCollector::checkagainstLinks(LLInventoryCategory *cat, LLInventoryItem *item)
{
    bool is_link = cat ? cat->getIsLinkType() : item->getIsLinkType();
    if (is_link && (mLinkFilter == EXCLUDE_LINKS))
        return false;
    if (!is_link && (mLinkFilter == ONLY_LINKS))
        return false;
    return true;
}

bool LLFilteredCollector::checkagainstExtended(LLInventoryCategory *cat, LLInventoryItem *item)
{
    const bool wants_item_filter =
        mInvType != LLInventoryType::IT_NONE ||
        mCreatorId.notNull() ||
        !mCreatorName.empty() ||
        mLinkedId.notNull() ||
        mHasWorn || mHasCopyable || mHasModifiable || mHasFavorite ||
        mHasCreatedAfter || mHasCreatedBefore;
    if (!wants_item_filter)
    {
        return true;
    }
    if (!item)
    {
        return false;
    }

    if (mInvType != LLInventoryType::IT_NONE && item->getInventoryType() != mInvType)
    {
        return false;
    }
    if (mCreatorId.notNull() && item->getCreatorUUID() != mCreatorId)
    {
        return false;
    }
    if (!mCreatorName.empty())
    {
        LLAvatarName avatar_name;
        if (!LLAvatarNameCache::get(item->getCreatorUUID(), &avatar_name) ||
            avatar_name.getCompleteName().find(mCreatorName) == std::string::npos)
        {
            return false;
        }
    }
    if (mLinkedId.notNull())
    {
        if (!item->getIsLinkType() || item->getLinkedUUID() != mLinkedId)
        {
            return false;
        }
    }
    if (mHasWorn && get_is_item_worn(item->getUUID()) != mWorn)
    {
        return false;
    }
    const LLPermissions& permissions = item->getPermissions();
    if (mHasCopyable &&
        (item->getIsLinkType() || permissions.allowCopyBy(gAgent.getID())) != mCopyable)
    {
        return false;
    }
    if (mHasModifiable && permissions.allowModifyBy(gAgent.getID()) != mModifiable)
    {
        return false;
    }
    if (mHasFavorite && get_is_favorite(item) != mFavorite)
    {
        return false;
    }
    if (mHasCreatedAfter && item->getCreationDate() < mCreatedAfter)
    {
        return false;
    }
    if (mHasCreatedBefore && item->getCreationDate() > mCreatedBefore)
    {
        return false;
    }
    return true;
}

namespace
{
bool inventory_tree_complete(const LLUUID& folder_id)
{
    if (!gInventory.isCategoryComplete(folder_id))
    {
        return false;
    }

    LLInventoryModel::cat_array_t* categories = nullptr;
    LLInventoryModel::item_array_t* items = nullptr;
    gInventory.getDirectDescendentsOf(folder_id, categories, items);
    if (!categories || !items)
    {
        return false;
    }
    for (const LLViewerInventoryCategory* category : *categories)
    {
        if (!inventory_tree_complete(category->getUUID()))
        {
            return false;
        }
    }
    return true;
}

class FSFetchItemsObserver final : public LLInventoryFetchItemsObserver
{
public:
    FSFetchItemsObserver(const uuid_vec_t& ids, nullary_func_type callback)
        : LLInventoryFetchItemsObserver(ids),
          mCallback(std::move(callback))
    {
    }

    void done() override
    {
        gInventory.removeObserver(this);
        mCallback();
        delete this;
    }

private:
    nullary_func_type mCallback;
};

void fetch_items_then(const uuid_vec_t& ids, nullary_func_type callback)
{
    auto* observer = new FSFetchItemsObserver(ids, std::move(callback));
    gInventory.addObserver(observer);
    observer->startFetch();
    if (observer->isFinished())
    {
        observer->done();
    }
}

bool object_is_agent_inventory(const LLUUID& id)
{
    return id == gInventory.getRootFolderID() ||
           gInventory.isObjectDescendentOf(id, gInventory.getRootFolderID());
}

LLSD object_summary(const LLInventoryObject* object)
{
    return llsd::map(
        "id", object->getUUID(),
        "name", object->getName(),
        "parent_id", object->getParentUUID(),
        "path", make_path(object));
}
}

void LLInventoryListener::status(LLSD const& data)
{
    ensureObserving();
    LLInventoryModelBackgroundFetch& fetch = LLInventoryModelBackgroundFetch::instance();
    const LLUUID root = gInventory.getRootFolderID();
    Response response(
        llsd::map(
            "usable", gInventory.isInventoryUsable(),
            "root_id", root,
            "library_root_id", gInventory.getLibraryRootFolderID(),
            "root_complete", root.notNull() && gInventory.isCategoryComplete(root),
            "inventory_fetch_started", fetch.inventoryFetchStarted(),
            "inventory_fetch_in_progress", fetch.inventoryFetchInProgress(),
            "inventory_fetch_completed", fetch.inventoryFetchCompleted(),
            "library_fetch_started", fetch.libraryFetchStarted(),
            "library_fetch_in_progress", fetch.libraryFetchInProgress(),
            "library_fetch_completed", fetch.libraryFetchCompleted(),
            "item_count", gInventory.getItemCount(),
            "category_count", gInventory.getCategoryCount(),
            "generation", mGeneration),
        data);
}

void LLInventoryListener::get(LLSD const& data)
{
    if (!gInventory.isInventoryUsable())
    {
        Response response(LLSD(), data);
        return response.error("Inventory is not usable; log in and wait for initialization");
    }

    const LLUUID id = data["id"].asUUID();
    if (LLViewerInventoryCategory* category = gInventory.getCategory(id))
    {
        Response response(LLSD(), data);
        add_cat_info(response, category);
        return;
    }
    if (!gInventory.getItem(id))
    {
        Response response(LLSD(), data);
        return response.error("Inventory object was not found");
    }

    LLSD fetch_data(data);
    fetch_data["item_ids"] = LLSD::emptyArray();
    fetch_data["item_ids"].append(id);
    fetch_items_then(
        uuid_vec_t{id},
        [this, fetch_data]() { getItemsInfo(fetch_data); });
}

void LLInventoryListener::list(LLSD const& data)
{
    if (!gInventory.isInventoryUsable())
    {
        Response response(LLSD(), data);
        return response.error("Inventory is not usable; log in and wait for initialization");
    }

    const LLUUID folder_id = data["folder_id"].asUUID();
    if (!gInventory.getCategory(folder_id))
    {
        Response response(LLSD(), data);
        return response.error("Folder was not found");
    }
    callAfterCategoryFetch(folder_id, [this, data]() { listAfterFetch(data); });
}

void LLInventoryListener::listAfterFetch(LLSD data)
{
    getDirectDescendants(data);
}

void LLInventoryListener::search(LLSD const& data)
{
    if (!gInventory.isInventoryUsable())
    {
        Response response(LLSD(), data);
        return response.error("Inventory is not usable; log in and wait for initialization");
    }
    LLUUID folder_id = data["folder_id"].asUUID();
    if (folder_id.isNull())
    {
        folder_id = gInventory.getRootFolderID();
    }
    if (!gInventory.getCategory(folder_id))
    {
        Response response(LLSD(), data);
        return response.error("Folder was not found");
    }

    LLInventoryModelBackgroundFetch::instance().start(folder_id, true);
    auto timer = std::make_shared<LLFrameTimer>();
    doOnIdleRepeating([this, data, folder_id, timer]()
    {
        if (inventory_tree_complete(folder_id))
        {
            searchAfterFetch(data);
            return true;
        }
        if (timer->getElapsedTimeF32() >= 60.f)
        {
            LLSD timed_data(data);
            timed_data["_fetch_timeout"] = true;
            searchAfterFetch(timed_data);
            return true;
        }
        return false;
    });
}

void LLInventoryListener::searchAfterFetch(LLSD data)
{
    Response response(LLSD(), data);
    LLUUID folder_id = data["folder_id"].asUUID();
    if (folder_id.isNull())
    {
        folder_id = gInventory.getRootFolderID();
    }
    LLInventoryModel::cat_array_t categories;
    LLInventoryModel::item_array_t items;
    LLFilteredCollector collector(data);
    gInventory.collectDescendentsIf(
        folder_id,
        categories,
        items,
        data["include_trash"].asBoolean()
            ? LLInventoryModel::INCLUDE_TRASH
            : LLInventoryModel::EXCLUDE_TRASH,
        collector);
    const S32 total_matches =
        static_cast<S32>(categories.size() + items.size());
    S32 remaining = data["limit"].asInteger();
    if (remaining <= 0)
    {
        remaining = total_matches;
    }
    for (LLViewerInventoryCategory* category : categories)
    {
        if (remaining-- <= 0) break;
        add_cat_info(response, category);
    }
    for (LLViewerInventoryItem* item : items)
    {
        if (remaining-- <= 0) break;
        add_item_info(response, item);
    }
    response["total_matches"] = total_matches;
    response["truncated"] =
        data["limit"].asInteger() > 0 &&
        total_matches > data["limit"].asInteger();
    response["complete"] = inventory_tree_complete(folder_id);
    if (data["_fetch_timeout"].asBoolean())
    {
        response.warn("Inventory fetch timed out; results may be incomplete");
    }
}

void LLInventoryListener::systemFolder(LLSD const& data)
{
    std::string name = data["ft_name"].asString();
    LLStringUtil::trim(name);
    LLStringUtil::toLower(name);
    LLStringUtil::replaceString(name, "_", " ");
    LLStringUtil::replaceString(name, "-", " ");

    static const std::map<std::string, std::string> aliases = {
        {"textures", "texture"},
        {"sounds", "sound"},
        {"calling cards", "callcard"},
        {"landmarks", "landmark"},
        {"objects", "object"},
        {"notecards", "notecard"},
        {"scripts", "lsltext"},
        {"body parts", "bodypart"},
        {"snapshots", "snapshot"},
        {"lost and found", "lstndfnd"},
        {"animations", "animatn"},
        {"gestures", "gesture"},
        {"favorites", "favorite"},
        {"current outfit", "current"},
        {"my outfits", "my_otfts"},
        {"marketplace listings", "merchant"},
        {"materials", "material"},
    };
    auto alias = aliases.find(name);
    if (alias != aliases.end())
    {
        name = alias->second;
    }

    LLSD normalized(data);
    normalized["ft_name"] = name;
    getBasicFolderID(normalized);
}

bool LLInventoryListener::validateDestination(const LLUUID& id, std::string& error) const
{
    LLViewerInventoryCategory* destination = gInventory.getCategory(id);
    if (!destination || !object_is_agent_inventory(id))
    {
        error = "Destination must be a folder in the agent inventory";
        return false;
    }
    const LLFolderType::EType type = destination->getPreferredType();
    if (type == LLFolderType::FT_CURRENT_OUTFIT ||
        type == LLFolderType::FT_MARKETPLACE_LISTINGS ||
        type == LLFolderType::FT_MARKETPLACE_STOCK ||
        type == LLFolderType::FT_LOST_AND_FOUND)
    {
        error = "That special destination is not supported by the automation API";
        return false;
    }
    return true;
}

void LLInventoryListener::createFolder(LLSD const& data)
{
    std::string error;
    const LLUUID parent_id = data["parent_id"].asUUID();
    std::string name = data["name"].asString();
    LLStringUtil::trim(name);
    if (!validateDestination(parent_id, error) || name.empty())
    {
        Response response(LLSD(), data);
        return response.error(error.empty() ? "Folder name must not be empty" : error);
    }

    gInventory.createNewCategory(
        parent_id,
        LLFolderType::FT_NONE,
        name,
        [data](const LLUUID& id)
        {
            LLSD reply;
            if (id.isNull())
            {
                reply["error"] = "Folder creation failed";
            }
            else
            {
                reply["id"] = id;
            }
            sendReply(reply, data);
        });
}

void LLInventoryListener::move(LLSD const& data)
{
    std::string error;
    const LLUUID id = data["id"].asUUID();
    LLUUID destination_id = data["parent_id"].asUUID();
    if (destination_id.isNull())
    {
        destination_id = data["dest_folder_id"].asUUID();
    }
    LLInventoryObject* object = gInventory.getObject(id);
    if (!object || !object_is_agent_inventory(id) ||
        !validateDestination(destination_id, error))
    {
        Response response(LLSD(), data);
        return response.error(!object ? "Inventory object was not found" :
                              !error.empty() ? error : "Only agent inventory can be moved");
    }
    if (id == destination_id ||
        (gInventory.getCategory(id) &&
         gInventory.isObjectDescendentOf(destination_id, id)))
    {
        Response response(LLSD(), data);
        return response.error("A folder cannot be moved into itself or its descendant");
    }

    if (LLViewerInventoryItem* item = gInventory.getItem(id))
    {
        if (!get_is_item_removable(&gInventory, id, false) ||
            (RlvActions::isRlvEnabled() &&
             !RlvFolderLocks::instance().canMoveItem(id, destination_id)))
        {
            Response response(LLSD(), data);
            return response.error("Item is protected or locked");
        }
        gInventory.changeItemParent(item, destination_id, false);
    }
    else if (LLViewerInventoryCategory* category = gInventory.getCategory(id))
    {
        if (!get_is_category_removable(&gInventory, id) ||
            (RlvActions::isRlvEnabled() &&
             !RlvFolderLocks::instance().canMoveFolder(id, destination_id)))
        {
            Response response(LLSD(), data);
            return response.error("Folder is protected or locked");
        }
        gInventory.changeCategoryParent(category, destination_id, false);
    }

    auto timer = std::make_shared<LLFrameTimer>();
    doOnIdleRepeating([data, id, destination_id, timer]()
    {
        LLInventoryObject* updated = gInventory.getObject(id);
        if (updated && updated->getParentUUID() == destination_id)
        {
            sendReply(llsd::map("ok", true, "object", object_summary(updated)), data);
            return true;
        }
        if (timer->getElapsedTimeF32() >= 30.f)
        {
            sendReply(llsd::map("error", "Move completion timed out"), data);
            return true;
        }
        return false;
    });
}

void LLInventoryListener::rename(LLSD const& data)
{
    const LLUUID id = data["id"].asUUID();
    std::string name = data["name"].asString();
    LLStringUtil::trim(name);
    LLInventoryObject* object = gInventory.getObject(id);
    if (!object || !object_is_agent_inventory(id) || name.empty())
    {
        Response response(LLSD(), data);
        return response.error(!object ? "Inventory object was not found" :
                              name.empty() ? "Name must not be empty" :
                              "Only agent inventory can be renamed");
    }

    bool allowed = false;
    if (LLViewerInventoryItem* item = gInventory.getItem(id))
    {
        allowed = item->isFinished() &&
                  item->getInventoryType() != LLInventoryType::IT_CALLINGCARD &&
                  item->getPermissions().allowModifyBy(gAgent.getID()) &&
                  (!RlvActions::isRlvEnabled() ||
                   RlvFolderLocks::instance().canRenameItem(id));
    }
    else
    {
        allowed = get_is_category_renameable(&gInventory, id);
    }
    if (!allowed)
    {
        Response response(LLSD(), data);
        return response.error("Inventory object cannot be renamed");
    }

    LLPointer<LLInventoryCallback> callback =
        new LLBoostFuncInventoryCallback([data, id](const LLUUID& result_id)
        {
            LLInventoryObject* updated = gInventory.getObject(id);
            if (result_id.isNull() || !updated)
            {
                sendReply(llsd::map("error", "Rename failed"), data);
            }
            else
            {
                sendReply(llsd::map("ok", true, "object", object_summary(updated)), data);
            }
        });
    LLSD updates;
    updates["name"] = name;
    if (gInventory.getItem(id))
    {
        update_inventory_item(id, updates, callback);
    }
    else
    {
        rename_category(&gInventory, id, name, callback);
    }
}

namespace
{
class FSInventoryCopyOperation
    : public std::enable_shared_from_this<FSInventoryCopyOperation>
{
public:
    using Complete = std::function<void(
        const LLUUID&,
        const std::vector<LLInventoryListener::NoCopyMove>&,
        const std::vector<std::string>&)>;

    FSInventoryCopyOperation(Complete complete, bool include_no_copy)
        : mComplete(std::move(complete)),
          mIncludeNoCopy(include_no_copy)
    {
    }

    void startFolder(LLViewerInventoryCategory* source,
                     const LLUUID& destination_parent,
                     const std::string& new_name)
    {
        scheduleCategory(source, destination_parent,
                         new_name.empty() ? source->getName() : new_name,
                         true);
    }

private:
    void scheduleCategory(LLViewerInventoryCategory* source,
                          const LLUUID& destination_parent,
                          const std::string& name,
                          bool root)
    {
        ++mPending;
        const LLUUID source_id = source->getUUID();
        gInventory.createNewCategory(
            destination_parent,
            source->getPreferredType() == LLFolderType::FT_OUTFIT
                ? LLFolderType::FT_OUTFIT : LLFolderType::FT_NONE,
            name,
            [self = shared_from_this(), source_id, root](const LLUUID& new_id)
            {
                if (new_id.isNull())
                {
                    self->mErrors.push_back("Unable to create destination folder for " +
                                            source_id.asString());
                }
                else
                {
                    self->mFolderMap[source_id] = new_id;
                    if (root)
                    {
                        self->mDestinationRoot = new_id;
                    }
                    self->scheduleContents(source_id, new_id);
                }
                self->taskDone();
            });
    }

    void scheduleContents(const LLUUID& source_id, const LLUUID& destination_id)
    {
        LLInventoryModel::cat_array_t* categories = nullptr;
        LLInventoryModel::item_array_t* items = nullptr;
        gInventory.getDirectDescendentsOf(source_id, categories, items);
        if (!categories || !items)
        {
            mErrors.push_back("Source folder was not fully available: " +
                              source_id.asString());
            return;
        }

        for (LLViewerInventoryCategory* category : *categories)
        {
            scheduleCategory(category, destination_id, category->getName(), false);
        }
        for (LLViewerInventoryItem* item : *items)
        {
            if (item->getIsLinkType())
            {
                scheduleLink(item, destination_id);
            }
            else if (!item->getPermissions().allowCopyBy(gAgent.getID()))
            {
                if (mIncludeNoCopy)
                {
                    mNoCopyMoves.push_back(
                        {item->getUUID(), item->getParentUUID(), destination_id});
                }
            }
            else
            {
                scheduleItem(item, destination_id);
            }
        }
    }

    void scheduleItem(LLViewerInventoryItem* item, const LLUUID& destination_id)
    {
        ++mPending;
        const LLUUID source_id = item->getUUID();
        LLPointer<LLInventoryCallback> callback =
            new LLBoostFuncInventoryCallback(
                [self = shared_from_this(), source_id](const LLUUID& new_id)
                {
                    if (new_id.isNull())
                    {
                        self->mErrors.push_back("Unable to copy item " +
                                                source_id.asString());
                    }
                    self->taskDone();
                });
        copy_inventory_item(
            gAgent.getID(),
            item->getPermissions().getOwner(),
            source_id,
            destination_id,
            std::string(),
            callback);
    }

    void scheduleLink(LLViewerInventoryItem* item, const LLUUID& destination_id)
    {
        ++mPending;
        const LLUUID source_id = item->getUUID();
        LLPointer<LLInventoryCallback> callback =
            new LLBoostFuncInventoryCallback(
                [self = shared_from_this(), source_id](const LLUUID& new_id)
                {
                    if (new_id.isNull())
                    {
                        self->mErrors.push_back("Unable to copy link " +
                                                source_id.asString());
                    }
                    self->taskDone();
                });
        link_inventory_object(destination_id, item->getLinkedUUID(), callback);
    }

    void taskDone()
    {
        if (--mPending == 0)
        {
            mComplete(mDestinationRoot, mNoCopyMoves, mErrors);
        }
    }

    Complete mComplete;
    bool mIncludeNoCopy;
    S32 mPending = 0;
    LLUUID mDestinationRoot;
    std::map<LLUUID, LLUUID> mFolderMap;
    std::vector<LLInventoryListener::NoCopyMove> mNoCopyMoves;
    std::vector<std::string> mErrors;
};

LLSD no_copy_moves_to_llsd(
    const std::vector<LLInventoryListener::NoCopyMove>& moves)
{
    LLSD result = LLSD::emptyArray();
    for (const auto& move : moves)
    {
        LLInventoryObject* item = gInventory.getObject(move.item_id);
        LLSD entry = llsd::map(
            "item_id", move.item_id,
            "old_parent_id", move.old_parent_id,
            "new_parent_id", move.new_parent_id);
        if (item)
        {
            entry["name"] = item->getName();
            entry["path"] = make_path(item);
        }
        result.append(entry);
    }
    return result;
}
}

void LLInventoryListener::copy(LLSD const& data)
{
    pruneCopyPlans();
    const LLUUID source_id = data["id"].asUUID();
    LLUUID destination_id = data["parent_id"].asUUID();
    if (destination_id.isNull())
    {
        destination_id = data["dest_folder_id"].asUUID();
    }
    LLInventoryObject* source = gInventory.getObject(source_id);
    std::string error;
    if (!source || !validateDestination(destination_id, error))
    {
        Response response(LLSD(), data);
        return response.error(source ? error : "Source inventory object was not found");
    }
    if (source_id == destination_id ||
        (gInventory.getCategory(source_id) &&
         gInventory.isObjectDescendentOf(destination_id, source_id)))
    {
        Response response(LLSD(), data);
        return response.error("A folder cannot be copied into itself or its descendant");
    }

    using namespace LLInventoryCopyPolicy;
    const Policy policy = parse(data["policy"].asString());
    if (policy == Policy::INVALID)
    {
        Response response(LLSD(), data);
        return response.error("Unknown copy policy");
    }

    if (LLViewerInventoryItem* item = gInventory.getItem(source_id))
    {
        const bool no_copy =
            !item->getIsLinkType() &&
            !item->getPermissions().allowCopyBy(gAgent.getID());
        const Action action = decide(policy, no_copy);
        if (action == Action::REJECT ||
            (no_copy && policy == Policy::COPYABLE_ONLY))
        {
            Response response(LLSD(), data);
            response["no_copy_items"] = LLSD::emptyArray();
            response["no_copy_items"].append(object_summary(item));
            return response.error("Item cannot be copied under the selected policy");
        }
        if (action == Action::COPY_AND_CONFIRM_MOVES)
        {
            CopyPlan plan;
            plan.id.generate();
            plan.source_id = source_id;
            plan.destination_root_id = destination_id;
            plan.expires_at = LLDate::now().secondsSinceEpoch() + 600.0;
            plan.moves.push_back({source_id, item->getParentUUID(), destination_id});
            mCopyPlans[plan.id] = plan;
            Response response(LLSD(), data);
            response["needs_confirmation"] = true;
            response["plan_id"] = plan.id;
            response["proposed_moves"] = no_copy_moves_to_llsd(plan.moves);
            response["warning"] =
                "Confirming will move the unique item out of its source folder";
            return;
        }

        LLPointer<LLInventoryCallback> callback =
            new LLBoostFuncInventoryCallback([data](const LLUUID& new_id)
            {
                if (new_id.isNull())
                {
                    sendReply(llsd::map("error", "Item copy failed"), data);
                }
                else
                {
                    sendReply(llsd::map("ok", true, "new_id", new_id), data);
                }
            });
        if (item->getIsLinkType())
        {
            link_inventory_object(destination_id, item->getLinkedUUID(), callback);
        }
        else
        {
            copy_inventory_item(
                gAgent.getID(),
                item->getPermissions().getOwner(),
                source_id,
                destination_id,
                data["new_name"].asString(),
                callback);
        }
        return;
    }

    auto begin_copy = [this, data, source_id, destination_id, policy]()
    {
        LLViewerInventoryCategory* category = gInventory.getCategory(source_id);
        if (!category || !inventory_tree_complete(source_id))
        {
            Response response(LLSD(), data);
            return response.error("Source folder could not be fetched completely");
        }

        LLInventoryModel::cat_array_t categories;
        LLInventoryModel::item_array_t items;
        gInventory.collectDescendents(
            source_id, categories, items, LLInventoryModel::INCLUDE_TRASH);
        bool has_no_copy = false;
        LLSD no_copy_items = LLSD::emptyArray();
        for (LLViewerInventoryItem* item : items)
        {
            if (!item->getIsLinkType() &&
                !item->getPermissions().allowCopyBy(gAgent.getID()))
            {
                has_no_copy = true;
                no_copy_items.append(object_summary(item));
            }
        }
        if (LLInventoryCopyPolicy::decide(policy, has_no_copy) ==
            LLInventoryCopyPolicy::Action::REJECT)
        {
            Response response(LLSD(), data);
            response["no_copy_items"] = no_copy_items;
            return response.error("Folder contains no-copy items");
        }

        const bool include_no_copy =
            policy == LLInventoryCopyPolicy::Policy::INCLUDE_NO_COPY;
        auto operation = std::make_shared<FSInventoryCopyOperation>(
            [this, data, source_id](
                const LLUUID& new_root,
                const std::vector<NoCopyMove>& moves,
                const std::vector<std::string>& errors)
            {
                LLSD reply;
                reply["new_id"] = new_root;
                reply["ok"] = errors.empty();
                for (const std::string& error_message : errors)
                {
                    reply["errors"].append(error_message);
                }
                if (!moves.empty())
                {
                    CopyPlan plan;
                    plan.id.generate();
                    plan.source_id = source_id;
                    plan.destination_root_id = new_root;
                    plan.expires_at = LLDate::now().secondsSinceEpoch() + 600.0;
                    plan.moves = moves;
                    mCopyPlans[plan.id] = plan;
                    reply["needs_confirmation"] = true;
                    reply["plan_id"] = plan.id;
                    reply["proposed_moves"] = no_copy_moves_to_llsd(moves);
                    reply["warning"] =
                        "Confirming will remove unique no-copy items from the source";
                }
                sendReply(reply, data);
            },
            include_no_copy);
        operation->startFolder(
            category, destination_id, data["new_name"].asString());
    };

    if (inventory_tree_complete(source_id))
    {
        begin_copy();
        return;
    }

    LLInventoryModelBackgroundFetch::instance().start(source_id, true);
    auto timer = std::make_shared<LLFrameTimer>();
    doOnIdleRepeating([begin_copy, source_id, data, timer]()
    {
        if (inventory_tree_complete(source_id))
        {
            begin_copy();
            return true;
        }
        if (timer->getElapsedTimeF32() >= 60.f)
        {
            sendReply(llsd::map("error", "Source folder fetch timed out"), data);
            return true;
        }
        return false;
    });
}

void LLInventoryListener::confirmCopy(LLSD const& data)
{
    pruneCopyPlans();
    const LLUUID plan_id = data["plan_id"].asUUID();
    auto found = mCopyPlans.find(plan_id);
    if (found == mCopyPlans.end())
    {
        Response response(LLSD(), data);
        return response.error("Copy plan is missing or expired");
    }

    CopyPlan plan = found->second;
    if (!data["confirm"].asBoolean())
    {
        mCopyPlans.erase(found);
        Response response(
            llsd::map(
                "ok", true,
                "confirmed", false,
                "new_id", plan.destination_root_id,
                "message", "No-copy items were left in the source"),
            data);
        return;
    }

    for (const NoCopyMove& move : plan.moves)
    {
        LLViewerInventoryItem* item = gInventory.getItem(move.item_id);
        std::string error;
        if (!item ||
            item->getParentUUID() != move.old_parent_id ||
            item->getPermissions().allowCopyBy(gAgent.getID()) ||
            !validateDestination(move.new_parent_id, error) ||
            !get_is_item_removable(&gInventory, move.item_id, false) ||
            (RlvActions::isRlvEnabled() &&
             !RlvFolderLocks::instance().canMoveItem(
                 move.item_id, move.new_parent_id)))
        {
            Response response(LLSD(), data);
            return response.error(
                "Copy plan is stale or a proposed no-copy move is no longer allowed");
        }
    }

    LLSD outcomes = LLSD::emptyArray();
    for (const NoCopyMove& move : plan.moves)
    {
        LLViewerInventoryItem* item = gInventory.getItem(move.item_id);
        gInventory.changeItemParent(item, move.new_parent_id, false);
        outcomes.append(llsd::map(
            "item_id", move.item_id,
            "old_parent_id", move.old_parent_id,
            "new_parent_id", move.new_parent_id,
            "ok", item->getParentUUID() == move.new_parent_id));
    }
    mCopyPlans.erase(found);
    Response response(
        llsd::map(
            "ok", true,
            "confirmed", true,
            "new_id", plan.destination_root_id,
            "outcomes", outcomes),
        data);
}

bool LLInventoryListener::prepareAssistantCopyConfirmation(const LLSD& data,
    LLSD& summary, LLSD& validation, std::string& error) const
{
    const LLUUID plan_id = data["plan_id"].asUUID();
    auto found = mCopyPlans.find(plan_id);
    if (found == mCopyPlans.end() || found->second.expires_at <= LLDate::now().secondsSinceEpoch())
    {
        error = "Copy plan is missing or expired";
        return false;
    }
    const CopyPlan& plan = found->second;
    LLSD subjects = LLSD::emptyArray();
    for (const NoCopyMove& move : plan.moves)
    {
        LLViewerInventoryItem* item = gInventory.getItem(move.item_id);
        LLInventoryObject* source = gInventory.getObject(move.old_parent_id);
        LLInventoryObject* destination = gInventory.getObject(move.new_parent_id);
        if (!item || !source || !destination ||
            item->getParentUUID() != move.old_parent_id ||
            item->getPermissions().allowCopyBy(gAgent.getID()) ||
            !validateDestination(move.new_parent_id, error) ||
            !get_is_item_removable(&gInventory, move.item_id, false) ||
            (RlvActions::isRlvEnabled() &&
             !RlvFolderLocks::instance().canMoveItem(move.item_id, move.new_parent_id)))
        {
            error = "Copy plan is stale or a proposed no-copy move is no longer allowed";
            return false;
        }
        LLSD subject = object_summary(item);
        subject["source"] = object_summary(source);
        subject["destination"] = object_summary(destination);
        subjects.append(subject);
    }
    summary = llsd::map("action_key", "confirmCopy", "subject_count", static_cast<S32>(plan.moves.size()),
        "subjects", subjects, "consequences", "These unique items will leave their source folders.");
    validation = llsd::map("plan_id", plan_id, "expires_at", plan.expires_at,
        "session_id", gAgent.getSessionID(), "subjects", subjects);
    return true;
}

bool LLInventoryListener::validateAssistantCopyConfirmation(const LLSD& data,
    const LLSD& validation, std::string& error) const
{
    LLSD current_summary, current_validation;
    if (!prepareAssistantCopyConfirmation(data, current_summary, current_validation, error)) return false;
    if (!llsd_equals(validation, current_validation))
    {
        error = "The copy plan changed; request fresh approval";
        return false;
    }
    return true;
}

void LLInventoryListener::pruneCopyPlans()
{
    const F64 now = LLDate::now().secondsSinceEpoch();
    for (auto it = mCopyPlans.begin(); it != mCopyPlans.end();)
    {
        if (it->second.expires_at <= now)
        {
            it = mCopyPlans.erase(it);
        }
        else
        {
            ++it;
        }
    }
    while (mCopyPlans.size() > 64)
    {
        mCopyPlans.erase(mCopyPlans.begin());
    }
}

namespace
{
const S32 kBatchLimit = 50;
const std::size_t kTextCap = 65536;

std::vector<LLUUID> uuid_list(const LLSD& value)
{
    std::vector<LLUUID> ids;
    if (value.isArray())
    {
        for (const LLSD& entry : llsd::inArray(value))
        {
            const LLUUID id = entry.asUUID();
            if (id.notNull())
            {
                ids.push_back(id);
            }
        }
    }
    else
    {
        const LLUUID id = value.asUUID();
        if (id.notNull())
        {
            ids.push_back(id);
        }
    }
    return ids;
}

std::string capped_text(const char* data, S32 length, bool& truncated)
{
    truncated = false;
    if (length < 0)
    {
        length = 0;
    }
    if (static_cast<std::size_t>(length) > kTextCap)
    {
        length = static_cast<S32>(kTextCap);
        truncated = true;
    }
    return std::string(data, data + length);
}

struct AssetRead
{
    LLSD request;
    LLUUID item_id;
    LLUUID asset_id;
    bool notecard = false;
};

void finish_asset_read(AssetRead* raw, S32 status)
{
    std::unique_ptr<AssetRead> request(raw);
    LLViewerInventoryItem* item = gInventory.getItem(request->item_id);
    const bool stale = !item || item->getAssetUUID() != request->asset_id;
    if (status != 0)
    {
        sendReply(llsd::map(
            "error", "Asset download failed",
            "id", request->item_id,
            "asset_id", request->asset_id,
            "stale", stale), request->request);
        return;
    }

    const LLAssetType::EType type = request->notecard ? LLAssetType::AT_NOTECARD : LLAssetType::AT_LSL_TEXT;
    LLFileSystem file(request->asset_id, type);
    const S32 file_length = file.getSize();
    if (file_length <= 0)
    {
        sendReply(llsd::map(
            "error", "Asset file was empty",
            "id", request->item_id,
            "asset_id", request->asset_id,
            "stale", true), request->request);
        return;
    }

    std::vector<char> buffer(file_length + 1, 0);
    file.read(reinterpret_cast<U8*>(&buffer[0]), file_length);
    bool truncated = false;
    std::string text;
    if (request->notecard)
    {
        std::stringstream stream(std::string(buffer.data(), buffer.data() + file_length));
        LLNotecard notecard;
        if (!notecard.importStream(stream))
        {
            sendReply(llsd::map(
                "error", "Notecard could not be parsed",
                "id", request->item_id,
                "asset_id", request->asset_id,
                "stale", true), request->request);
            return;
        }
        text = capped_text(notecard.getText().data(), static_cast<S32>(notecard.getText().size()), truncated);
    }
    else
    {
        text = capped_text(buffer.data(), file_length, truncated);
    }

    LLSD reply;
    reply["ok"] = true;
    reply["id"] = request->item_id;
    reply["asset_id"] = request->asset_id;
    reply["text"] = text;
    reply["truncated"] = truncated;
    reply["stale"] = stale;
    sendReply(reply, request->request);
}

void on_asset_read(const LLUUID&, LLAssetType::EType, void* user_data, S32 status, LLExtStat)
{
    finish_asset_read(static_cast<AssetRead*>(user_data), status);
}

void reply_landmark(const LLSD& request, const LLUUID& item_id, const LLUUID& asset_id, LLLandmark* landmark)
{
    LLViewerInventoryItem* item = gInventory.getItem(item_id);
    const bool stale = !item || item->getAssetUUID() != asset_id;
    if (!landmark)
    {
        sendReply(llsd::map(
            "error", "Landmark asset was not found",
            "id", item_id,
            "asset_id", asset_id,
            "stale", true), request);
        return;
    }

    LLSD reply;
    reply["ok"] = true;
    reply["id"] = item_id;
    reply["asset_id"] = asset_id;
    reply["stale"] = stale;
    LLUUID region_id;
    if (landmark->getRegionID(region_id))
    {
        reply["region_id"] = region_id;
    }
    LLVector3d position;
    if (landmark->getGlobalPos(position))
    {
        reply["global_pos"] = llsd::array(position.mdV[VX], position.mdV[VY], position.mdV[VZ]);
        std::string region_name;
        if (LLWorldMap::getInstance()->simNameFromPosGlobal(position, region_name))
        {
            reply["region_name"] = region_name;
        }
    }
    else
    {
        reply["pending_position"] = true;
    }
    sendReply(reply, request);
}
}

bool LLInventoryListener::requireConfirm(LLSD const& data, Response& response) const
{
    if (data["confirm"].asBoolean())
    {
        return true;
    }
    response.error("Confirmation is required");
    return false;
}

void LLInventoryListener::types(LLSD const& data)
{
    Response response(llsd::map(
        "folder_types", LLFolderType::getTypeNames(),
        "asset_types", LLAssetType::getTypeNames()), data);
}

void LLInventoryListener::getMany(LLSD const& data)
{
    if (!gInventory.isInventoryUsable())
    {
        Response response(LLSD(), data);
        return response.error("Inventory is not usable; log in and wait for initialization");
    }
    const std::vector<LLUUID> ids = uuid_list(data.has("ids") ? data["ids"] : data["item_ids"]);
    if (ids.empty() || static_cast<S32>(ids.size()) > kBatchLimit)
    {
        Response response(LLSD(), data);
        return response.error("ids must contain between 1 and 50 UUIDs");
    }
    LLSD fetch(data);
    fetch["item_ids"] = LLSD::emptyArray();
    for (const LLUUID& id : ids)
    {
        fetch["item_ids"].append(id);
    }
    fetch_items_then(ids, [this, fetch]() { getItemsInfo(fetch); });
}

void LLInventoryListener::resolvePath(LLSD const& data)
{
    Response response(LLSD(), data);
    if (!gInventory.isInventoryUsable())
    {
        return response.error("Inventory is not usable; log in and wait for initialization");
    }
    std::string path = data["path"].asString();
    LLStringUtil::trim(path);
    while (!path.empty() && (path.front() == '/' || path.front() == '\\'))
    {
        path.erase(path.begin());
    }
    std::vector<std::string> parts;
    std::string part;
    for (char character : path)
    {
        if (character == '/' || character == '\\')
        {
            if (!part.empty())
            {
                parts.push_back(part);
                part.clear();
            }
        }
        else
        {
            part.push_back(character);
        }
    }
    if (!part.empty())
    {
        parts.push_back(part);
    }
    if (parts.empty())
    {
        return response.error("path is required");
    }

    std::vector<LLUUID> current;
    const LLUUID start = data["folder_id"].asUUID();
    current.push_back(start.notNull() ? start : gInventory.getRootFolderID());
    for (const std::string& name : parts)
    {
        std::vector<LLUUID> next;
        for (const LLUUID& folder_id : current)
        {
            LLInventoryModel::cat_array_t* cats = nullptr;
            LLInventoryModel::item_array_t* items = nullptr;
            gInventory.getDirectDescendentsOf(folder_id, cats, items);
            if (cats)
            {
                for (LLViewerInventoryCategory* category : *cats)
                {
                    if (category && category->getName() == name)
                    {
                        next.push_back(category->getUUID());
                    }
                }
            }
            if (items && &name == &parts.back())
            {
                for (LLViewerInventoryItem* item : *items)
                {
                    if (item && item->getName() == name)
                    {
                        next.push_back(item->getUUID());
                    }
                }
            }
        }
        current.swap(next);
        if (current.empty())
        {
            break;
        }
    }

    response["matches"] = LLSD::emptyArray();
    for (const LLUUID& id : current)
    {
        if (LLInventoryObject* object = gInventory.getObject(id))
        {
            response["matches"].append(object_summary(object));
        }
    }
    response["count"] = static_cast<LLSD::Integer>(response["matches"].size());
    response["ambiguous"] = response["matches"].size() > 1;
    response["ok"] = response["matches"].size() > 0;
}

void LLInventoryListener::protectedFolders(LLSD const& data)
{
    ensureObserving();
    Response response(LLSD(), data);
    const LLSD stored = gSavedPerAccountSettings.getLLSD("FSProtectedFolders");
    response["folders"] = LLSD::emptyArray();
    if (!stored.isArray())
    {
        return;
    }
    for (const LLSD& entry : llsd::inArray(stored))
    {
        const LLUUID id = entry.asUUID();
        LLSD folder = llsd::map("id", id);
        if (LLInventoryObject* object = gInventory.getObject(id))
        {
            folder["name"] = object->getName();
            folder["path"] = make_path(object);
        }
        response["folders"].append(folder);
    }
}

void LLInventoryListener::setDescription(LLSD const& data)
{
    const LLUUID id = data["id"].asUUID();
    LLInventoryObject* object = gInventory.getObject(id);
    if (!object || !object_is_agent_inventory(id))
    {
        Response response(LLSD(), data);
        return response.error(object ? "Only agent inventory can be edited" : "Inventory object was not found");
    }
    if (LLViewerInventoryItem* item = gInventory.getItem(id))
    {
        if (!item->getPermissions().allowModifyBy(gAgent.getID()) ||
            item->getInventoryType() == LLInventoryType::IT_CALLINGCARD)
        {
            Response response(LLSD(), data);
            return response.error("Inventory object cannot be edited");
        }
    }
    LLSD updates;
    updates["desc"] = data["desc"].asString();
    LLPointer<LLInventoryCallback> callback =
        new LLBoostFuncInventoryCallback([data, id](const LLUUID&)
        {
            LLInventoryObject* updated = gInventory.getObject(id);
            if (!updated)
            {
                sendReply(llsd::map("error", "Description update failed"), data);
            }
            else
            {
                sendReply(llsd::map("ok", true, "object", object_summary(updated)), data);
            }
        });
    if (gInventory.getItem(id))
    {
        update_inventory_item(id, updates, callback);
    }
    else
    {
        update_inventory_category(id, updates, callback);
    }
}

void LLInventoryListener::setThumbnail(LLSD const& data)
{
    const LLUUID id = data["id"].asUUID();
    LLInventoryObject* object = gInventory.getObject(id);
    if (!object || !object_is_agent_inventory(id))
    {
        Response response(LLSD(), data);
        return response.error(object ? "Only agent inventory can be edited" : "Inventory object was not found");
    }
    const LLUUID thumbnail_id = data["thumbnail_id"].asUUID();
    LLSD updates;
    if (thumbnail_id.notNull())
    {
        updates["thumbnail"] = LLSD().with("asset_id", thumbnail_id.asString());
    }
    else
    {
        updates["thumbnail"] = LLSD();
    }
    LLPointer<LLInventoryCallback> callback =
        new LLBoostFuncInventoryCallback([data, id](const LLUUID&)
        {
            LLInventoryObject* updated = gInventory.getObject(id);
            sendReply(updated ? llsd::map("ok", true, "object", object_summary(updated))
                              : llsd::map("error", "Thumbnail update failed"), data);
        });
    if (gInventory.getItem(id))
    {
        update_inventory_item(id, updates, callback);
    }
    else
    {
        update_inventory_category(id, updates, callback);
    }
}

namespace
{
struct SnapshotTextureHold
{
    LLSD request;
    LLUUID inventory_id;
    S32 cost = 0;
    bool sent = false;
};

void reply_snapshot_texture(const std::shared_ptr<SnapshotTextureHold>& hold, const LLSD& body)
{
    if (!hold || hold->sent)
    {
        return;
    }
    hold->sent = true;
    sendReply(body, hold->request);
}

void assign_uploaded_thumbnail(const std::shared_ptr<SnapshotTextureHold>& hold, const LLUUID& asset_id)
{
    if (!hold)
    {
        return;
    }
    if (asset_id.isNull())
    {
        reply_snapshot_texture(hold, llsd::map("error", "Texture upload failed"));
        return;
    }
    LLSD updates;
    updates["thumbnail"] = LLSD().with("asset_id", asset_id.asString());
    LLPointer<LLInventoryCallback> callback =
        new LLBoostFuncInventoryCallback([hold, asset_id](const LLUUID&)
        {
            if (!gInventory.getObject(hold->inventory_id))
            {
                reply_snapshot_texture(hold, llsd::map("error", "Thumbnail update failed"));
                return;
            }
            reply_snapshot_texture(hold, llsd::map(
                "ok", true,
                "cost", LLSD::Integer(hold->cost),
                "destination", "texture",
                "asset_id", asset_id,
                "thumbnail_id", asset_id,
                "id", hold->inventory_id));
        });
    if (gInventory.getItem(hold->inventory_id))
    {
        update_inventory_item(hold->inventory_id, updates, callback);
    }
    else
    {
        update_inventory_category(hold->inventory_id, updates, callback);
    }
}

void snapshot_texture_legacy_callback(const LLUUID& asset_id, void* user_data, S32 result, LLExtStat)
{
    LLResourceData* data = static_cast<LLResourceData*>(user_data);
    std::shared_ptr<SnapshotTextureHold> hold;
    if (data && data->mUserData)
    {
        auto* boxed = static_cast<std::shared_ptr<SnapshotTextureHold>*>(data->mUserData);
        hold = *boxed;
        delete boxed;
        data->mUserData = nullptr;
    }
    if (!hold || !data || result < 0)
    {
        reply_snapshot_texture(hold, llsd::map("error", "Texture upload failed"));
        delete data;
        return;
    }

    const S32 expected_upload_cost = data->mExpectedUploadCost;
    bool balance_ok = true;
    LLViewerRegion* region = gAgent.getRegion();
    if (!can_afford_transaction(expected_upload_cost))
    {
        LLBuyCurrencyHTML::openCurrencyFloater("", expected_upload_cost);
        balance_ok = false;
    }
    else if (region)
    {
        gStatusBar->debitBalance(expected_upload_cost);
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_MoneyTransferRequest);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_MoneyData);
        msg->addUUIDFast(_PREHASH_SourceID, gAgent.getID());
        msg->addUUIDFast(_PREHASH_DestID, LLUUID::null);
        msg->addU8("Flags", 0);
        msg->addS32Fast(_PREHASH_Amount, expected_upload_cost);
        msg->addU8Fast(_PREHASH_AggregatePermNextOwner, (U8)LLAggregatePermissions::AP_EMPTY);
        msg->addU8Fast(_PREHASH_AggregatePermInventory, (U8)LLAggregatePermissions::AP_EMPTY);
        msg->addS32Fast(_PREHASH_TransactionType, TRANS_UPLOAD_CHARGE);
        msg->addStringFast(_PREHASH_Description, NULL);
        msg->sendReliable(region->getHost());
    }
    if (!balance_ok)
    {
        reply_snapshot_texture(hold, llsd::map("error", "Not enough L$ to upload the texture"));
        delete data;
        return;
    }

    LLFolderType::EType dest_loc = data->mPreferredLocation == LLFolderType::FT_NONE
        ? LLFolderType::assetTypeToFolderType(data->mAssetInfo.mType)
        : data->mPreferredLocation;
    const LLUUID folder_id = gInventory.findCategoryUUIDForType(dest_loc);
    if (folder_id.isNull())
    {
        reply_snapshot_texture(hold, llsd::map("error", "Texture upload failed"));
        delete data;
        return;
    }
    U32 next_owner_perms = data->mNextOwnerPerm;
    if (PERM_NONE == next_owner_perms)
    {
        next_owner_perms = PERM_MOVE | PERM_TRANSFER;
    }
    create_inventory_item(
        gAgent.getID(),
        gAgent.getSessionID(),
        folder_id,
        data->mAssetInfo.mTransactionID,
        data->mAssetInfo.getName(),
        data->mAssetInfo.getDescription(),
        data->mAssetInfo.mType,
        data->mInventoryType,
        NO_INV_SUBTYPE,
        next_owner_perms,
        LLPointer<LLInventoryCallback>(NULL));
    assign_uploaded_thumbnail(hold, asset_id);
    delete data;
}
}

void LLInventoryListener::snapshotUpload(LLSD const& data)
{
    snapshotUploadWithContext(data, nullptr);
}

void LLInventoryListener::dispatchPreparedSnapshotUpload(const LLSD& data, const FSAssistantExecutionContext& context)
{
    snapshotUploadWithContext(data, &context);
}

void LLInventoryListener::snapshotUploadWithContext(LLSD const& data, const FSAssistantExecutionContext* context)
{
    const LLUUID id = data["id"].asUUID();
    LLInventoryObject* object = gInventory.getObject(id);
    if (!object || !object_is_agent_inventory(id))
    {
        sendReply(llsd::map("error", object ? "Only agent inventory can be edited" : "Inventory object was not found"), data);
        return;
    }

    std::string raw_destination = data.has("destination") ? data["destination"].asString() : std::string();
    LLStringUtil::trim(raw_destination);
    LLStringUtil::toLower(raw_destination);
    std::string destination;
    if (!fs_snapshot::normalize_destination(raw_destination, destination))
    {
        sendReply(llsd::map("error", "destination must be thumbnail or texture"), data);
        return;
    }

    FSSnapshotDefaults defaults;
    defaults.viewport_only = true;
    defaults.square = true;
    defaults.square_edge = fs_snapshot::kDefaultEdge;
    FSSnapshotFrame frame;
    std::string error;
    if (!fs_parse_snapshot_frame(data, defaults, frame, error))
    {
        sendReply(llsd::map("error", error), data);
        return;
    }

    if (context && ((context->allowed && !context->allowed()) ||
        (context->deadline > 0.0 && static_cast<F64>(totalTime()) / 1000000.0 >= context->deadline)))
    {
        sendReply(llsd::map("error", "Snapshot permission was revoked or the request expired"), data);
        return;
    }
    const S32 quoted_cost = destination == "texture" ?
        LLAgentBenefitsMgr::current().getTextureUploadCost(frame.width, frame.height) : 0;
    if (context && context->approvedSnapshotCost >= 0 && quoted_cost > context->approvedSnapshotCost)
    {
        sendReply(llsd::map("error", "The upload price increased. Request a fresh confirmation."), data);
        return;
    }

    LLPointer<LLImageRaw> raw;
    if (!fs_capture_snapshot_image(frame, raw, error))
    {
        sendReply(llsd::map("error", error.empty() ? "Snapshot failed" : error), data);
        return;
    }

    if (destination == "thumbnail")
    {
        LLFloaterSimpleSnapshot::uploadThumbnail(
            raw,
            id,
            LLUUID::null,
            [data, id](const LLUUID& asset_id)
            {
                if (asset_id.isNull())
                {
                    sendReply(llsd::map("error", "Thumbnail upload failed"), data);
                    return;
                }
                sendReply(llsd::map(
                    "ok", true,
                    "cost", LLSD::Integer(0),
                    "destination", "thumbnail",
                    "thumbnail_id", asset_id,
                    "id", id), data);
            });
        return;
    }

    if (gDisconnected)
    {
        sendReply(llsd::map("error", "Texture upload failed"), data);
        return;
    }

    std::string name = data.has("name") ? data["name"].asString() : std::string();
    LLStringUtil::trim(name);
    if (name.empty())
    {
        name = object->getName();
    }
    const S32 cost = LLAgentBenefitsMgr::current().getTextureUploadCost(frame.width, frame.height);
    if (context && context->approvedSnapshotCost >= 0 && cost > context->approvedSnapshotCost)
    {
        sendReply(llsd::map("error", "The upload price increased. Request a fresh confirmation."), data);
        return;
    }
    LLPointer<LLImageJ2C> j2c = LLViewerTextureList::convertToUploadFile(raw);
    if (j2c.isNull() || !j2c->getData() || j2c->getDataSize() <= 0)
    {
        sendReply(llsd::map("error", "Snapshot encoding failed"), data);
        return;
    }
    std::string buffer;
    buffer.assign(reinterpret_cast<const char*>(j2c->getData()), static_cast<std::size_t>(j2c->getDataSize()));

    auto hold = std::make_shared<SnapshotTextureHold>();
    hold->request = data;
    hold->inventory_id = id;
    hold->cost = cost;
    LLNewBufferedResourceUploadInfo::uploadFinish_f finish = [hold](LLUUID asset_id, LLSD)
    {
        assign_uploaded_thumbnail(hold, asset_id);
    };
    LLNewBufferedResourceUploadInfo::uploadFailure_f failure = [hold](LLUUID, LLSD, std::string reason)
    {
        reply_snapshot_texture(hold, llsd::map("error", reason.empty() ? "Texture upload failed" : reason));
        return true;
    };
    LLResourceUploadInfo::ptr_t uploadInfo = std::make_shared<LLNewBufferedResourceUploadInfo>(
        buffer,
        LLUUID::generateNewID(),
        name,
        name,
        0,
        LLFolderType::FT_TEXTURE,
        LLInventoryType::IT_TEXTURE,
        LLAssetType::AT_TEXTURE,
        LLFloaterPerms::getNextOwnerPerms("Uploads"),
        LLFloaterPerms::getGroupPerms("Uploads"),
        LLFloaterPerms::getEveryonePerms("Uploads"),
        cost,
        LLUUID::null,
        false,
        finish,
        failure);

    const std::string upload_cap = gAgent.getRegionCapability("NewFileAgentInventory");
    if (!upload_cap.empty())
    {
        upload_new_resource(uploadInfo);
        return;
    }
    if (!gAssetStorage)
    {
        reply_snapshot_texture(hold, llsd::map("error", "Texture upload failed"));
        return;
    }
    auto* boxed = new std::shared_ptr<SnapshotTextureHold>(hold);
    upload_new_resource(uploadInfo, snapshot_texture_legacy_callback, boxed);
}

void LLInventoryListener::setFavorite(LLSD const& data)
{
    Response response(LLSD(), data);
    const LLUUID id = data["id"].asUUID();
    if (!gInventory.getObject(id) || !object_is_agent_inventory(id))
    {
        return response.error("Inventory object was not found");
    }
    if (!data.has("favorite"))
    {
        return response.error("favorite is required");
    }
    set_favorite(id, data["favorite"].asBoolean());
    response["ok"] = true;
    response["id"] = id;
    response["favorite"] = get_is_favorite(id);
}

void LLInventoryListener::link(LLSD const& data)
{
    const LLUUID id = data["id"].asUUID();
    LLUUID parent_id = data["parent_id"].asUUID();
    std::string error;
    LLInventoryObject* source = gInventory.getObject(id);
    if (!source || !validateDestination(parent_id, error))
    {
        Response response(LLSD(), data);
        return response.error(source ? error : "Inventory object was not found");
    }
    const LLUUID linked_id = source->getIsLinkType() ? source->getLinkedUUID() : id;
    LLPointer<LLInventoryCallback> callback =
        new LLBoostFuncInventoryCallback([data](const LLUUID& new_id)
        {
            sendReply(new_id.isNull() ? llsd::map("error", "Link creation failed")
                                      : llsd::map("ok", true, "new_id", new_id), data);
        });
    link_inventory_object(parent_id, linked_id, callback);
}

namespace
{
std::string replacement_destination_error(const LLUUID& id)
{
    LLViewerInventoryCategory* destination = gInventory.getCategory(id);
    if (!destination || !object_is_agent_inventory(id))
        return "Destination must be a folder in the agent inventory";
    const LLFolderType::EType type = destination->getPreferredType();
    if (type == LLFolderType::FT_CURRENT_OUTFIT ||
        type == LLFolderType::FT_MARKETPLACE_LISTINGS ||
        type == LLFolderType::FT_MARKETPLACE_STOCK ||
        type == LLFolderType::FT_LOST_AND_FOUND)
        return "That special destination is not supported by the automation API";
    return std::string();
}

std::string replacement_candidate_error(const FSLinkReplacementSelection& selection,
                                         const FSLinkReplacementCandidate& candidate)
{
    const LLUUID id(candidate.id), parent(candidate.parent_id), trash(selection.trash_id);
    LLViewerInventoryItem* link = gInventory.getItem(id);
    if (!link || !object_is_agent_inventory(id) || !link->getIsLinkType() ||
        link->getParentUUID() != parent ||
        link->getLinkedUUID() != LLUUID(candidate.source_id) ||
        link->getActualType() != candidate.link_kind ||
        link->getName() != candidate.name || link->getActualDescription() != candidate.description)
        return "The original link disappeared or changed after preparation";
    std::string error = replacement_destination_error(parent);
    if (!error.empty()) return error;
    error = replacement_destination_error(trash);
    if (!error.empty()) return error;
    if (!get_is_item_removable(&gInventory, id, false) ||
        (RlvActions::isRlvEnabled() &&
         (!RlvFolderLocks::instance().canMoveItem(id, trash) ||
          RlvFolderLocks::instance().isLockedFolder(parent, RLV_LOCK_ADD))))
        return "The original link or its destination is protected or locked";
    LLInventoryObject* requested = gInventory.getObject(LLUUID(selection.target_id));
    LLInventoryObject* target = gInventory.getObject(LLUUID(selection.resolved_target_id));
    if (!requested || !target || target->getIsLinkType() ||
        !object_is_agent_inventory(target->getUUID()) ||
        !LLAssetType::lookupCanLink(target->getType()) ||
        (requested->getIsLinkType() ? requested->getLinkedUUID() : requested->getUUID()) != target->getUUID() ||
        (gInventory.getCategory(target->getUUID()) ? LLAssetType::AT_LINK_FOLDER : LLAssetType::AT_LINK) != selection.target_kind)
        return "The replacement target disappeared or changed after preparation";
    return std::string();
}

std::shared_ptr<FSLinkReplacement::Reservations> replacement_reservations()
{
    static auto reservations = std::make_shared<FSLinkReplacement::Reservations>();
    return reservations;
}

// startFetch submits AIS/UDP fetches without retaining this request object.
// The operation's bounded timer polls inventory and owns no global observer.
class ReplacementItemFetch final : public LLInventoryFetchItemsObserver
{
public:
    explicit ReplacementItemFetch(const LLUUID& id) : LLInventoryFetchItemsObserver(id) {}
    void done() override {}
};
}

bool LLInventoryListener::prepareLinkReplacement(const LLSD& data,
    FSLinkReplacementSelection& selection, std::string& error) const
{
    selection = FSLinkReplacementSelection();
    const LLUUID source_id = data["source_id"].asUUID();
    const LLUUID target_id = data["target_id"].asUUID();
    if (gDisconnected || gAgent.getID().isNull() || !gInventory.isInventoryUsable())
    {
        error = "Log in and wait for inventory before replacing links";
        return false;
    }
    if (source_id.isNull() || target_id.isNull())
    {
        error = "source_id and target_id must be inventory UUIDs";
        return false;
    }
    LLInventoryObject* requested = gInventory.getObject(target_id);
    LLInventoryObject* target = requested && requested->getIsLinkType()
        ? gInventory.getObject(requested->getLinkedUUID()) : requested;
    if (!target || target->getIsLinkType() || !object_is_agent_inventory(target->getUUID()) ||
        !LLAssetType::lookupCanLink(target->getType()))
    {
        error = "The target must resolve to a linkable object in agent inventory";
        return false;
    }
    selection.source_id = source_id.asString();
    selection.target_id = target_id.asString();
    selection.resolved_target_id = target->getUUID().asString();
    selection.target_kind = gInventory.getCategory(target->getUUID())
        ? LLAssetType::AT_LINK_FOLDER : LLAssetType::AT_LINK;
    selection.session_id = gAgent.getSessionID().asString();
    selection.target_name = target->getName();
    selection.target_path = make_path(target);
    if (const LLInventoryObject* source = gInventory.getObject(source_id))
    {
        selection.source_name = source->getName();
        selection.source_path = make_path(source);
    }
    selection.no_op = source_id == target->getUUID();
    if (selection.no_op) return true;
    const LLUUID root = gInventory.getRootFolderID();
    if (!inventory_tree_complete(root))
    {
        LLInventoryModelBackgroundFetch::instance().start(root, true);
        error = "Inventory is still loading; retry link replacement when the full inventory is ready";
        return false;
    }
    const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (!validateDestination(trash, error)) return false;
    selection.trash_id = trash.asString();
    LLInventoryModel::cat_array_t categories;
    LLInventoryModel::item_array_t items;
    gInventory.collectDescendents(root, categories, items, LLInventoryModel::EXCLUDE_TRASH);
    for (LLViewerInventoryItem* item : items)
    {
        if (!item->getIsLinkType() || item->getLinkedUUID() != source_id) continue;
        FSLinkReplacementCandidate candidate;
        candidate.id = item->getUUID().asString();
        candidate.parent_id = item->getParentUUID().asString();
        candidate.source_id = item->getLinkedUUID().asString();
        candidate.name = item->getName();
        candidate.path = make_path(item);
        candidate.description = item->getActualDescription();
        candidate.link_kind = item->getActualType();
        if (const LLInventoryObject* parent = gInventory.getObject(item->getParentUUID()))
        {
            candidate.parent_name = parent->getName();
            candidate.parent_path = make_path(parent);
        }
        candidate.skip_error = replacement_candidate_error(selection, candidate);
        selection.links.push_back(std::move(candidate));
    }
    std::sort(selection.links.begin(), selection.links.end(),
        [](const FSLinkReplacementCandidate& lhs, const FSLinkReplacementCandidate& rhs)
        { return lhs.id < rhs.id; });
    return true;
}

bool LLInventoryListener::validateLinkReplacementSelection(
    const FSLinkReplacementSelection& selection, std::string& error) const
{
    FSLinkReplacementSelection current;
    if (!prepareLinkReplacement(llsd::map("source_id", LLUUID(selection.source_id),
                                          "target_id", LLUUID(selection.target_id)), current, error)) return false;
    if (current.session_id != selection.session_id ||
        current.resolved_target_id != selection.resolved_target_id ||
        current.target_kind != selection.target_kind || current.no_op != selection.no_op ||
        current.trash_id != selection.trash_id || current.links.size() != selection.links.size() ||
        current.source_name != selection.source_name || current.target_name != selection.target_name ||
        current.source_path != selection.source_path || current.target_path != selection.target_path)
    {
        error = "The replacement selection changed; request fresh approval";
        return false;
    }
    for (std::size_t i = 0; i < current.links.size(); ++i)
    {
        const auto& before = selection.links[i];
        const auto& now = current.links[i];
        if (before.id != now.id || before.parent_id != now.parent_id ||
            before.source_id != now.source_id || before.link_kind != now.link_kind ||
            before.name != now.name || before.path != now.path ||
            before.description != now.description || before.skip_error != now.skip_error)
        {
            error = "A selected link changed; request fresh approval";
            return false;
        }
    }
    return true;
}

void LLInventoryListener::dispatchPreparedLinkReplacement(const LLSD& data,
    std::shared_ptr<const FSLinkReplacementSelection> selection,
    const FSAssistantExecutionContext& context)
{
    std::string error;
    if (!selection || !validateLinkReplacementSelection(*selection, error))
    {
        sendReply(llsd::map("error", error.empty() ? "Replacement selection is missing" : error,
                            "error_code", "stale_approval"), data);
        return;
    }
    if (!data["confirm"].asBoolean())
    {
        Response response(llsd::map("count", static_cast<S32>(selection->links.size())), data);
        requireConfirm(data, response);
        return;
    }
    const LLUUID operation_id = LLUUID::generateNewID();
    auto reservations = replacement_reservations();
    reservations->setSession(selection->session_id);
    FSLinkReplacement::Backend backend;
    backend.now = []() { return static_cast<F64>(totalTime()) / 1000000.0; };
    backend.allowed = [session = selection->session_id, allowed = context.allowed]()
    {
        return !gDisconnected && !LLApp::isExiting() && gInventory.isInventoryUsable() &&
            gAgent.getSessionID().asString() == session && (!allowed || allowed());
    };
    backend.validate = [selection](const FSLinkReplacementCandidate& candidate)
    { return replacement_candidate_error(*selection, candidate); };
    backend.create = [selection](const FSLinkReplacementCandidate& candidate,
                                 std::function<void(const std::string&)> completed)
    {
        LLPointer<LLInventoryCallback> callback = new LLBoostFuncInventoryCallback(
            [completed](const LLUUID& new_id) { completed(new_id.isNull() ? std::string() : new_id.asString()); });
        link_inventory_object(LLUUID(candidate.parent_id), LLUUID(selection->resolved_target_id), callback);
    };
    backend.verify = [selection](const FSLinkReplacementCandidate& candidate, const std::string& id)
    {
        LLViewerInventoryItem* link = gInventory.getItem(LLUUID(id));
        if (!link || !link->isFinished()) return FSLinkReplacement::Verification::Waiting;
        return link->getIsLinkType() && link->getUUID() != LLUUID(candidate.id) &&
            link->getParentUUID() == LLUUID(candidate.parent_id) &&
            link->getLinkedUUID() == LLUUID(selection->resolved_target_id) &&
            link->getActualType() == selection->target_kind
                ? FSLinkReplacement::Verification::Valid : FSLinkReplacement::Verification::Invalid;
    };
    backend.fetch = [](const std::string& id)
    {
        ReplacementItemFetch request{LLUUID(id)};
        request.startFetch();
    };
    backend.submitTrash = [selection](const FSLinkReplacementCandidate& candidate)
    {
        LLViewerInventoryItem* original = gInventory.getItem(LLUUID(candidate.id));
        if (!original) return false;
        gInventory.changeItemParent(original, LLUUID(selection->trash_id), false);
        LLViewerInventoryItem* moved = gInventory.getItem(LLUUID(candidate.id));
        return moved && moved->getParentUUID() == LLUUID(selection->trash_id);
    };
    const F64 now = backend.now();
    const F64 deadline = context.deadline > 0.0 ? std::min(now + 30.0, context.deadline - 0.5) : now + 30.0;
    auto operation = std::make_shared<FSLinkReplacement::Operation>(selection, reservations,
        operation_id.asString(), std::move(backend), deadline,
        [data, operation_id](const std::vector<FSLinkReplacement::Result>& outcomes)
        {
            LLSD results = LLSD::emptyArray();
            S32 completed = 0, skipped = 0;
            for (const auto& outcome : outcomes)
            {
                LLSD row = llsd::map("id", LLUUID(outcome.candidate.id),
                    "parent_id", LLUUID(outcome.candidate.parent_id), "ok", outcome.ok,
                    "status", outcome.status, "original_preserved", outcome.original_preserved,
                    "retry_safe", outcome.retry_safe, "trash_state", outcome.ok ? "submitted" : "not_submitted");
                if (!outcome.new_id.empty()) row["new_id"] = LLUUID(outcome.new_id);
                if (!outcome.error.empty()) row["error"] = outcome.error;
                results.append(row);
                completed += outcome.ok ? 1 : 0;
                skipped += outcome.status == "skipped" ? 1 : 0;
            }
            const S32 count = static_cast<S32>(outcomes.size());
            sendReply(llsd::map("ok", completed == count, "count", count, "results", results,
                "operation_id", operation_id, "completed_count", completed,
                "failed_count", count - completed - skipped, "skipped_count", skipped,
                "partial", completed > 0 && completed < count), data);
        });
    if (!operation->tick())
        doPeriodically([operation]() { return operation->tick(); }, 0.1f);
}

void LLInventoryListener::replaceLinks(LLSD const& data)
{
    auto selection = std::make_shared<FSLinkReplacementSelection>();
    std::string error;
    if (!prepareLinkReplacement(data, *selection, error))
    {
        sendReply(llsd::map("error", error), data);
        return;
    }
    dispatchPreparedLinkReplacement(data, selection, FSAssistantExecutionContext());
}

void LLInventoryListener::createItem(LLSD const& data)
{
    std::string type_name = data["type"].asString();
    LLStringUtil::trim(type_name);
    LLStringUtil::toLower(type_name);
    if (type_name.empty())
    {
        Response response(LLSD(), data);
        return response.error("type is required");
    }
    const LLUUID parent_id = data["parent_id"].asUUID();
    if (parent_id.notNull())
    {
        std::string error;
        if (!validateDestination(parent_id, error))
        {
            Response response(LLSD(), data);
            return response.error(error);
        }
    }

    if (type_name == "landmark")
    {
        const LLUUID folder_id = parent_id.notNull()
            ? parent_id : gInventory.findCategoryUUIDForType(LLFolderType::FT_LANDMARK);
        std::string name = data["name"].asString();
        if (name.empty())
        {
            LLAgentUI::buildLocationString(name, LLAgentUI::LOCATION_FORMAT_LANDMARK);
        }
        LLLandmarkActions::createLandmarkHere(name, data["desc"].asString(), folder_id);
        Response response(LLSD(), data);
        response["ok"] = true;
        response["parent_id"] = folder_id;
        response["note"] = "The new landmark id arrives in a later inventory change";
        return;
    }

    const LLWearableType::EType wearable = LLWearableType::getInstance()->typeNameToType(type_name);
    const bool wearable_ok = wearable >= LLWearableType::WT_SHAPE && wearable < LLWearableType::WT_COUNT;
    const bool known = wearable_ok || type_name == "lsl" || type_name == "notecard" ||
        type_name == "gesture" || type_name == "material" || type_name == "sky" ||
        type_name == "water" || type_name == "daycycle";
    if (!known)
    {
        Response response(LLSD(), data);
        return response.error("Unknown inventory item type");
    }

    menu_create_inventory_item(nullptr, parent_id, LLSD(type_name), LLUUID::null,
        [data](const LLUUID& new_id)
        {
            sendReply(new_id.isNull() ? llsd::map("error", "Item creation failed")
                                      : llsd::map("ok", true, "new_id", new_id), data);
        });
}

void LLInventoryListener::batchMove(LLSD const& data)
{
    Response response(LLSD(), data);
    const std::vector<LLUUID> ids = uuid_list(data["ids"]);
    if (ids.empty() || static_cast<S32>(ids.size()) > kBatchLimit)
    {
        return response.error("ids must contain between 1 and 50 UUIDs");
    }
    const LLUUID destination_id = data["parent_id"].asUUID();
    std::string error;
    if (!validateDestination(destination_id, error))
    {
        return response.error(error);
    }

    LLSD results = LLSD::emptyArray();
    for (const LLUUID& id : ids)
    {
        LLInventoryObject* object = gInventory.getObject(id);
        if (!object || !object_is_agent_inventory(id))
        {
            results.append(llsd::map("id", id, "ok", false, "error", "Inventory object was not found"));
            continue;
        }
        if (id == destination_id ||
            (gInventory.getCategory(id) && gInventory.isObjectDescendentOf(destination_id, id)))
        {
            results.append(llsd::map("id", id, "ok", false, "error", "A folder cannot be moved into itself or its descendant"));
            continue;
        }
        if (LLViewerInventoryItem* item = gInventory.getItem(id))
        {
            if (!get_is_item_removable(&gInventory, id, false) ||
                (RlvActions::isRlvEnabled() && !RlvFolderLocks::instance().canMoveItem(id, destination_id)))
            {
                results.append(llsd::map("id", id, "ok", false, "error", "Item is protected or locked"));
                continue;
            }
            gInventory.changeItemParent(item, destination_id, false);
        }
        else if (LLViewerInventoryCategory* category = gInventory.getCategory(id))
        {
            if (!get_is_category_removable(&gInventory, id) ||
                (RlvActions::isRlvEnabled() && !RlvFolderLocks::instance().canMoveFolder(id, destination_id)))
            {
                results.append(llsd::map("id", id, "ok", false, "error", "Folder is protected or locked"));
                continue;
            }
            gInventory.changeCategoryParent(category, destination_id, false);
        }
        results.append(llsd::map("id", id, "ok", true));
    }
    response["results"] = results;
}

void LLInventoryListener::batchRename(LLSD const& data)
{
    Response response(LLSD(), data);
    if (!data["items"].isArray() || data["items"].size() < 1 || data["items"].size() > kBatchLimit)
    {
        return response.error("items must contain between 1 and 50 {id, name} entries");
    }
    LLSD results = LLSD::emptyArray();
    for (const LLSD& entry : llsd::inArray(data["items"]))
    {
        const LLUUID id = entry["id"].asUUID();
        std::string name = entry["name"].asString();
        LLStringUtil::trim(name);
        LLInventoryObject* object = gInventory.getObject(id);
        if (!object || !object_is_agent_inventory(id) || name.empty())
        {
            results.append(llsd::map("id", id, "ok", false, "error", "Inventory object was not found or name is empty"));
            continue;
        }
        bool allowed = false;
        if (LLViewerInventoryItem* item = gInventory.getItem(id))
        {
            allowed = item->isFinished() &&
                      item->getInventoryType() != LLInventoryType::IT_CALLINGCARD &&
                      item->getPermissions().allowModifyBy(gAgent.getID()) &&
                      (!RlvActions::isRlvEnabled() || RlvFolderLocks::instance().canRenameItem(id));
        }
        else
        {
            allowed = get_is_category_renameable(&gInventory, id);
        }
        if (!allowed)
        {
            results.append(llsd::map("id", id, "ok", false, "error", "Inventory object cannot be renamed"));
            continue;
        }
        LLSD updates;
        updates["name"] = name;
        if (gInventory.getItem(id))
        {
            update_inventory_item(id, updates, nullptr);
        }
        else
        {
            rename_category(&gInventory, id, name, nullptr);
        }
        results.append(llsd::map("id", id, "ok", true, "name", name));
    }
    response["results"] = results;
}

void LLInventoryListener::batchCopy(LLSD const& data)
{
    Response response(LLSD(), data);
    const std::vector<LLUUID> ids = uuid_list(data["ids"]);
    if (ids.empty() || static_cast<S32>(ids.size()) > kBatchLimit)
    {
        return response.error("ids must contain between 1 and 50 UUIDs");
    }
    const LLUUID destination_id = data["parent_id"].asUUID();
    std::string error;
    if (!validateDestination(destination_id, error))
    {
        return response.error(error);
    }

    using namespace LLInventoryCopyPolicy;
    const Policy policy = parse(data["policy"].asString());
    if (policy == Policy::INVALID)
    {
        return response.error("Unknown copy policy");
    }

    LLSD results = LLSD::emptyArray();
    for (const LLUUID& id : ids)
    {
        LLViewerInventoryItem* item = gInventory.getItem(id);
        if (!item)
        {
            results.append(llsd::map(
                "id", id,
                "ok", false,
                "error", gInventory.getCategory(id)
                    ? "Use inventory_copy for folders"
                    : "Inventory object was not found"));
            continue;
        }
        const bool no_copy = !item->getIsLinkType() && !item->getPermissions().allowCopyBy(gAgent.getID());
        const Action action = decide(policy, no_copy);
        if (action == Action::REJECT || (no_copy && policy == Policy::COPYABLE_ONLY))
        {
            results.append(llsd::map("id", id, "ok", false, "error", "Item cannot be copied under the selected policy"));
            continue;
        }
        if (action == Action::COPY_AND_CONFIRM_MOVES)
        {
            CopyPlan plan;
            plan.id.generate();
            plan.source_id = id;
            plan.destination_root_id = destination_id;
            plan.expires_at = LLDate::now().secondsSinceEpoch() + 600.0;
            plan.moves.push_back({id, item->getParentUUID(), destination_id});
            mCopyPlans[plan.id] = plan;
            results.append(llsd::map(
                "id", id,
                "ok", true,
                "needs_confirmation", true,
                "plan_id", plan.id));
            continue;
        }
        if (item->getIsLinkType())
        {
            link_inventory_object(destination_id, item->getLinkedUUID(), nullptr);
        }
        else
        {
            copy_inventory_item(gAgent.getID(), item->getPermissions().getOwner(), id, destination_id, std::string(), nullptr);
        }
        results.append(llsd::map("id", id, "ok", true, "submitted", true));
    }
    response["results"] = results;
}

void LLInventoryListener::trash(LLSD const& data)
{
    const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (trash_id.isNull())
    {
        Response response(LLSD(), data);
        return response.error("Trash folder was not found");
    }
    LLSD moved(data);
    moved["parent_id"] = trash_id;
    move(moved);
}

void LLInventoryListener::restore(LLSD const& data)
{
    const LLUUID id = data["id"].asUUID();
    LLFolderType::EType preferred = LLFolderType::FT_NONE;
    if (LLViewerInventoryItem* item = gInventory.getItem(id))
    {
        preferred = item->getInventoryType() == LLInventoryType::IT_SNAPSHOT
            ? LLFolderType::FT_SNAPSHOT_CATEGORY
            : LLFolderType::assetTypeToFolderType(item->getType());
    }
    else if (LLViewerInventoryCategory* category = gInventory.getCategory(id))
    {
        preferred = LLFolderType::assetTypeToFolderType(category->getType());
    }
    else
    {
        Response response(LLSD(), data);
        return response.error("Inventory object was not found");
    }
    const LLUUID parent_id = gInventory.findCategoryUUIDForType(preferred);
    if (parent_id.isNull())
    {
        Response response(LLSD(), data);
        return response.error("Restore destination was not found");
    }
    LLSD moved(data);
    moved["parent_id"] = parent_id;
    move(moved);
}

void LLInventoryListener::emptyTrash(LLSD const& data)
{
    if (!data["confirm"].asBoolean())
    {
        Response response(LLSD(), data);
        return response.error("Confirmation is required");
    }
    const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (trash_id.isNull())
    {
        Response response(LLSD(), data);
        return response.error("Trash folder was not found");
    }
    if (gInventory.categoryHasChildren(trash_id) == LLInventoryModel::CHILDREN_NO)
    {
        Response response(LLSD(), data);
        response["ok"] = true;
        response["emptied"] = true;
        return;
    }
    purge_descendents_of(trash_id, new LLBoostFuncInventoryCallback([data](const LLUUID&)
    {
        sendReply(llsd::map("ok", true, "emptied", true), data);
    }));
}

void LLInventoryListener::purge(LLSD const& data)
{
    const LLUUID id = data["id"].asUUID();
    LLInventoryObject* object = gInventory.getObject(id);
    if (!object || !object_is_agent_inventory(id))
    {
        Response response(LLSD(), data);
        return response.error(object ? "Only agent inventory can be purged" : "Inventory object was not found");
    }
    if (LLViewerInventoryCategory* category = gInventory.getCategory(id))
    {
        if (LLFolderType::lookupIsProtectedType(category->getPreferredType()))
        {
            Response response(LLSD(), data);
            return response.error("Protected folders cannot be purged");
        }
    }
    if (!data["confirm"].asBoolean())
    {
        Response response(LLSD(), data);
        return response.error("Confirmation is required");
    }
    remove_inventory_object(id, new LLBoostFuncInventoryCallback([data, id](const LLUUID&)
    {
        sendReply(llsd::map("ok", true, "id", id, "purged", true), data);
    }));
}

void LLInventoryListener::readNotecard(LLSD const& data)
{
    LLViewerInventoryItem* item = gInventory.getItem(data["id"].asUUID());
    if (!item || item->getType() != LLAssetType::AT_NOTECARD)
    {
        Response response(LLSD(), data);
        return response.error(item ? "Item is not a notecard" : "Inventory item was not found");
    }
    if (!gAssetStorage)
    {
        Response response(LLSD(), data);
        return response.error("Asset storage is not available");
    }
    auto* request = new AssetRead{data, item->getUUID(), item->getAssetUUID(), true};
    gAssetStorage->getAssetData(item->getAssetUUID(), LLAssetType::AT_NOTECARD, on_asset_read, request);
}

void LLInventoryListener::readScript(LLSD const& data)
{
    LLViewerInventoryItem* item = gInventory.getItem(data["id"].asUUID());
    if (!item || item->getType() != LLAssetType::AT_LSL_TEXT)
    {
        Response response(LLSD(), data);
        return response.error(item ? "Item is not a script" : "Inventory item was not found");
    }
    if (!gAssetStorage)
    {
        Response response(LLSD(), data);
        return response.error("Asset storage is not available");
    }
    auto* request = new AssetRead{data, item->getUUID(), item->getAssetUUID(), false};
    gAssetStorage->getAssetData(item->getAssetUUID(), LLAssetType::AT_LSL_TEXT, on_asset_read, request);
}

void LLInventoryListener::landmark(LLSD const& data)
{
    LLViewerInventoryItem* item = gInventory.getItem(data["id"].asUUID());
    if (!item || item->getType() != LLAssetType::AT_LANDMARK)
    {
        Response response(LLSD(), data);
        return response.error(item ? "Item is not a landmark" : "Inventory item was not found");
    }
    const LLUUID item_id = item->getUUID();
    const LLUUID asset_id = item->getAssetUUID();
    auto sent = std::make_shared<bool>(false);
    auto reply_once = [data, item_id, asset_id, sent](LLLandmark* loaded)
    {
        if (*sent)
        {
            return;
        }
        *sent = true;
        reply_landmark(data, item_id, asset_id, loaded);
    };
    LLLandmark* existing = gLandmarkList.getAsset(asset_id, reply_once);
    LLVector3d position;
    if (existing && existing->getGlobalPos(position))
    {
        reply_once(existing);
    }
    else if (!existing && !gLandmarkList.isAssetInLoadedCallbackMap(asset_id) && !*sent)
    {
        reply_once(nullptr);
    }
}

void LLInventoryListener::changes(LLSD const& data)
{
    ensureObserving();
    Response response(LLSD(), data);
    const S32 since = data["since"].asInteger();
    response["generation"] = mGeneration;
    response["changes"] = LLSD::emptyArray();
    for (const ChangeRecord& change : mChanges)
    {
        if (change.generation > since)
        {
            response["changes"].append(llsd::map(
                "generation", change.generation,
                "id", change.id,
                "mask", LLSD::Integer(change.mask)));
        }
    }
}
