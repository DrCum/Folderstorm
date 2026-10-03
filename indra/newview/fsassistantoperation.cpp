#include "llviewerprecompiledheaders.h"
#include "fsassistantoperation.h"

#include "llagent.h"
#include "llappearancemgr.h"
#include "llinventorymodel.h"
#include "llinventoryfunctions.h"
#include "llsdutil.h"
#include "llviewerinventory.h"

#include <set>

namespace
{
std::string safe_text(std::string value)
{
    for (char& character : value)
    {
        if (static_cast<unsigned char>(character) < 32 || character == 127) character = ' ';
    }
    return value;
}

std::string object_path(const LLInventoryObject* object)
{
    std::string result;
    std::set<LLUUID> seen;
    while (object && seen.insert(object->getUUID()).second)
    {
        const std::string name = safe_text(object->getName());
        result = result.empty() ? name : name + " / " + result;
        object = gInventory.getObject(object->getParentUUID());
    }
    return result;
}

LLSD subject(const LLUUID& id)
{
    LLSD result = llsd::map("id", id);
    if (const LLInventoryObject* object = gInventory.getObject(id))
    {
        result["name"] = safe_text(object->getName());
        result["path"] = object_path(object);
        result["parent_id"] = object->getParentUUID();
        result["source"] = object_path(gInventory.getObject(object->getParentUUID()));
    }
    else
    {
        result["name"] = id.asString();
        result["name_unavailable"] = true;
    }
    return result;
}

std::string folder_label(const LLUUID& id)
{
    const LLInventoryObject* object = gInventory.getObject(id);
    return object ? object_path(object) : id.asString();
}

void add_subject(LLSD& summary, const LLUUID& id, const std::string& new_name = {})
{
    LLSD entry = subject(id);
    if (!new_name.empty()) entry["after_name"] = safe_text(new_name);
    summary["subjects"].append(entry);
}
}

bool fs_prepare_assistant_operation(FSAssistantPreparedOperation& operation, std::string& error)
{
    LLSD& request = operation.request;
    const std::string api = request["api"].asString();
    const std::string op = request["op"].asString();
    LLSD summary = llsd::map("op", op, "subjects", LLSD::emptyArray(), "consequences", LLSD::emptyArray());
    if (api == "LLInventory")
    {
        if ((op == "move" || op == "copy") && request["parent_id"].asUUID().isNull())
            request["parent_id"] = request["dest_folder_id"];
        if (op == "batchMove" || op == "batchCopy")
        {
            const LLSD& values = request["ids"];
            if (values.isArray())
            {
                for (const LLSD& value : llsd::inArray(values))
                    if (value.asUUID().notNull()) add_subject(summary, value.asUUID());
            }
            else if (values.asUUID().notNull()) add_subject(summary, values.asUUID());
            if (summary["subjects"].size() < 1 || summary["subjects"].size() > 50)
            {
                error = "ids must contain between 1 and 50 UUIDs";
                return false;
            }
        }
        else if (op == "batchRename")
        {
            if (!request["items"].isArray() || request["items"].size() < 1 || request["items"].size() > 50)
            {
                error = "items must contain between 1 and 50 {id, name} entries";
                return false;
            }
            for (S32 index = 0; index < request["items"].size(); ++index)
            {
                LLSD& row = request["items"][index];
                std::string name = row["name"].asString();
                LLStringUtil::trim(name);
                row["name"] = name;
                add_subject(summary, row["id"].asUUID(), name);
                if (name.empty()) summary["subjects"][index]["skip_error"] = "Name is empty";
            }
        }
        else if (op == "createFolder" || op == "createItem")
        {
            if (op == "createFolder")
            {
                std::string name = request["name"].asString();
                LLStringUtil::trim(name);
                if (name.empty()) { error = "Folder name must not be empty"; return false; }
                request["name"] = name;
            }
            summary["value"] = safe_text(request["name"].asString());
            if (op == "createItem") summary["value"] = safe_text(request["type"].asString()) + ": " + summary["value"].asString();
        }
        else
        {
            std::string name;
            if (op == "rename")
            {
                name = request["name"].asString();
                LLStringUtil::trim(name);
                if (name.empty()) { error = "Name must not be empty"; return false; }
                request["name"] = name;
            }
            const LLUUID id = request["id"].asUUID();
            if (id.isNull()) { error = "Inventory object id is required"; return false; }
            add_subject(summary, id, name);
        }

        if (request.has("parent_id")) summary["destination"] = folder_label(request["parent_id"].asUUID());
        if (op == "restore")
        {
            if (const LLInventoryObject* object = gInventory.getObject(request["id"].asUUID()))
            {
                const LLViewerInventoryItem* item = gInventory.getItem(object->getUUID());
                const LLFolderType::EType type = item && item->getInventoryType() == LLInventoryType::IT_SNAPSHOT
                    ? LLFolderType::FT_SNAPSHOT_CATEGORY : LLFolderType::assetTypeToFolderType(object->getType());
                summary["destination"] = folder_label(gInventory.findCategoryUUIDForType(type));
            }
        }
        if (op == "copy" || op == "batchCopy")
        {
            summary["copy_policy"] = request.has("policy") && !request["policy"].asString().empty() ? safe_text(request["policy"].asString()) : "include_no_copy";
            summary["consequences"].append("copy_no_copy");
            if (request.has("new_name")) summary["value"] = safe_text(request["new_name"].asString());
        }
        if (op == "trash") summary["consequences"].append("trash_restore");
        if (op == "setDescription") summary["value"] = safe_text(request["desc"].asString());
        if (op == "setThumbnail") summary["value"] = request["thumbnail_id"].asString();
        if (op == "setFavorite") summary["value"] = request["favorite"].asBoolean() ? "true" : "false";
        if (op == "batchMove" || op == "batchRename" || op == "batchCopy") summary["consequences"].append("partial_batch");
    }
    else if (api == "LLAppearance")
    {
        if (op == "wearOutfit")
        {
            // Match wear_category(): library name takes precedence, then UUID.
            LLUUID folder;
            if (request.has("folder_name")) folder = findDescendentCategoryIDByName(gInventory.getLibraryRootFolderID(), request["folder_name"].asString());
            if (folder.isNull()) folder = request["folder_id"].asUUID();
            if (!gInventory.getCategory(folder)) { error = "Outfit folder was not found"; return false; }
            request.erase("folder_name");
            request["folder_id"] = folder;
            add_subject(summary, folder);
            summary["consequences"].append(request["append"].asBoolean() ? "wear_append" : "wear_replace");
        }
        else
        {
            const LLSD& ids = request["items_id"];
            if (ids.isArray())
            {
                for (const LLSD& id : llsd::inArray(ids)) add_subject(summary, id.asUUID());
            }
            else add_subject(summary, ids.asUUID());
            if (op == "wearItems") summary["consequences"].append(request["replace"].asBoolean() ? "wear_replace_items" : "wear_add");
        }
    }
    else if (api == "LLCamera")
    {
        if (op == "setPose") summary["value"] = safe_text(request["preset"].asString());
        if (op == "set")
        {
            // Position and focus are numerical vectors, never remote labels.
            summary["position"] = request["position"];
            summary["focus"] = request["focus"];
        }
    }
    summary["subject_count"] = std::max<S32>(1, static_cast<S32>(summary["subjects"].size()));
    operation.summary = summary;
    // Labels/paths are conservatively included: a changed identity/location or
    // rename-before value must not be hidden behind a previously approved label.
    operation.validation = summary;
    operation.validation["session_id"] = gAgent.getSessionID();
    return true;
}

bool fs_validate_assistant_operation(const FSAssistantPreparedOperation& operation, std::string& error)
{
    FSAssistantPreparedOperation fresh;
    fresh.request = operation.request;
    if (!fs_prepare_assistant_operation(fresh, error)) return false;
    if (!llsd_equals(operation.validation, fresh.validation))
    {
        error = "The objects or destinations changed while awaiting approval. Request a fresh confirmation.";
        return false;
    }
    return true;
}
