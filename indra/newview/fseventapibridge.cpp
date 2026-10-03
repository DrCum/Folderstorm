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

/**
 * @file fseventapibridge.cpp
 * @brief Authenticated loopback bridge for viewer Event APIs.
 *
 * Action classes are enforced here, before an event is posted. purge and
 * emptyTrash are hard-denied. A settings value cannot enable them.
 */

#include "llviewerprecompiledheaders.h"

#include "fseventapibridge.h"
#include "fsassistantpermissions.h"
#include "fsassistantapproval.h"
#include "fsassistantoperation.h"
#include "fsassistantpolicy.h"
#include "fslinkreplacement.h"

#include "llapr.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "lleventapi.h"
#include "llevents.h"
#include "llfile.h"
#include "llfloaterreg.h"
#include "llhttpconstants.h"
#include "llhttpnode.h"
#include "lliohttpserver.h"
#include "lliosocket.h"
#include "llsdjson.h"
#include "lltimer.h"
#include "lluuid.h"
#include "llviewercontrol.h"
#include "fssnapshotupload.h"
#include "llagentbenefits.h"
#include "llcameralistener.h"
#include "llinventorylistener.h"
#include "llstring.h"

#include <boost/json.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <deque>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

#if !LL_WINDOWS
#include <sys/stat.h>
#endif

namespace
{
constexpr const char* BRIDGE_PATH = "/firestorm/event-api";
constexpr const char* API_VERSION = "1";
constexpr const char* PERMISSION_SETTING = "LocalEventAPIPermissionClasses";
constexpr std::size_t TOKEN_BYTES = 32;
constexpr std::size_t MAX_REQUEST_BYTES = 1024 * 1024;
constexpr F64 ASK_WAIT_SECONDS = 60.0;
constexpr F64 CHAIN_GUARD_SECONDS = 70.0;

constexpr const char* ERR_UNKNOWN_OP = "Event API operation is not classified for the local MCP bridge";
constexpr const char* ERR_PERMANENT = "Permanent delete is not available";
constexpr const char* ERR_NOT_ALLOWED = "Action is not allowed";
constexpr const char* ERR_DENIED = "Action was denied";
constexpr const char* ERR_TIMED_OUT = "Action confirmation timed out";

int gPolicyGeneration = 1;

using ActionClass = fs_assistant::ActionClass;
using ClassDef = fs_assistant::PermissionClass;
const auto& CLASS_DEFS = fs_assistant::permissionClasses;

const ClassDef* find_def(ActionClass kind)
{
    for (const ClassDef& def : CLASS_DEFS)
    {
        if (def.kind == kind)
        {
            return &def;
        }
    }
    return nullptr;
}

const ClassDef* find_def(const std::string& id)
{
    for (const ClassDef& def : CLASS_DEFS)
        if (id == def.id) return &def;
    return nullptr;
}

bool level_allowed(const ClassDef& def, const std::string& level)
{
    return fs_assistant::permissionLevelAllowed(def, level);
}

std::string effective_level(ActionClass kind)
{
    const ClassDef* def = find_def(kind);
    if (!def)
    {
        return "deny";
    }
    const LLSD configured = gSavedSettings.getLLSD(PERMISSION_SETTING);
    if (configured.isMap() && configured.has(def->id))
    {
        const std::string raw = configured[def->id].asString();
        if (level_allowed(*def, raw))
        {
            return raw;
        }
    }
    return def->fallback;
}

LLSD effective_permissions()
{
    LLSD permissions = LLSD::emptyMap();
    for (const ClassDef& def : CLASS_DEFS)
    {
        permissions[def.id] = effective_level(def.kind);
    }
    return permissions;
}

std::vector<std::string> effective_levels(const std::vector<std::string>& classes)
{
    std::vector<std::string> levels;
    for (const std::string& id : classes)
    {
        const ClassDef* def = find_def(id);
        levels.push_back(def ? effective_level(def->kind) : "deny");
    }
    return levels;
}

LLSD policy_error_details(const std::string& op, const std::vector<std::string>& classes)
{
    LLSD extra = llsd::map("op", op, "policy_generation", gPolicyGeneration);
    for (const std::string& id : classes)
    {
        extra["required_classes"].append(id);
        const ClassDef* def = find_def(id);
        if (!def || effective_level(def->kind) == "deny")
        {
            extra["denied_classes"].append(id);
            if (!extra.has("class")) extra["class"] = id;
        }
    }
    return extra;
}

bool is_read_op(const std::string& api, const std::string& op)
{
    if (api == "LLInventory")
    {
        return op == "status" || op == "get" || op == "list" || op == "search" ||
               op == "systemFolder" || op == "types" || op == "getMany" ||
               op == "resolvePath" || op == "protectedFolders" || op == "readNotecard" ||
               op == "readScript" || op == "landmark" || op == "changes" ||
               op == "getItemsInfo" || op == "getFolderTypeNames" ||
               op == "getAssetTypeNames" || op == "getBasicFolderID" ||
               op == "getDirectDescendants" || op == "collectDescendantsIf";
    }
    if (api == "LLAppearance")
    {
        return op == "getOutfitsList" || op == "getOutfitItems" || op == "worn";
    }
    return api == "LLCamera" && op == "get";
}

ActionClass classify(const std::string& api, const std::string& op)
{
    if (api == "LLInventory")
    {
        if (op == "purge" || op == "emptyTrash")
        {
            return ActionClass::Permanent;
        }
        if (is_read_op(api, op))
        {
            return ActionClass::Read;
        }
        if (op == "createFolder" || op == "createItem" || op == "link")
        {
            return ActionClass::Create;
        }
        if (op == "rename" || op == "batchRename" || op == "setDescription" ||
            op == "setThumbnail" || op == "setFavorite")
        {
            return ActionClass::Edit;
        }
        if (op == "move" || op == "batchMove" || op == "restore" || op == "copy" ||
            op == "batchCopy")
        {
            return ActionClass::Move;
        }
        if (op == "trash")
        {
            return ActionClass::Trash;
        }
        if (op == "confirmCopy")
        {
            return ActionClass::NoCopy;
        }
        if (op == "replaceLinks")
        {
            return ActionClass::Links;
        }
        return ActionClass::Unknown;
    }
    if (api == "LLAppearance")
    {
        if (is_read_op(api, op))
        {
            return ActionClass::Read;
        }
        if (op == "wearOutfit" || op == "wearItems" || op == "detachItems")
        {
            return ActionClass::Wear;
        }
        return ActionClass::Unknown;
    }
    if (api == "LLCamera")
    {
        if (op == "get")
        {
            return ActionClass::Read;
        }
        if (op == "set" || op == "setPose" || op == "reset" || op == "snapshot")
        {
            return ActionClass::Camera;
        }
    }
    return ActionClass::Unknown;
}

bool op_needs_confirm(const std::string& op)
{
    return op == "wearOutfit" || op == "wearItems" || op == "detachItems" ||
           op == "replaceLinks" || op == "confirmCopy";
}

F64 now_seconds()
{
    return static_cast<F64>(totalTime()) / 1000000.0;
}

std::string make_token()
{
    unsigned char bytes[TOKEN_BYTES];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
    {
        return {};
    }

    std::ostringstream token;
    token << std::hex << std::setfill('0');
    for (unsigned char byte : bytes)
    {
        token << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return token.str();
}

bool secure_equal(const std::string& lhs, const std::string& rhs)
{
    return lhs.size() == rhs.size() &&
           CRYPTO_memcmp(lhs.data(), rhs.data(), lhs.size()) == 0;
}

LLSD json_headers()
{
    LLSD headers;
    headers[HTTP_OUT_HEADER_CONTENT_TYPE] = HTTP_CONTENT_JSON;
    headers["Cache-Control"] = "no-store";
    headers["X-Content-Type-Options"] = "nosniff";
    return headers;
}

std::string to_json(const LLSD& value)
{
    return boost::json::serialize(LlsdToJson(value));
}

bool bridge_api_allowed(const std::string& api)
{
    return api == "LLInventory" || api == "LLAppearance" || api == "LLCamera";
}

void fail_response(LLHTTPNode::ResponsePtr response, S32 code, const std::string& error, const LLSD& extra = LLSD())
{
    LLSD body = extra.isMap() ? extra : LLSD::emptyMap();
    body["error"] = error;
    response->extendedResult(code, to_json(body), json_headers());
}
}

class FSEventAPIBridge::State : public std::enable_shared_from_this<State>
{
public:
    using ResponsePtr = LLHTTPNode::ResponsePtr;

    explicit State(std::string token)
        : mToken(std::move(token)),
          mReplyPump("FSEventAPIBridgeReply", true),
          mReplyConnection(mReplyPump.listen(
              LLEventPump::ANONYMOUS,
              [this](const LLSD& reply) { return onReply(reply); })),
          mEnabled(true)
    {
    }

    bool authorize(const LLSD& context) const
    {
        if (!mEnabled)
        {
            return false;
        }

        const LLSD& request = context[CONTEXT_REQUEST];
        const std::string remote = request[CONTEXT_REMOTE_HOST].asString();
        if (remote != "127.0.0.1")
        {
            return false;
        }

        const LLSD& headers = request[CONTEXT_HEADERS];
        if (headers.has("origin"))
        {
            return false;
        }

        const std::string authorization = headers["authorization"].asString();
        constexpr const char* prefix = "Bearer ";
        if (authorization.rfind(prefix, 0) != 0)
        {
            return false;
        }
        return secure_equal(authorization.substr(std::strlen(prefix)), mToken);
    }

    void dispatch(const LLSD& input, ResponsePtr response)
    {
        const std::string api = input["api"].asString();
        const std::string op = input["op"].asString();
        if (api.empty() || op.empty())
        {
            fail_response(response, HTTP_BAD_REQUEST, "Both api and op are required");
            return;
        }
        // Authentication, loopback and Origin checks already ran in Node.
        // Diagnostics expose no inventory and work when Read is Never.
        if (api == "LocalAssistant" && op == "health")
        {
            mLastDiagnosticAt = now_seconds();
            LLSD apis = LLSD::emptyMap();
            for (const char* name : {"LLInventory", "LLAppearance", "LLCamera"})
                apis[name] = LLEventAPI::getInstance(name) != nullptr;
            response->extendedResult(HTTP_OK,
                to_json(llsd::map("bridge_ready", mEnabled, "api_version", 1,
                    "policy_generation", gPolicyGeneration, "apis_ready", apis)), json_headers());
            return;
        }
        mLastExternalRequestAt = now_seconds();
        mLastExternalApi = api.substr(0, 64);
        mLastExternalOp = op.substr(0, 64);
        for (char& c : mLastExternalApi) if (static_cast<unsigned char>(c) < 32) c = ' ';
        for (char& c : mLastExternalOp) if (static_cast<unsigned char>(c) < 32) c = ' ';
        if (!bridge_api_allowed(api))
        {
            fail_response(
                response,
                HTTP_FORBIDDEN,
                "Event API is not exposed by the local MCP bridge",
                llsd::map("api", api));
            return;
        }
        // Hard-deny before any class lookup or settings read. confirm and a
        // hand-edited allow/ask value are ignored because this returns first.
        if (op == "purge" || op == "emptyTrash")
        {
            fail_response(response, HTTP_FORBIDDEN, ERR_PERMANENT, llsd::map("op", op));
            return;
        }

        if (api == "LLInventory" && op == "snapshotUpload")
        {
            dispatchSnapshotUpload(input, response);
            return;
        }

        const ActionClass kind = classify(api, op);
        if (kind == ActionClass::Unknown || kind == ActionClass::Permanent)
        {
            fail_response(response, HTTP_FORBIDDEN, ERR_UNKNOWN_OP, llsd::map("api", api, "op", op));
            return;
        }

        const std::string level = effective_level(kind);
        if (level == "deny")
        {
            LLSD extra = llsd::map("op", op, "class", find_def(kind)->id);
            if (api == "LLInventory" && op == "status")
            {
                extra["permissions"] = effective_permissions();
                extra["policy_generation"] = gPolicyGeneration;
            }
            fail_response(response, HTTP_FORBIDDEN, ERR_NOT_ALLOWED, extra);
            return;
        }
        if (level == "ask")
        {
            FSAssistantPreparedOperation prepared;
            std::string error;
            if (!prepareOperation(input, {find_def(kind)->id}, prepared, error))
            {
                fail_response(response, HTTP_BAD_REQUEST, error);
                return;
            }
            enqueueAsk(std::move(prepared), response, api, op);
            return;
        }
        if (op == "replaceLinks")
        {
            FSAssistantPreparedOperation prepared;
            std::string error;
            if (!prepareOperation(input, {find_def(kind)->id}, prepared, error))
            {
                fail_response(response, HTTP_BAD_REQUEST, error);
                return;
            }
            postEvent(prepared.request, response, api, op, true, &prepared);
            return;
        }
        postEvent(input, response, api, op, op_needs_confirm(op));
    }

    void disable()
    {
        mEnabled = false;
        ++mSessionGeneration;
        denyQueuedAsks(ERR_DENIED);
        for (auto& pending : mPending)
        {
            if (!pending.second.sent)
            {
                pending.second.sent = true;
                fail_response(pending.second.response, HTTP_SERVICE_UNAVAILABLE, "Viewer Event API bridge is shutting down");
            }
        }
        mPending.clear();
    }

    void enable() { mEnabled = true; ++mSessionGeneration; }
    bool enabled() const { return mEnabled; }
    void setToken(std::string token) { mToken = std::move(token); }
    const std::string& token() const { return mToken; }

    void permissionsChanged()
    {
        mExecutions.erase(std::remove_if(mExecutions.begin(), mExecutions.end(), [](const auto& weak)
        {
            auto gate = weak.lock();
            if (!gate) return true;
            gate->observe(effective_levels(gate->classes()));
            return false;
        }), mExecutions.end());
        std::vector<LLUUID> revoked;
        for (const auto& entry : mAsks)
            if (fs_assistant::decide(effective_levels(entry.second.prepared.requiredClasses)) == fs_assistant::Decision::Deny)
                revoked.push_back(entry.first);
        for (const LLUUID& key : revoked) resolveAsk(key, false, ERR_NOT_ALLOWED, false);
    }

    FSEventAPIBridge::StatusSnapshot snapshot() const
    {
        FSEventAPIBridge::StatusSnapshot result;
        result.enabled = mEnabled;
        result.ready = mEnabled;
        result.policyGeneration = gPolicyGeneration;
        result.pendingApprovals = static_cast<int>(mAsks.size());
        result.lastExternalRequestAt = mLastExternalRequestAt;
        result.lastDiagnosticAt = mLastDiagnosticAt;
        result.lastExternalApi = mLastExternalApi;
        result.lastExternalOp = mLastExternalOp;
        return result;
    }

private:
    struct Posted
    {
        ResponsePtr response;
        std::string api;
        std::string op;
        bool sent = false;
    };

    struct Ask
    {
        LLUUID key;
        ResponsePtr response;
        FSAssistantPreparedOperation prepared;
        std::string api;
        std::string op;
        bool needsConfirm = false;
        F64 enqueuedAt = 0.0;
        F64 chainDeadline = 0.0;
        F64 dialogDeadline = 0.0;
        bool dialogPosted = false;
        fs_assistant::ApprovalGate gate;
        LLHandle<LLFloater> dialog;
    };

    void dispatchSnapshotUpload(const LLSD& input, ResponsePtr response)
    {
        std::string raw_destination = input.has("destination") ? input["destination"].asString() : std::string();
        LLStringUtil::trim(raw_destination);
        LLStringUtil::toLower(raw_destination);
        std::string destination;
        if (!fs_snapshot::normalize_destination(raw_destination, destination))
        {
            fail_response(response, HTTP_BAD_REQUEST, "destination must be thumbnail or texture");
            return;
        }
        FSAssistantPreparedOperation prepared;
        std::string error;
        LLSD request(input);
        request["destination"] = destination;
        if (!prepareOperation(request, fs_assistant::snapshot_classes(destination), prepared, error))
        {
            fail_response(response, HTTP_BAD_REQUEST, error);
            return;
        }
        const S32 cost = prepared.summary["cost"].asInteger();
        const auto action = fs_assistant::decide(effective_levels(prepared.requiredClasses), cost);
        if (action == fs_assistant::Decision::Deny)
        {
            fail_response(
                response,
                HTTP_FORBIDDEN,
                ERR_NOT_ALLOWED,
                policy_error_details("snapshotUpload", prepared.requiredClasses));
            return;
        }
        if (action == fs_assistant::Decision::Ask)
        {
            enqueueAsk(std::move(prepared), response, "LLInventory", "snapshotUpload");
            return;
        }
        postEvent(prepared.request, response, "LLInventory", "snapshotUpload", false, &prepared);
    }

    std::shared_ptr<LLInventoryListener> inventoryListener() const
    {
        return std::dynamic_pointer_cast<LLInventoryListener>(LLEventAPI::getInstance("LLInventory"));
    }

    bool prepareOperation(const LLSD& input, const std::vector<std::string>& classes,
                          FSAssistantPreparedOperation& prepared, std::string& error)
    {
        prepared.request = input;
        prepared.requiredClasses = classes;
        const std::string op = input["op"].asString();
        if (op == "confirmCopy" || op == "replaceLinks")
        {
            auto listener = inventoryListener();
            if (!listener) { error = "Inventory API is not ready"; return false; }
            if (op == "confirmCopy")
            {
                if (!listener->prepareAssistantCopyConfirmation(input, prepared.summary, prepared.validation, error)) return false;
                prepared.summary["consequences"] = LLSD::emptyArray();
                prepared.summary["consequences"].append("no_copy");
            }
            else
            {
                auto selection = std::make_shared<FSLinkReplacementSelection>();
                if (!listener->prepareLinkReplacement(input, *selection, error)) return false;
                prepared.linkSelection = selection;
                LLSD& summary = prepared.summary;
                summary = llsd::map("subject_count", static_cast<S32>(selection->links.size()),
                    "source", selection->source_path.empty() ? selection->source_id : selection->source_path,
                    "target", selection->target_path.empty() ? selection->resolved_target_id : selection->target_path,
                    "subjects", LLSD::emptyArray(), "consequences", LLSD::emptyArray());
                for (const FSLinkReplacementCandidate& link : selection->links)
                {
                    LLSD row = llsd::map("id", link.id, "name", link.name, "path", link.path,
                        "source", link.parent_path, "destination", link.parent_path);
                    if (!link.skip_error.empty()) row["skip_error"] = link.skip_error;
                    summary["subjects"].append(row);
                }
                summary["consequences"].append("replace_links");
            }
            prepared.summary["op"] = op;
            return true;
        }
        if (!fs_prepare_assistant_operation(prepared, error)) return false;
        if (op == "snapshotUpload" || op == "snapshot")
        {
            FSSnapshotDefaults defaults;
            if (op == "snapshotUpload")
            {
                defaults.viewport_only = true;
                defaults.square = true;
                defaults.square_edge = fs_snapshot::kDefaultEdge;
            }
            FSSnapshotFrame frame;
            if (!fs_parse_snapshot_frame(input, defaults, frame, error)) return false;
            // Uploads already default to an explicit square. Preserve native
            // camera snapshot's implicit-size path and its sub-64 thin edges.
            if (op == "snapshotUpload" || frame.explicit_size)
            {
                prepared.request["width"] = frame.width;
                prepared.request["height"] = frame.height;
            }
            prepared.request["viewport_only"] = frame.viewport_only;
            prepared.request["show_ui"] = frame.show_ui;
            prepared.request["show_hud"] = frame.show_hud;
            prepared.summary["width"] = frame.width;
            prepared.summary["height"] = frame.height;
            prepared.summary["viewport_only"] = frame.viewport_only;
            if (op == "snapshotUpload")
            {
                const std::string destination = input["destination"].asString();
                prepared.summary["cost"] = fs_snapshot::quoted_cost(destination,
                    destination == "texture" ? LLAgentBenefitsMgr::current().getTextureUploadCost(frame.width, frame.height) : 0);
                prepared.summary["consequences"].append(destination == "texture" ? "texture" : "thumbnail");
            }
        }
        return true;
    }

    bool validateOperation(const FSAssistantPreparedOperation& prepared, std::string& error)
    {
        const std::string op = prepared.request["op"].asString();
        if (op == "confirmCopy" || op == "replaceLinks")
        {
            auto listener = inventoryListener();
            if (!listener) { error = "Inventory API is not ready"; return false; }
            return op == "confirmCopy"
                ? listener->validateAssistantCopyConfirmation(prepared.request, prepared.validation, error)
                : prepared.linkSelection && listener->validateLinkReplacementSelection(*prepared.linkSelection, error);
        }
        if (!fs_validate_assistant_operation(prepared, error)) return false;
        if (op == "snapshot")
        {
            FSSnapshotFrame current;
            if (!fs_parse_snapshot_frame(prepared.request, FSSnapshotDefaults(), current, error)) return false;
            if (current.width != prepared.summary["width"].asInteger() || current.height != prepared.summary["height"].asInteger())
            {
                error = "The capture frame changed. Request a fresh confirmation.";
                return false;
            }
        }
        if (op == "snapshotUpload" && prepared.request["destination"].asString() == "texture" &&
            LLAgentBenefitsMgr::current().getTextureUploadCost(prepared.request["width"].asInteger(),
                prepared.request["height"].asInteger()) > prepared.summary["cost"].asInteger())
        {
            error = "The upload price increased. Request a fresh confirmation.";
            return false;
        }
        return true;
    }

    void postEvent(LLSD request, ResponsePtr response, const std::string& api, const std::string& op,
                   bool needsConfirm, const FSAssistantPreparedOperation* prepared = nullptr, F64 deadline = 0.0)
    {
        if (needsConfirm)
        {
            request["confirm"] = true;
        }
        const LLUUID request_id = LLUUID::generateNewID();
        request["reqid"] = request_id;
        request["reply"] = mReplyPump.getName();
        Posted posted;
        posted.response = response;
        posted.api = api;
        posted.op = op;
        mPending.emplace(request_id, posted);

        if (!LLEventAPI::getInstance(api))
        {
            auto found = mPending.find(request_id);
            if (found != mPending.end() && !found->second.sent)
            {
                found->second.sent = true;
                fail_response(found->second.response, HTTP_NOT_FOUND, "Event API was not found or did not accept the request");
                mPending.erase(found);
            }
            return;
        }
        if (prepared && (op == "replaceLinks" || op == "snapshotUpload"))
        {
            const auto classes = prepared->requiredClasses;
            const auto generation = mSessionGeneration;
            auto gate = std::make_shared<fs_assistant::ExecutionGate>(classes);
            mExecutions.erase(std::remove_if(mExecutions.begin(), mExecutions.end(),
                [](const auto& weak) { return weak.expired(); }), mExecutions.end());
            mExecutions.emplace_back(gate);
            std::weak_ptr<State> weak = weak_from_this();
            FSAssistantExecutionContext context;
            context.deadline = deadline > 0.0 ? deadline : now_seconds() + CHAIN_GUARD_SECONDS;
            context.allowed = [weak, classes, generation, gate]()
            {
                auto self = weak.lock();
                return self && gate->allowed() && self->mEnabled && self->mSessionGeneration == generation &&
                    fs_assistant::decide(effective_levels(classes)) != fs_assistant::Decision::Deny;
            };
            auto listener = inventoryListener();
            if (!listener)
            {
                fail_response(response, HTTP_NOT_FOUND, "Inventory API is not ready");
                mPending.erase(request_id);
                return;
            }
            if (op == "replaceLinks") listener->dispatchPreparedLinkReplacement(request, prepared->linkSelection, context);
            else
            {
                context.approvedSnapshotCost = prepared->summary["cost"].asInteger();
                listener->dispatchPreparedSnapshotUpload(request, context);
            }
            return;
        }
        LLEventPumps::instance().obtain(api).post(request);
    }

    void enqueueAsk(FSAssistantPreparedOperation prepared, ResponsePtr response, const std::string& api, const std::string& op)
    {
        Ask ask;
        ask.key = LLUUID::generateNewID();
        ask.response = response;
        ask.prepared = std::move(prepared);
        ask.api = api;
        ask.op = op;
        ask.needsConfirm = op_needs_confirm(op);
        ask.enqueuedAt = now_seconds();
        ask.chainDeadline = ask.enqueuedAt + CHAIN_GUARD_SECONDS;
        mAskOrder.push_back(ask.key);
        mAsks.emplace(ask.key, std::move(ask));
        ensureTicker();
        scheduleShow();
    }

    void scheduleShow()
    {
        if (mShowScheduled)
        {
            return;
        }
        mShowScheduled = true;
        std::weak_ptr<State> weak = weak_from_this();
        doOnIdleOneTime([weak]()
        {
            if (std::shared_ptr<State> self = weak.lock())
            {
                self->mShowScheduled = false;
                self->showFront();
            }
        });
    }

    void ensureTicker()
    {
        if (mTicker)
        {
            return;
        }
        mTicker = true;
        std::weak_ptr<State> weak = weak_from_this();
        doPeriodically([weak]()
        {
            std::shared_ptr<State> self = weak.lock();
            if (!self)
            {
                return true;
            }
            self->tickAsks();
            if (self->mAsks.empty())
            {
                self->mTicker = false;
                return true;
            }
            return false;
        }, 0.5f);
    }

    void tickAsks()
    {
        const F64 now = now_seconds();
        std::vector<LLUUID> expired;
        for (const LLUUID& key : mAskOrder)
        {
            auto found = mAsks.find(key);
            if (found == mAsks.end() || found->second.gate.finished())
            {
                continue;
            }
            const F64 deadline = found->second.dialogPosted ? found->second.dialogDeadline
                                                            : found->second.chainDeadline;
            if (now >= deadline)
            {
                expired.push_back(key);
            }
        }
        for (const LLUUID& key : expired)
        {
            resolveAsk(key, false, ERR_TIMED_OUT, false);
        }
        showFront();
    }

    void showFront()
    {
        if (!mEnabled || mAskOrder.empty())
        {
            return;
        }
        auto found = mAsks.find(mAskOrder.front());
        if (found == mAsks.end() || found->second.gate.finished() || found->second.dialogPosted)
        {
            return;
        }
        Ask& ask = found->second;
        if (fs_assistant::decide(effective_levels(ask.prepared.requiredClasses)) == fs_assistant::Decision::Deny)
        {
            resolveAsk(ask.key, false, ERR_NOT_ALLOWED, false);
            return;
        }
        std::string validation_error;
        if (!validateOperation(ask.prepared, validation_error))
        {
            resolveAsk(ask.key, false, validation_error, false);
            return;
        }
        const F64 now = now_seconds();
        if (now + 1.0 >= ask.chainDeadline)
        {
            resolveAsk(ask.key, false, ERR_TIMED_OUT, false);
            showFront();
            return;
        }
        ask.dialogPosted = true;
        ask.dialogDeadline = std::min(now + ASK_WAIT_SECONDS, ask.chainDeadline);
        const LLUUID key = ask.key;
        std::weak_ptr<State> weak = weak_from_this();
        auto dialog = LLFloaterReg::getTypedInstance<FSAssistantApproval>("local_assistant_approval");
        if (!dialog)
        {
            resolveAsk(ask.key, false, "The local assistant approval window is unavailable", false);
            return;
        }
        ask.dialog = dialog->getHandle();
        dialog->present(ask.prepared.summary,
            [weak, key](bool allow)
            {
                std::shared_ptr<State> self = weak.lock();
                if (!self)
                {
                    return;
                }
                self->resolveAsk(key, allow, allow ? std::string() : std::string(ERR_DENIED), true);
            });
    }

    void denyQueuedAsks(const std::string& error)
    {
        std::vector<LLUUID> keys(mAskOrder.begin(), mAskOrder.end());
        for (const LLUUID& key : keys)
        {
            resolveAsk(key, false, error, false);
        }
    }

    void resolveAsk(const LLUUID& key, bool allow, const std::string& error, bool fromDialog)
    {
        auto found = mAsks.find(key);
        if (found == mAsks.end() || found->second.gate.finished())
        {
            return;
        }
        const bool expired = now_seconds() >= (found->second.dialogPosted ? found->second.dialogDeadline : found->second.chainDeadline);
        const auto outcome = found->second.gate.finish(allow,
            effective_levels(found->second.prepared.requiredClasses), mEnabled, expired);
        Ask ask = found->second;
        // Remove the terminal request before dismissing any UI: close callbacks
        // and old Yes callbacks cannot find or resurrect it.
        mAsks.erase(found);
        mAskOrder.erase(std::remove(mAskOrder.begin(), mAskOrder.end(), key), mAskOrder.end());
        if (!fromDialog)
            if (auto dialog = dynamic_cast<FSAssistantApproval*>(ask.dialog.get())) dialog->dismiss();

        if (outcome != fs_assistant::ApprovalGate::Outcome::Allow)
        {
            // Send the denial ourselves while the chain is still inside its 75s budget.
            // extendedResult no-ops if that chain has already dropped the pipe.
            const std::string reason = outcome == fs_assistant::ApprovalGate::Outcome::TimedOut ? ERR_TIMED_OUT :
                outcome == fs_assistant::ApprovalGate::Outcome::Revoked ? ERR_NOT_ALLOWED : (error.empty() ? ERR_DENIED : error);
            fail_response(ask.response, HTTP_FORBIDDEN, reason, policy_error_details(ask.op, ask.prepared.requiredClasses));
            scheduleShow();
            return;
        }
        std::string validation_error;
        if (!validateOperation(ask.prepared, validation_error))
        {
            fail_response(ask.response, HTTP_FORBIDDEN, validation_error);
            scheduleShow();
            return;
        }
        postEvent(ask.prepared.request, ask.response, ask.api, ask.op, ask.needsConfirm, &ask.prepared, ask.chainDeadline);
        scheduleShow();
    }

    bool onReply(const LLSD& reply)
    {
        const LLUUID request_id = reply["reqid"].asUUID();
        auto found = mPending.find(request_id);
        if (found == mPending.end() || found->second.sent)
        {
            return false;
        }
        found->second.sent = true;
        LLSD body = reply;
        if (found->second.api == "LLInventory" && found->second.op == "status" && body.isMap())
        {
            body["permissions"] = effective_permissions();
            body["policy_generation"] = gPolicyGeneration;
        }
        found->second.response->extendedResult(HTTP_OK, to_json(body), json_headers());
        mPending.erase(found);
        return true;
    }

    std::string mToken;
    LLEventStream mReplyPump;
    LLTempBoundListener mReplyConnection;
    std::map<LLUUID, Posted> mPending;
    std::map<LLUUID, Ask> mAsks;
    std::deque<LLUUID> mAskOrder;
    std::vector<std::weak_ptr<fs_assistant::ExecutionGate>> mExecutions;
    U64 mSessionGeneration = 1;
    F64 mLastExternalRequestAt = 0.0;
    F64 mLastDiagnosticAt = 0.0;
    std::string mLastExternalApi;
    std::string mLastExternalOp;
    bool mEnabled = false;
    bool mTicker = false;
    bool mShowScheduled = false;
};

class FSEventAPIBridge::Node final : public LLHTTPNode
{
public:
    explicit Node(std::shared_ptr<State> state)
        : mState(std::move(state))
    {
    }

    EHTTPNodeContentType getContentType() const override
    {
        return CONTENT_TYPE_TEXT;
    }

    void post(ResponsePtr response, const LLSD& context, const LLSD& input) const override
    {
        if (!mState->authorize(context))
        {
            fail_response(response, HTTP_UNAUTHORIZED, "Unauthorized");
            return;
        }

        const std::string body = input.asString();
        if (body.empty() || body.size() > MAX_REQUEST_BYTES)
        {
            fail_response(response, HTTP_BAD_REQUEST, "Invalid request size");
            return;
        }

        try
        {
            LLSD request = LlsdFromJson(boost::json::parse(body));
            if (!request.isMap())
            {
                throw std::runtime_error("request must be a JSON object");
            }
            mState->dispatch(request, response);
        }
        catch (const std::exception& error)
        {
            fail_response(response, HTTP_BAD_REQUEST, error.what());
        }
    }

    void options(ResponsePtr response, const LLSD&) const override
    {
        response->status(HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

private:
    std::shared_ptr<State> mState;
};

void FSEventAPIBridge::notePermissionClassesChanged()
{
    ++gPolicyGeneration;
    if (instanceExists() && instance().mState) instance().mState->permissionsChanged();
}

int FSEventAPIBridge::getPolicyGeneration()
{
    return gPolicyGeneration;
}

FSEventAPIBridge::FSEventAPIBridge() = default;

FSEventAPIBridge::~FSEventAPIBridge()
{
    stop();
}

void FSEventAPIBridge::setPump(LLPumpIO& pump)
{
    mPump = &pump;
}

bool FSEventAPIBridge::start(LLPumpIO& pump)
{
    setPump(pump);
    return applyEnabled(true);
}

bool FSEventAPIBridge::applyEnabled(bool enabled)
{
    if (!mPump)
    {
        return false;
    }
    if (enabled)
    {
        if (!mState)
        {
            return bindOnce();
        }
        if (mState->enabled())
        {
            return true;
        }
        std::string token = make_token();
        if (token.empty())
        {
            mLastError = "Unable to generate the local assistant access token";
            LL_WARNS("EventAPIBridge") << "Unable to generate authentication token" << LL_ENDL;
            return false;
        }
        mState->setToken(std::move(token));
        mState->enable();
        if (!writeDiscovery())
        {
            mLastError = "Unable to write the local assistant connection file";
            mState->disable();
            return false;
        }
        mLastError.clear();
        LL_INFOS("EventAPIBridge") << "Local Event API bridge listening on 127.0.0.1:"
                                    << mPort << LL_ENDL;
        return true;
    }

    if (mState && mState->enabled())
    {
        mState->disable();
    }
    removeDiscoveryFile();
    return true;
}

bool FSEventAPIBridge::bindOnce()
{
    std::string token = make_token();
    if (token.empty())
    {
        mLastError = "Unable to generate the local assistant access token";
        LL_WARNS("EventAPIBridge") << "Unable to generate authentication token" << LL_ENDL;
        return false;
    }

    auto state = std::make_shared<State>(std::move(token));
    U16 port = 0;
    LLHTTPNode& root = LLIOHTTPServer::create(
        gAPRPoolp, *mPump, LLSocket::PORT_EPHEMERAL, "127.0.0.1", port, 75.f);
    root.addNode(BRIDGE_PATH, new Node(state));
    mPort = port;

    const std::string filename =
        "fs-mcp-" + std::to_string(LLApp::getPid()) + ".json";
    mDiscoveryPath = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, filename);
    mState = std::move(state);
    if (!writeDiscovery())
    {
        mLastError = "Unable to write the local assistant connection file";
        mState->disable();
        return false;
    }
    mLastError.clear();
    LL_INFOS("EventAPIBridge") << "Local Event API bridge listening on 127.0.0.1:"
                                << mPort << LL_ENDL;
    return true;
}

bool FSEventAPIBridge::writeDiscovery()
{
    if (!mState || mDiscoveryPath.empty())
    {
        return false;
    }
    const std::string temporary = mDiscoveryPath + ".tmp";
    LLSD discovery = llsd::map(
        "api_version", 1,
        "apiVersion", API_VERSION,
        "pid", LLApp::getPid(),
        "port", mPort,
        "token", mState->token(),
        "host", "127.0.0.1",
        "scheme", "http",
        "path", BRIDGE_PATH);
    const std::string payload = to_json(discovery);

    LLFILE* file = LLFile::fopen(temporary, "wb");
    const bool write_ok =
        file && std::fwrite(payload.data(), 1, payload.size(), file) == payload.size();
    const bool close_ok = !file || LLFile::close(file) == 0;
    if (!write_ok || !close_ok)
    {
        LLFile::remove(temporary, ENOENT);
        LL_WARNS("EventAPIBridge") << "Unable to write discovery file" << LL_ENDL;
        return false;
    }
#if !LL_WINDOWS
    chmod(temporary.c_str(), S_IRUSR | S_IWUSR);
#endif
    if (LLFile::rename(temporary, mDiscoveryPath) != 0)
    {
        LLFile::remove(temporary, ENOENT);
        return false;
    }
    return true;
}

void FSEventAPIBridge::removeDiscoveryFile()
{
    if (!mDiscoveryPath.empty())
    {
        LLFile::remove(mDiscoveryPath, ENOENT);
    }
}

void FSEventAPIBridge::stop()
{
    if (mState)
    {
        mState->disable();
        mState.reset();
    }
    removeDiscoveryFile();
    mDiscoveryPath.clear();
    mPump = nullptr;
}

bool FSEventAPIBridge::isRunning() const
{
    return mState && mState->enabled();
}

int FSEventAPIBridge::getPort() const
{
    return isRunning() ? mPort : 0;
}

const std::string& FSEventAPIBridge::getDiscoveryPath() const
{
    return mDiscoveryPath;
}

FSEventAPIBridge::StatusSnapshot FSEventAPIBridge::getStatusSnapshot() const
{
    StatusSnapshot result = mState ? mState->snapshot() : StatusSnapshot();
    result.enabled = gSavedSettings.getBOOL("EnableLocalEventAPIBridge");
    result.ready = isRunning();
    result.port = getPort();
    result.policyGeneration = gPolicyGeneration;
    result.error = mLastError;
    return result;
}
