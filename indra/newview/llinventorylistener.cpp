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
#include "llapp.h"
#include "lldate.h"
#include "rlvactions.h"
#include "rlvlocks.h"
#include "stringize.h"

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
    add("get", "Fetch and return inventory objects named by item_ids",
        &LLInventoryListener::get, llsd::map("item_ids", LLSD(), "reply", LLSD()));
    add("list", "Fetch and return the direct children of folder_id",
        &LLInventoryListener::list, llsd::map("folder_id", LLSD(), "reply", LLSD()));
    add("search", "Fetch recursively and search below folder_id",
        &LLInventoryListener::search, llsd::map("folder_id", LLSD(), "reply", LLSD()));
    add("createFolder", "Create a normal folder below parent_id",
        &LLInventoryListener::createFolder,
        llsd::map("parent_id", LLSD(), "name", LLSD(), "reply", LLSD()));
    add("move", "Move an inventory item or folder to dest_folder_id",
        &LLInventoryListener::move,
        llsd::map("id", LLSD(), "dest_folder_id", LLSD(), "reply", LLSD()));
    add("rename", "Rename an inventory item or folder",
        &LLInventoryListener::rename,
        llsd::map("id", LLSD(), "name", LLSD(), "reply", LLSD()));
    add("copy", "Copy an item or folder; may return a no-copy move plan",
        &LLInventoryListener::copy,
        llsd::map("id", LLSD(), "dest_folder_id", LLSD(), "reply", LLSD()));
    add("confirmCopy", "Confirm or decline no-copy moves from a completed copy",
        &LLInventoryListener::confirmCopy,
        llsd::map("plan_id", LLSD(), "confirm", LLSD(), "reply", LLSD()));
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

    add_objects_info(response, *cats, *items);
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

    LLAssetType::EType mType;
    std::string mName;
    std::string mDesc;
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
    mLinkFilter(INCLUDE_LINKS),
    mItemLimit(0),
    mItemCount(0)
{

    mName = data["name"].asString();
    mDesc = data["desc"].asString();

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
            "category_count", gInventory.getCategoryCount()),
        data);
}

void LLInventoryListener::get(LLSD const& data)
{
    if (!gInventory.isInventoryUsable())
    {
        Response response(LLSD(), data);
        return response.error("Inventory is not usable; log in and wait for initialization");
    }

    uuid_vec_t ids = LLSDParam<uuid_vec_t>(data["item_ids"]);
    uuid_vec_t item_ids;
    for (const LLUUID& id : ids)
    {
        if (gInventory.getItem(id))
        {
            item_ids.push_back(id);
        }
    }
    fetch_items_then(item_ids, [this, data]() { getItemsInfo(data); });
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
    const LLUUID folder_id = data["folder_id"].asUUID();
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
    const LLUUID folder_id = data["folder_id"].asUUID();
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
    add_objects_info(response, categories, items);
    response["complete"] = inventory_tree_complete(folder_id);
    if (data["_fetch_timeout"].asBoolean())
    {
        response.warn("Inventory fetch timed out; results may be incomplete");
    }
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
    Response validation(LLSD(), LLSD());
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
    const LLUUID destination_id = data["dest_folder_id"].asUUID();
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
        link_inventory_object(destination_id, item, callback);
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
    const LLUUID destination_id = data["dest_folder_id"].asUUID();
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
            link_inventory_object(destination_id, item, callback);
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
