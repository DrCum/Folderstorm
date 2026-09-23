/**
 * @file llappearancelistener.cpp
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

#include "llappearancelistener.h"

#include "llappearancemgr.h"
#include "llinventoryfunctions.h"
#include "lltransutil.h"
#include "llviewerjointattachment.h"
#include "llvoavatarself.h"
#include "llwearableitemslist.h"
#include "llwearabletype.h"
#include "rlvactions.h"
#include "rlvlocks.h"
#include "stringize.h"

LLAppearanceListener::LLAppearanceListener()
  : LLEventAPI("LLAppearance",
               "API to wear a specified outfit and wear/remove individual items")
{
    add("wearOutfit",
        "Wear outfit by folder id: [\"folder_id\"] OR by folder name: [\"folder_name\"]\n"
        "When [\"append\"] is true, outfit will be added to COF\n"
        "otherwise it will replace current oufit",
        &LLAppearanceListener::wearOutfit);

    add("wearItems",
        "Wear items by id: [items_id]",
        &LLAppearanceListener::wearItems,
        llsd::map("items_id", LLSD(), "replace", LLSD()));

    add("detachItems",
        "Detach items by id: [items_id]",
        &LLAppearanceListener::detachItems,
        llsd::map("items_id", LLSD()));

    add("getOutfitsList",
        "Return the table with Outfits info(id and name)",
         &LLAppearanceListener::getOutfitsList);

    add("getOutfitItems",
        "Return the table of items with info(id : name, wearable_type, is_worn) inside specified outfit folder",
         &LLAppearanceListener::getOutfitItems);
    add("worn",
        "Return Current Outfit contents, attachment points, and whether the outfit is still baking",
        &LLAppearanceListener::worn,
        llsd::map("reply", LLSD()));
}

bool LLAppearanceListener::requireConfirm(LLSD const& data, Response& response) const
{
    if (data["confirm"].asBoolean())
    {
        return true;
    }
    response.error("Confirmation is required");
    return false;
}

bool LLAppearanceListener::rlvAllows(const LLUUID& item_id, bool detach, std::string& error) const
{
    if (!RlvActions::isRlvEnabled())
    {
        return true;
    }
    LLViewerInventoryItem* item = gInventory.getItem(item_id);
    if (!item)
    {
        return true;
    }
    if (detach)
    {
        if (!RlvFolderLocks::instance().canRemoveItem(item_id))
        {
            error = "Item folder is locked";
            return false;
        }
    }
    else if (RlvFolderLocks::instance().isLockedFolder(item->getParentUUID(), RLV_LOCK_ADD))
    {
        error = "Item folder is locked";
        return false;
    }
    LLViewerInventoryItem* target = gInventory.getItem(item->getLinkedUUID());
    if (!target)
    {
        target = item;
    }
    if (target->isWearableType())
    {
        if (detach)
        {
            if (!RlvWearableLocks::instance().canRemove(target))
            {
                error = "Wearable is locked";
                return false;
            }
        }
        else if (RlvWearableLocks::instance().canWear(target) == RLV_WEAR_LOCKED)
        {
            error = "Wearable is locked";
            return false;
        }
    }
    else if (target->getType() == LLAssetType::AT_OBJECT)
    {
        if (detach)
        {
            if (!RlvAttachmentLocks::instance().canDetach(target))
            {
                error = "Attachment is locked";
                return false;
            }
        }
        else if (RlvAttachmentLocks::instance().canAttach(target) == RLV_WEAR_LOCKED)
        {
            error = "Attachment is locked";
            return false;
        }
    }
    return true;
}


void LLAppearanceListener::wearOutfit(LLSD const &data)
{
    Response response(LLSD(), data);
    if (!requireConfirm(data, response))
    {
        return;
    }
    if (!data.has("folder_id") && !data.has("folder_name"))
    {
        return response.error("Either [folder_id] or [folder_name] is required");
    }
    if (data.has("folder_id") && RlvActions::isRlvEnabled() &&
        RlvFolderLocks::instance().isLockedFolder(data["folder_id"].asUUID(), RLV_LOCK_ADD))
    {
        return response.error("Outfit folder is locked");
    }

    bool append = data.has("append") ? data["append"].asBoolean() : false;
    if (!LLAppearanceMgr::instance().wearOutfit(data, append))
    {
        return response.error("Failed to wear outfit");
    }
    response["ok"] = true;
    response["folder_id"] = data["folder_id"];
    response["append"] = append;
    response["outfit_dirty"] = LLAppearanceMgr::instance().isOutfitDirty();
}

void LLAppearanceListener::wearItems(LLSD const &data)
{
    Response response(LLSD(), data);
    if (!requireConfirm(data, response))
    {
        return;
    }
    const LLSD& items_id{ data["items_id"] };
    uuid_vec_t  ids;
    if (!items_id.isArray())
    {
        ids.push_back(items_id.asUUID());
    }
    else // array
    {
        for (const auto& id : llsd::inArray(items_id))
        {
            ids.push_back(id);
        }
    }
    for (const LLUUID& id : ids)
    {
        std::string rlv_error;
        if (!rlvAllows(id, false, rlv_error))
        {
            return response.error(rlv_error);
        }
    }
    LLAppearanceMgr::instance().wearItemsOnAvatar(ids, true, data["replace"].asBoolean());
    response["ok"] = true;
    response["count"] = (S32)ids.size();
    response["outfit_dirty"] = LLAppearanceMgr::instance().isOutfitDirty();
}

void LLAppearanceListener::detachItems(LLSD const &data)
{
    Response response(LLSD(), data);
    if (!requireConfirm(data, response))
    {
        return;
    }
    const LLSD& items_id{ data["items_id"] };
    uuid_vec_t  ids;
    if (!items_id.isArray())
    {
        ids.push_back(items_id.asUUID());
    }
    else // array
    {
        for (const auto& id : llsd::inArray(items_id))
        {
            ids.push_back(id);
        }
    }
    for (const LLUUID& id : ids)
    {
        std::string rlv_error;
        if (!rlvAllows(id, true, rlv_error))
        {
            return response.error(rlv_error);
        }
    }
    LLAppearanceMgr::instance().removeItemsFromAvatar(ids);
    response["ok"] = true;
    response["count"] = (S32)ids.size();
    response["outfit_dirty"] = LLAppearanceMgr::instance().isOutfitDirty();
}

void LLAppearanceListener::getOutfitsList(LLSD const &data)
{
    Response response(LLSD(), data);
    const LLUUID outfits_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_MY_OUTFITS);

    LLInventoryModel::cat_array_t cat_array;
    LLInventoryModel::item_array_t item_array;

    LLIsFolderType is_category(LLFolderType::FT_OUTFIT);
    gInventory.collectDescendentsIf(outfits_id, cat_array, item_array, LLInventoryModel::EXCLUDE_TRASH, is_category);

    response["outfits"] = llsd::toMap(cat_array,
        [](const LLPointer<LLViewerInventoryCategory> &cat)
        { return std::make_pair(cat->getUUID().asString(), cat->getName()); });
}

void LLAppearanceListener::getOutfitItems(LLSD const &data)
{
    Response response(LLSD(), data);
    LLUUID outfit_id(data["outfit_id"].asUUID());
    LLViewerInventoryCategory *cat = gInventory.getCategory(outfit_id);
    if (!cat || cat->getPreferredType() != LLFolderType::FT_OUTFIT)
    {
        return response.error(stringize("Couldn't find outfit ", outfit_id.asString()));
    }
    LLInventoryModel::cat_array_t  cat_array;
    LLInventoryModel::item_array_t item_array;

    LLFindOutfitItems collector = LLFindOutfitItems();
    gInventory.collectDescendentsIf(outfit_id, cat_array, item_array, LLInventoryModel::EXCLUDE_TRASH, collector);

    response["items"] = llsd::toMap(item_array,
        [](const LLPointer<LLViewerInventoryItem> &it)
        {
            return std::make_pair(
                it->getUUID().asString(),
                llsd::map(
                    "name", it->getName(),
                    "wearable_type", LLWearableType::getInstance()->getTypeName(it->isWearableType() ? it->getWearableType() : LLWearableType::WT_NONE),
                    "is_worn", get_is_item_worn(it)));
        });
}

void LLAppearanceListener::worn(LLSD const& data)
{
    Response response(LLSD(), data);
    if (!gInventory.isInventoryUsable())
    {
        return response.error("Inventory is not usable; log in and wait for initialization");
    }
    const LLUUID cof_id = LLAppearanceMgr::instance().getCOF();
    LLViewerInventoryCategory* cof = gInventory.getCategory(cof_id);
    if (!cof)
    {
        return response.error("Current outfit folder was not found");
    }

    LLInventoryModel::cat_array_t* categories = nullptr;
    LLInventoryModel::item_array_t* items = nullptr;
    gInventory.getDirectDescendentsOf(cof_id, categories, items);
    LLSD worn_items = LLSD::emptyArray();
    if (items)
    {
        for (LLViewerInventoryItem* item : *items)
        {
            LLViewerInventoryItem* target = gInventory.getItem(item->getLinkedUUID());
            if (!target)
            {
                target = item;
            }
            std::string attachment_point;
            if (LLViewerJointAttachment* point = RlvAttachPtLookup::getAttachPoint(target))
            {
                attachment_point = point->getName();
            }
            const LLWearableType::EType wearable = target->isWearableType()
                ? target->getWearableType() : LLWearableType::WT_NONE;
            worn_items.append(llsd::map(
                "id", item->getUUID(),
                "linked_id", item->getLinkedUUID(),
                "name", item->getName(),
                "wearable_type", LLWearableType::getInstance()->getTypeName(wearable),
                "attachment_point", attachment_point,
                "is_worn", get_is_item_worn(item)));
        }
    }
    response["cof_id"] = cof_id;
    response["complete"] = gInventory.isCategoryComplete(cof_id);
    response["outfit_dirty"] = LLAppearanceMgr::instance().isOutfitDirty();
    response["items"] = worn_items;
}
