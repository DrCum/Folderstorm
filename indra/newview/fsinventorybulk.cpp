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

/** Session-only assistant inventory planning, confirmed outcomes, and limited undo. */
#include "llviewerprecompiledheaders.h"
#include "fsinventorybulk.h"
#include "fsinventorybulkmodel.h"
#include "fsassistantpolicy.h"
#include "fseventapibridge.h"
#include "llaisapi.h"
#include "llcorehttputil.h"
#include "llagent.h"
#include "llinventoryfunctions.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "aoengine.h"
#include "fslslbridge.h"
#include "fsfloaterwearablefavorites.h"
#include "llviewerinventory.h"
#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "llcallbacklist.h"
#include "llsdutil.h"
#include "rlvactions.h"
#include "rlvlocks.h"
#include <algorithm>
#include <set>

extern bool gDisconnected;
namespace FSInventoryBulk
{
namespace
{
using namespace FSInventoryBulkModel;
auto store = std::make_shared<Store>();
std::vector<std::weak_ptr<fs_assistant::ExecutionGate>> native_gates;
std::string current_session;
bool ticker = false;
double now() { return static_cast<double>(totalTime()) / 1000000.0; }
std::map<std::string, std::uint64_t> revisions, external_revisions;
std::uint64_t revision_counter = 0;
std::map<std::string, std::string> material_states;
std::string materialState(const std::string& id)
{
    auto* object = gInventory.getObject(LLUUID(id));
    if (!object) return {};
    std::string state = object->getParentUUID().asString() + ":" + object->getName();
    if (auto* item = gInventory.getItem(LLUUID(id)))
        state += ":" + std::to_string(item->getType()) + ":" + std::to_string(item->getInventoryType()) +
            ":" + item->getLinkedUUID().asString() + ":" + std::to_string(item->isFinished()) +
            ":" + std::to_string(item->getPermissions().allowCopyBy(gAgent.getID())) +
            ":" + std::to_string(item->getPermissions().allowModifyBy(gAgent.getID()));
    else if (auto* category = gInventory.getCategory(LLUUID(id))) state += ":" + std::to_string(category->getPreferredType());
    return state;
}
class RevisionObserver final : public LLInventoryObserver
{
    void changed(U32 mask) override
    {
        if (!(mask & (LLInventoryObserver::LABEL | LLInventoryObserver::STRUCTURE | LLInventoryObserver::INTERNAL | LLInventoryObserver::REMOVE))) return;
        for (const LLUUID& id : gInventory.getChangedIDs())
        {
            auto found = revisions.find(id.asString());
            if (found != revisions.end())
            {
                const auto state = materialState(id.asString());
                if (state != material_states[id.asString()])
                {
                    found->second = ++revision_counter;
                    if (!AISAPI::isBulkUpdateFor(id)) external_revisions[id.asString()] = ++revision_counter;
                    material_states[id.asString()] = state;
                }
            }
        }
    }
};
RevisionObserver* revision_observer = nullptr;
std::uint64_t revision(const std::string& id)
{
    auto found = revisions.find(id);
    if (found != revisions.end()) return found->second;
    if (revisions.size() >= Store::MAX_TARGETS) { material_states.erase(revisions.begin()->first); external_revisions.erase(revisions.begin()->first); revisions.erase(revisions.begin()); }
    material_states[id] = materialState(id);
    external_revisions[id] = ++revision_counter;
    return revisions.emplace(id, ++revision_counter).first->second;
}
std::string sessionKey()
{
    if (gDisconnected || gAgent.getID().isNull() || gAgent.getSessionID().isNull() || !gInventory.isInventoryUsable()) return {};
    return gAgent.getID().asString() + ":" + gAgent.getSessionID().asString() + ":" + LLGridManager::instance().getGrid();
}
void sync()
{
    const auto next_session = sessionKey();
    if (next_session != current_session) { revisions.clear(); external_revisions.clear(); material_states.clear(); }
    current_session = next_session;
    store->session(current_session);
    if (!revision_observer) { revision_observer = new RevisionObserver(); gInventory.addObserver(revision_observer); }
    if (!ticker)
    {
        ticker = true;
        doPeriodically([]() { sync(); store->history(now()); return false; }, 1.0f);
    }
}
std::string label(std::string text, std::size_t limit = 256) { return boundedLabel(std::move(text), limit); }
bool agentObject(const LLUUID& id)
{
    return id == gInventory.getRootFolderID() || gInventory.isObjectDescendentOf(id, gInventory.getRootFolderID());
}
Snapshot snapshot(const LLUUID& id)
{
    Snapshot s; s.id = id.asString();
    auto* object = gInventory.getObject(id);
    if (!object || !agentObject(id)) return s;
    s.name = object->getName(); s.revision = revision(s.id); s.external_revision = external_revisions[s.id]; s.parent = object->getParentUUID().asString();
    s.path = label(make_path(object), 1024);
    if (auto* parent = gInventory.getCategory(object->getParentUUID())) s.parent_label = label(make_path(parent), 1024);
    if (auto* item = gInventory.getItem(id))
    {
        s.link = item->getIsLinkType(); s.linked_id = s.link ? item->getLinkedUUID().asString() : std::string();
        s.type = item->getType(); s.copyable = item->getPermissions().allowCopyBy(gAgent.getID());
        s.modifiable = item->getPermissions().allowModifyBy(gAgent.getID());
        s.ordinary = !s.link && item->isFinished();
    }
    else if (auto* category = gInventory.getCategory(id))
    {
        s.folder = true; s.type = category->getPreferredType(); s.ordinary = s.type == LLFolderType::FT_NONE;
    }
    return s;
}
std::string destinationError(const std::string& id)
{
    auto* folder = gInventory.getCategory(LLUUID(id));
    if (!folder || !agentObject(folder->getUUID())) return "Destination must be a folder in the agent inventory";
    const auto type = folder->getPreferredType();
    if (type == LLFolderType::FT_CURRENT_OUTFIT || type == LLFolderType::FT_MARKETPLACE_LISTINGS ||
        type == LLFolderType::FT_MARKETPLACE_STOCK || type == LLFolderType::FT_LOST_AND_FOUND)
        return "That special destination is not supported by the automation API";
    return {};
}
bool ordinaryFolder(const std::string& id)
{
    auto* folder = gInventory.getCategory(LLUUID(id));
    if (!folder || !agentObject(folder->getUUID()) || folder->getPreferredType() != LLFolderType::FT_NONE) return false;
    for (const auto type : {LLFolderType::FT_TRASH, LLFolderType::FT_CURRENT_OUTFIT, LLFolderType::FT_LOST_AND_FOUND,
                           LLFolderType::FT_MARKETPLACE_LISTINGS, LLFolderType::FT_MARKETPLACE_STOCK})
    {
        const LLUUID root = gInventory.findCategoryUUIDForType(type);
        if (root.notNull() && (folder->getUUID() == root || gInventory.isObjectDescendentOf(folder->getUUID(), root))) return false;
    }
    return !((gSavedPerAccountSettings.getBOOL("LockAOFolders") && gInventory.isObjectDescendentOf(folder->getUUID(), AOEngine::instance().getAOFolder())) ||
        (gSavedPerAccountSettings.getBOOL("LockBridgeFolder") && gInventory.isObjectDescendentOf(folder->getUUID(), FSLSLBridge::instance().getBridgeFolder())) ||
        (gSavedPerAccountSettings.getBOOL("LockWearableFavoritesFolders") && gInventory.isObjectDescendentOf(folder->getUUID(), FSFloaterWearableFavorites::getFavoritesFolder())));
}
std::string inverseRule(const Row& row, Kind kind)
{
    if (kind != Kind::Move) return {};
    auto* item = gInventory.getItem(LLUUID(row.before.id));
    if (!item || item->getIsLinkType() || !item->getPermissions().allowCopyBy(gAgent.getID()) ||
        !ordinaryFolder(item->getParentUUID().asString()) || !ordinaryFolder(row.destination))
        return "Move undo requires a copyable ordinary item and current ordinary folders outside protected or Trash trees";
    return {};
}
std::string rule(const Row& row, Kind kind)
{
    const LLUUID id(row.before.id);
    auto* object = gInventory.getObject(id);
    if (!object || !agentObject(id)) return "Inventory object was not found in agent inventory";
    if (kind == Kind::Rename)
    {
        if (row.after_name.empty() || row.after_name.size() > 255) return "Name must contain 1 to 255 bytes";
        if (auto* item = gInventory.getItem(id))
        {
            if (!item->isFinished() || item->getInventoryType() == LLInventoryType::IT_CALLINGCARD ||
                !item->getPermissions().allowModifyBy(gAgent.getID()) ||
                (RlvActions::isRlvEnabled() && !RlvFolderLocks::instance().canRenameItem(id)))
                return "Inventory object cannot be renamed";
        }
        else if (!get_is_category_renameable(&gInventory, id)) return "Folder cannot be renamed";
    }
    else
    {
        if ((gSavedPerAccountSettings.getBOOL("LockAOFolders") && gInventory.isObjectDescendentOf(id, AOEngine::instance().getAOFolder())) ||
            (gSavedPerAccountSettings.getBOOL("LockBridgeFolder") && gInventory.isObjectDescendentOf(id, FSLSLBridge::instance().getBridgeFolder())) ||
            (gSavedPerAccountSettings.getBOOL("LockWearableFavoritesFolders") && gInventory.isObjectDescendentOf(id, FSFloaterWearableFavorites::getFavoritesFolder())))
            return "Object is in a locked viewer folder";
        auto error = destinationError(row.destination); if (!error.empty()) return error;
        const LLUUID dest(row.destination);
        if (id == dest || (gInventory.getCategory(id) && gInventory.isObjectDescendentOf(dest, id)))
            return "A folder cannot be moved into itself or its descendant";
        if (gInventory.getItem(id))
        {
            if (!get_is_item_removable(&gInventory, id, false) ||
                (RlvActions::isRlvEnabled() && !RlvFolderLocks::instance().canMoveItem(id, dest)))
                return "Item is protected or locked";
        }
        else if (!get_is_category_removable(&gInventory, id) ||
            (RlvActions::isRlvEnabled() && !RlvFolderLocks::instance().canMoveFolder(id, dest)))
            return "Folder is protected or locked";
    }
    return {};
}
std::string classFor(Kind kind) { return kind == Kind::Rename ? "edit" : "move"; }
LLSD rowData(const Row& row)
{
    return llsd::map("id", row.before.id, "name", label(row.before.name), "path", row.before.path,
        "before_name", label(row.before.name), "after_name", label(row.after_name),
        "before_parent_id", row.before.parent, "after_parent_id", row.destination,
        "source", row.before.parent_label, "destination", row.destination_label,
        "eligible", row.skip.empty(), "skip_reason", row.skip, "undo_eligible", row.undo_eligible);
}
LLSD planData(const Plan& p)
{
    const auto permission = FSEventAPIBridge::getPermissionLevel(classFor(p.kind));
    LLSD data = llsd::map("plan_id", p.id, "expires_in_seconds", std::max(0, static_cast<int>(p.expires - now())),
        "kind", p.kind == Kind::Rename ? "rename" : "move", "required_class", classFor(p.kind),
        "count", static_cast<int>(p.rows.size()), "rows", LLSD::emptyArray(),
        "execution_allowed", permission != "deny", "current_permission", permission);
    int eligible = 0;
    for (const auto& row : p.rows) { data["rows"].append(rowData(row)); eligible += row.skip.empty(); }
    data["eligible_count"] = eligible; data["skipped_count"] = static_cast<int>(p.rows.size()) - eligible;
    if (!p.inverse_of.empty()) data["inverse_of"] = p.inverse_of;
    data["status"] = "prepared";
    if (!p.operation_id.empty()) { data["operation_id"] = p.operation_id; auto entry = store->record(p.operation_id, now()); data["status"] = entry && entry->terminal ? "completed" : "executing"; }
    return data;
}
LLSD outcomeData(const History& entry, bool include_labels)
{
    LLSD data = llsd::map("operation_id", entry.id, "kind", entry.kind == Kind::Rename ? "rename" : "move",
        "status", entry.terminal ? "completed" : "executing", "count", static_cast<int>(entry.rows.size()),
        "started", entry.started, "results", LLSD::emptyArray());
    if (!entry.inverse_of.empty()) data["inverse_of"] = entry.inverse_of;
    for (std::size_t i = 0; i < entry.rows.size(); ++i)
    {
        const auto& result = entry.results[i];
        LLSD row = include_labels ? rowData(entry.rows[i]) : llsd::map("id", entry.rows[i].before.id);
        row["status"] = result.status;
        row["ok"] = result.status == "confirmed" || result.accepted;
        row["undo_available"] = store->undoAvailable(entry, i) && snapshot(LLUUID(entry.rows[i].before.id)) == result.after;
        if (!result.error.empty()) row["error"] = result.error;
        data["results"].append(row);
    }
    int eligible = 0, submitted = 0, confirmed = 0, skipped = 0;
    for (std::size_t i = 0; i < entry.rows.size(); ++i)
    {
        eligible += entry.rows[i].skip.empty();
        submitted += entry.results[i].status == "submitted" || entry.results[i].status == "confirmed" || entry.results[i].status == "unconfirmed";
        confirmed += entry.results[i].status == "confirmed";
        skipped += entry.results[i].status == "skipped";
    }
    data["eligible_count"] = eligible; data["skipped_count"] = skipped;
    data["submitted_count"] = submitted; data["confirmed_count"] = confirmed;
    data["required_class"] = classFor(entry.kind); data["origin"] = "local_assistant";
    if (include_labels) { data["rows"] = data["results"]; data.erase("results"); }
    return data;
}
std::string validateRow(const Row& row, Kind kind, bool include_paths)
{
    const Snapshot current = snapshot(LLUUID(row.before.id));
    if (!(current == row.before) || (include_paths && current.path != row.before.path))
        return "Inventory changed since preview; request a new preview";
    if (kind == Kind::Move)
    {
        auto* dest = gInventory.getCategory(LLUUID(row.destination));
        if (!dest || dest->getPreferredType() != row.destination_type || (include_paths && label(make_path(dest), 1024) != row.destination_label))
            return "Destination changed since preview; request a new preview";
    }
    return rule(row, kind);
}
bool authoritative(const LLSD& response, const Snapshot& expected, const std::string& name, const std::string& parent)
{
    // GET bodies must identify the object and provide its material state.
    // A UUID callback or the optimistic cache alone never counts as proof.
    const char* key = expected.folder ? "category_id" : "item_id";
    return response.isMap() && response.has(key) && response[key].asUUID() == LLUUID(expected.id) &&
        response.has("name") && response["name"].asString() == name &&
        response.has("parent_id") && response["parent_id"].asUUID() == LLUUID(parent);
}
void perform(const Row& row, Kind kind, bool inverse, std::function<bool()> alive,
             std::function<void()> submitted, std::function<void(Outcome)> completed)
{
    const auto session = current_session;
    auto same_session = [session]() { return !session.empty() && sessionKey() == session; };
    auto failure = [completed](const std::string& status, const std::string& error, bool accepted = false)
    { Outcome result; result.status = status; result.error = error; result.accepted = accepted; completed(result); };
    if (!same_session() || !alive()) { failure("cancelled_before_submission", "Execution was cancelled before submission"); return; }
    if (!AISAPI::isAvailable())
    {
        if (inverse) { failure("skipped", "This grid cannot confirm safe undo"); return; }
        // Preserve existing UDP/OpenSim behavior, while being explicit that its
        // optimistic cache and UUID callbacks do not confirm a server change.
        submitted();
        if (kind == Kind::Rename)
        {
            LLSD updates; updates["name"] = row.after_name;
            if (row.before.folder) rename_category(&gInventory, LLUUID(row.before.id), row.after_name, nullptr);
            else update_inventory_item(LLUUID(row.before.id), updates, nullptr);
        }
        else if (auto* item = gInventory.getItem(LLUUID(row.before.id)))
            gInventory.changeItemParent(item, LLUUID(row.destination), false);
        else if (auto* folder = gInventory.getCategory(LLUUID(row.before.id)))
            gInventory.changeCategoryParent(folder, LLUUID(row.destination), false);
        failure("unconfirmed", "Submitted using a protocol without authoritative confirmation; undo unavailable", true);
        return;
    }
    AISAPI::BulkRequest(LLUUID(row.before.id), row.before.folder, LLSD(), same_session, alive,
        [row, kind, inverse, alive, same_session, submitted, completed, failure](bool success, const LLSD& response)
        {
            if (!same_session() || !alive()) { failure("cancelled_before_submission", "Execution was cancelled before submission"); return; }
            const auto error = inverse && !inverseRule(row, kind).empty() ? inverseRule(row, kind) : validateRow(row, kind, false);
            if (!success || !authoritative(response, row.before, row.before.name, row.before.parent) || !error.empty())
            { failure("skipped", "Server state changed or could not be verified before submission"); return; }
            LLSD updates;
            if (kind == Kind::Rename) updates["name"] = row.after_name;
            else updates["parent_id"] = LLUUID(row.destination);
            auto did_submit = std::make_shared<bool>(false);
            AISAPI::BulkRequest(LLUUID(row.before.id), row.before.folder, updates, same_session, [row, kind, inverse, alive]() { return alive() && (!inverse || inverseRule(row, kind).empty()) && validateRow(row, kind, false).empty(); },
                [row, kind, alive, same_session, did_submit, completed, failure](bool accepted, const LLSD& response)
                {
                    if (!same_session()) { failure("unconfirmed", "Session ended; submitted work may have completed", accepted); return; }
                    if (!*did_submit) { failure("cancelled_before_submission", "Execution stopped before mutation submission"); return; }
                    if (!accepted)
                    {
                        const auto status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(response[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
                        const int code = status.getType();
                        failure(code >= 400 && code < 500 ? "failed" : "unconfirmed", code >= 400 && code < 500 ? "Server rejected the inventory update" : "Server update was not acknowledged; inspect inventory before retrying"); return;
                    }
                    // Read back even after cancellation: reconciliation does not
                    // submit more mutations or authorize a new inverse action.
                    AISAPI::BulkRequest(LLUUID(row.before.id), row.before.folder, LLSD(), same_session,
                        []() { return true; }, [row, kind, completed, failure](bool success, const LLSD& response)
                        {
                            const auto name = kind == Kind::Rename ? row.after_name : row.before.name;
                            const auto parent = kind == Kind::Move ? row.destination : row.before.parent;
                            if (!success || !authoritative(response, row.before, name, parent))
                            { failure("unconfirmed", "Update acknowledged but final server state could not be verified", true); return; }
                            Outcome result; result.status = "confirmed"; result.accepted = true;
                            result.after = snapshot(LLUUID(row.before.id));
                            if (result.after.name != name || result.after.parent != parent)
                            { failure("unconfirmed", "Final inventory state changed during verification", true); return; }
                            completed(result);
                        });
                }, [did_submit, submitted]() { *did_submit = true; submitted(); });
        });
}
}

LLSD preview(const LLSD& request, std::string& error)
{
    sync();
    if (current_session.empty()) { error = "Inventory session is unavailable"; return {}; }
    auto prepared = std::make_shared<Plan>();
    prepared->id = LLUUID::generateNewID().asString(); prepared->session = current_session;
    const auto op = request["op"].asString();
    if (op == "previewBulkUndo")
    {
        std::vector<std::string> ids;
        if (request.has("ids") && !request["ids"].isArray()) { error = "ids must be an array"; return {}; }
        for (const LLSD& id : llsd::inArray(request["ids"])) ids.push_back(id.asUUID().asString());
        if (!store->previewUndo(request["operation_id"].asUUID().asString(), ids, prepared->id, now(), error)) return {};
        auto inverse = store->plan(prepared->id, now());
        for (auto& row : inverse->rows)
            if (auto* dest = gInventory.getCategory(LLUUID(row.destination))) row.destination_type = dest->getPreferredType();
        if (!validate(prepared->id, error)) return {};
        return plan(prepared->id, error);
    }
    const bool rename = op == "previewBatchRename" || op == "batchRename" || op == "rename";
    const bool move = op == "previewBatchMove" || op == "batchMove" || op == "move";
    if (!rename && !move) { error = "Unknown bulk preview operation"; return {}; }
    prepared->kind = rename ? Kind::Rename : Kind::Move;
    LLSD entries = rename ? request["items"] : request["ids"];
    if (op == "rename") entries = LLSD::emptyArray().append(llsd::map("id", request["id"], "name", request["name"]));
    if (op == "move") entries = LLSD::emptyArray().append(request["id"]);
    if (!entries.isArray() || entries.size() < 1 || entries.size() > 50)
    { error = "A preview must contain 1 to 50 requested rows"; return {}; }
    const std::string destination = request["parent_id"].asUUID().asString();
    for (const LLSD& entry : llsd::inArray(entries))
    {
        const auto id = (rename ? entry["id"] : entry).asUUID();
        if (id.isNull()) { error = "Each target must be a non-null inventory UUID"; return {}; }
        Row row; row.before = snapshot(id); row.destination = rename ? row.before.parent : destination;
        row.after_name = rename ? entry["name"].asString() : row.before.name;
        LLInventoryObject::correctInventoryName(row.after_name);
        if (auto* dest = gInventory.getCategory(LLUUID(row.destination))) { row.destination_label = label(make_path(dest), 1024); row.destination_type = dest->getPreferredType(); }
        row.skip = rule(row, prepared->kind);
        if (row.skip.empty() && ((rename && row.after_name == row.before.name) || (!rename && row.destination == row.before.parent)))
            row.skip = "Target already has the requested state";
        row.undo_eligible = row.skip.empty() && row.before.ordinary && !row.before.link &&
            (rename || (!row.before.folder && row.before.copyable && ordinaryFolder(row.before.parent) && ordinaryFolder(row.destination)));
        prepared->rows.push_back(row);
    }
    if (move)
        for (const auto& a : prepared->rows) for (const auto& b : prepared->rows)
            if (a.before.id != b.before.id && a.before.folder && gInventory.isObjectDescendentOf(LLUUID(b.before.id), LLUUID(a.before.id)))
            { error = "Choose a folder or its descendants, not both"; return {}; }
    if (!store->prepare(prepared, now(), error)) return {};
    return planData(*prepared);
}
LLSD plan(const std::string& id, std::string& error)
{
    sync(); auto prepared = store->plan(id, now());
    if (!prepared) { error = "Plan is missing, expired, or belongs to another session"; return {}; }
    return planData(*prepared);
}
std::string requiredClass(const std::string& id, std::string& error)
{
    sync(); auto prepared = store->plan(id, now());
    if (!prepared) { error = "Plan is missing, expired, or belongs to another session"; return {}; }
    return classFor(prepared->kind);
}
bool validate(const std::string& id, std::string& error)
{
    sync(); auto prepared = store->plan(id, now());
    if (!prepared) { error = "Plan is missing, expired, or belongs to another session"; return false; }
    if (!prepared->operation_id.empty()) return true; // Idempotent result lookup; never redispatch.
    for (const auto& row : prepared->rows)
    {
        if (!row.skip.empty())
        {
            if (!(snapshot(LLUUID(row.before.id)) == row.before)) { error = "Inventory changed since preview; request a new preview"; return false; }
            auto current_skip = rule(row, prepared->kind);
            if (current_skip.empty() && ((prepared->kind == Kind::Rename && row.after_name == row.before.name) || (prepared->kind == Kind::Move && row.destination == row.before.parent))) current_skip = "Target already has the requested state";
            if (current_skip != row.skip) { error = "Inventory eligibility changed since preview"; return false; }
            continue;
        }
        error = validateRow(row, prepared->kind, true);
        if (!error.empty()) return false;
        if (!prepared->inverse_of.empty())
        {
            error = inverseRule(row, prepared->kind); if (!error.empty()) return false;
            auto original = store->record(prepared->inverse_of, now());
            bool eligible = false;
            if (original) for (std::size_t i = 0; i < original->rows.size(); ++i)
                if (original->rows[i].before.id == row.before.id) eligible = store->undoAvailable(*original, i);
            if (!eligible) { error = "Undo is no longer eligible; request a fresh preview"; return false; }
        }
    }
    return true;
}
LLSD summary(const std::string& id, std::string& error)
{
    auto data = plan(id, error); if (!error.empty()) return {};
    LLSD out = llsd::map("op", data["kind"].asString() == "rename" ? "batchRename" : "batchMove",
        "subject_count", data["count"], "subjects", LLSD::emptyArray(), "consequences", LLSD::emptyArray());
    for (LLSD row : llsd::inArray(data["rows"]))
    {
        if (!row["eligible"].asBoolean()) row["skip_error"] = row["skip_reason"];
        row["new_name"] = row["after_name"];
        out["subjects"].append(row);
    }
    return out;
}
LLSD history(const LLSD& request, std::string& error)
{
    sync();
    if (request.has("operation_id"))
    {
        auto entry = store->record(request["operation_id"].asUUID().asString(), now());
        if (!entry) { error = "History record is missing or expired"; return {}; }
        return llsd::map("operation", outcomeData(*entry, true));
    }
    int offset = request.has("offset") ? request["offset"].asInteger() : 0;
    int limit = request.has("limit") ? request["limit"].asInteger() : 20;
    if (offset < 0 || limit < 1 || limit > 50) { error = "offset must be nonnegative and limit between 1 and 50"; return {}; }
    auto records = store->history(now());
    LLSD data = llsd::map("operations", LLSD::emptyArray(), "total", static_cast<int>(records.size()), "offset", offset);
    for (std::size_t i = offset; i < records.size() && i < static_cast<std::size_t>(offset) + limit; ++i)
        data["operations"].append(outcomeData(*records[i], true));
    return data;
}
void execute(const std::string& id, const FSAssistantExecutionContext& context,
             std::function<void(const LLSD&)> completed)
{
    std::string error;
    if (!context.allowed || !context.allowed() || !validate(id, error))
    { completed(llsd::map("error", error.empty() ? "Execution is not authorized" : error)); return; }
    const auto prepared = store->plan(id, now());
    const bool consumed = !prepared->operation_id.empty();
    auto entry = store->claim(id, LLUUID::generateNewID().asString(), now(), error);
    if (!entry) { completed(llsd::map("error", error)); return; }
    if (consumed) { completed(outcomeData(*entry, false)); return; }
    Backend backend;
    backend.now = now; backend.allowed = context.allowed;
    const auto kind = entry->kind;
    const bool inverse = !entry->inverse_of.empty();
    backend.validate = [kind, inverse](const Row& row) { auto error = inverse ? inverseRule(row, kind) : std::string(); return error.empty() ? validateRow(row, kind, false) : error; };
    backend.perform = [kind](const Row& row, bool inverse, std::function<bool()> alive, std::function<void()> submitted, std::function<void(Outcome)> done)
    { perform(row, kind, inverse, std::move(alive), std::move(submitted), std::move(done)); };
    auto operation = std::make_shared<Operation>(store, entry, std::move(backend),
        context.deadline > 0 ? std::min(now() + 30, context.deadline - 0.5) : now() + 30,
        [completed](const History& record) { completed(outcomeData(record, false)); });
    if (!operation->tick()) doPeriodically([operation]() { sync(); return operation->tick(); }, 0.1f);
}
void executeNative(const std::string& id, std::function<void(const LLSD&)> completed)
{
    std::string error; const auto kind = requiredClass(id, error);
    if (!error.empty() || FSEventAPIBridge::getPermissionLevel(kind) == "deny")
    { completed(llsd::map("error", error.empty() ? "This inventory action is set to Never" : error)); return; }
    auto gate = std::make_shared<fs_assistant::ExecutionGate>(std::vector<std::string>{kind});
    native_gates.erase(std::remove_if(native_gates.begin(), native_gates.end(), [](const auto& weak) { return weak.expired(); }), native_gates.end());
    native_gates.emplace_back(gate);
    const auto session = current_session;
    FSAssistantExecutionContext context; context.deadline = now() + 30.5;
    context.allowed = [gate, kind, session]()
    { return gate->allowed() && sessionKey() == session && FSEventAPIBridge::getPermissionLevel(kind) != "deny"; };
    execute(id, context, std::move(completed));
}
void clear() { sync(); store->clear(); }
void stop(const std::string& id) { sync(); store->stop(id); }
void permissionsChanged()
{
    for (auto it = native_gates.begin(); it != native_gates.end();)
    {
        if (auto gate = it->lock()) { gate->observe({FSEventAPIBridge::getPermissionLevel(gate->classes().front())}); ++it; }
        else it = native_gates.erase(it);
    }
}
bool prepareLegacy(const LLSD& request, std::string& id, std::string& error)
{
    auto result = preview(request, error); if (!error.empty()) return false;
    id = result["plan_id"].asString(); return true;
}
LLSD legacyResult(const LLSD& request, const LLSD& result)
{
    LLSD out = result;
    const auto op = request["op"].asString();
    if (op == "batchRename")
    {
        std::map<LLUUID, std::string> names;
        for (const LLSD& item : llsd::inArray(request["items"])) { auto name = item["name"].asString(); LLInventoryObject::correctInventoryName(name); names[item["id"].asUUID()] = name; }
        for (LLSD& item : llsd::inArray(out["results"])) if (item["ok"].asBoolean()) item["name"] = names[item["id"].asUUID()];
    }
    if (op == "rename" || op == "move")
    {
        if (out["results"].size())
        {
            const auto& row = out["results"][0]; out["ok"] = row["ok"];
            if (row.has("error") && !row["ok"].asBoolean()) out["error"] = row["error"];
            if (row["ok"].asBoolean()) if (auto* object = gInventory.getObject(request["id"].asUUID()))
                out["object"] = llsd::map("id", object->getUUID(), "name", object->getName(),
                    "parent_id", object->getParentUUID(), "path", make_path(object));
        }
    }
    return out;
}
}
