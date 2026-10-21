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
#include "fssessionworker.h"
#include "fsassistantpermissions.h"
#include "fsassistantapproval.h"
#include "fsassistantoperation.h"
#include "fsassistantfeature.h"
#include "fsassistantsocial.h"
#include "fsassistantfeatures.h"
#include "fsassistantpolicy.h"
#include "fslinkreplacement.h"
#include "fsinventorybulk.h"
#include "fsworkspacelistener.h"
#include "fsworldlistener.h"
#include "fsbuildlistener.h"
#include "fsnavigationlistener.h"
#include "fscontentlistener.h"
#include "fsgesturelistener.h"
#include "fspreferenceslistener.h"
#include "fsposelistener.h"

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
#include "fsassistantsession.h"
#include "fsassistantdiscovery.h"
#include "llsdutil.h"
#include <filesystem>
#include <cstdlib>
#include "llagent.h"
#include "llagentui.h"
#include "llstartup.h"
#include "llviewernetwork.h"
#include "llappviewer.h"
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
    if(const auto* feature=fs_assistant::featureOperation(api,op))return feature->readOnly;
    if (api == "LLInventory")
    {
        return op == "status" || op == "get" || op == "list" || op == "search" ||
               op == "systemFolder" || op == "types" || op == "getMany" ||
               op == "resolvePath" || op == "protectedFolders" || op == "readNotecard" ||
               op == "readScript" || op == "landmark" || op == "changes" ||
               op == "getItemsInfo" || op == "getFolderTypeNames" ||
               op == "getAssetTypeNames" || op == "getBasicFolderID" ||
               op == "getDirectDescendants" || op == "collectDescendantsIf" ||
               op == "previewBatchRename" || op == "previewBatchMove" ||
               op == "previewBulkUndo" || op == "bulkHistory";
    }
    if (api == "LLAppearance")
    {
        return op == "getOutfitsList" || op == "getOutfitItems" || op == "worn";
    }
    if (api == "LLWorkspace") return op == "list" || op == "status" || op == "preview";
    return api == "LLCamera" && op == "get";
}

ActionClass classify(const std::string& api, const std::string& op)
{
    if (api == "LLWorkspace")
        return is_read_op(api, op) ? ActionClass::Read : (op == "apply" || op == "previous") ? ActionClass::Workspace : ActionClass::Unknown;
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
    return api == "LLInventory" || api == "LLAppearance" || api == "LLCamera" || api == "LLWorkspace" || fs_assistant::featureAPI(api);
}

std::shared_ptr<FSAssistantFeature> feature_listener(const std::string& api)
{return std::dynamic_pointer_cast<FSAssistantFeature>(LLEventAPI::getInstance(api));}

LLSD assistant_capabilities()
{
    LLSD result=llsd::map("bulk_inventory_review",1,"workspace_tools",1,"managed_sessions",1,"async_operations",1);
    for(const auto& entry:fs_assistant::featureOperations)
        if(const auto feature=feature_listener(entry.api))
        {
            const auto advertised=feature->assistantCapabilities();
            if(advertised.has(entry.capability) && advertised[entry.capability].asInteger()==1)
                result[entry.capability]=1;
        }
    return result;
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
        fs_initialize_workspace_api();
        fs_initialize_world_api();
        fs_initialize_build_api();
        fs_initialize_navigation_api();
        fs_initialize_content_api();
        fs_initialize_gesture_api();
        fs_initialize_preferences_api();
        fs_initialize_pose_api();
        mSocial=std::make_shared<FSAssistantSocial>();
        refreshSocialPrivacy();
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
        FSEventAPIBridge::instance().refreshSession();
        const auto expected=input["viewer_session"].asString();
        if(!mEnabled || !fs_assistant::sessionMatches(mIdentity["managed"].asBoolean(),mIdentity["viewer_session"].asString(),expected,input.has("viewer_session")))
        { fail_response(response,HTTP_FORBIDDEN,"session_changed",llsd::map("error_code","session_changed"));return; }
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
            for (const char* name : {"LLInventory", "LLAppearance", "LLCamera", "LLWorkspace"})
                apis[name] = LLEventAPI::getInstance(name) != nullptr;
            LLSD health=mIdentity;
            health["bridge_ready"]=mEnabled;health["api_version"]=1;health["policy_generation"]=gPolicyGeneration;
            health["managed_async_appearance_writes"]=true;health["managed_create_item"]=true;
            health["apis_ready"]=apis;health["capabilities"]=assistant_capabilities();
            LLSD limits=LLSD::emptyMap();
            for(const auto& entry:fs_assistant::featureOperations)
                if(const auto feature=feature_listener(entry.api))
                {apis[entry.api]=true;limits[entry.api]=feature->assistantLimits();}
            health["apis_ready"]=apis;health["feature_limits"]=limits;
            response->extendedResult(HTTP_OK,to_json(health),json_headers());

            return;
        }
        if(api=="LocalAssistant" && op=="operationStatus")
        {
            auto result=asyncStatus(input["operation_id"].asUUID());
            if(result.isUndefined())fail_response(response,HTTP_NOT_FOUND,"Operation belongs to an expired connection or is unavailable");
            else {result["viewer_session"]=mIdentity["viewer_session"];response->extendedResult(HTTP_OK,to_json(result),json_headers());}
            return;
        }
        if(mIdentity["managed"].asBoolean())
        {
            if(!mIdentity["ready"].asBoolean())
            {fail_response(response,HTTP_FORBIDDEN,"Character is not ready",llsd::map("error_code","not_ready"));return;}
            if(fs_assistant::needsActiveCharacter(api,op) && !FSSessionWorker::assistantActiveAllowed())
            {fail_response(response,HTTP_FORBIDDEN,"Activate this character and finish its transition/dialog first",llsd::map("error_code","requires_active_character"));return;}
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

        if (api == "LLInventory" && op == "executeBulkPlan")
        {
            std::string error;
            FSAssistantPreparedOperation prepared;
            if (!prepareOperation(input, {}, prepared, error)) { fail_response(response, HTTP_BAD_REQUEST, error); return; }
            const auto decision = fs_assistant::decide(effective_levels(prepared.requiredClasses));
            if (decision == fs_assistant::Decision::Deny) { fail_response(response, HTTP_FORBIDDEN, ERR_NOT_ALLOWED, policy_error_details(op, prepared.requiredClasses)); return; }
            if (decision == fs_assistant::Decision::Ask && !FSInventoryBulk::plan(prepared.validation["bulk_plan_id"].asString(), error).has("operation_id")) enqueueAsk(std::move(prepared), response, api, op);
            else postEvent(input, response, api, op, false, &prepared);
            return;
        }
        if (api == "LLWorkspace" && (op == "apply" || op == "previous"))
        {
            FSAssistantPreparedOperation prepared; std::string error;
            if (!prepareOperation(input, {}, prepared, error)) { fail_response(response, HTTP_BAD_REQUEST, error); return; }
            const auto decision = fs_assistant::decide(effective_levels(prepared.requiredClasses));
            if (decision == fs_assistant::Decision::Deny) fail_response(response, HTTP_FORBIDDEN, ERR_NOT_ALLOWED, policy_error_details(op, prepared.requiredClasses));
            else if (decision == fs_assistant::Decision::Ask) enqueueAsk(std::move(prepared), response, api, op);
            else postEvent(prepared.request, response, api, op, false, &prepared);
            return;
        }
        if(fs_assistant::featureAPI(api))
        {
            const auto* entry=fs_assistant::featureOperation(api,op);
            const auto feature=feature_listener(api);
            if(!entry || !feature || feature->assistantCapabilities()[entry->capability].asInteger()!=1)
            {fail_response(response,HTTP_NOT_FOUND,"This exact assistant feature is unsupported",llsd::map("error_code","unsupported"));return;}
            if(*entry->permission)
            {
                const auto* def=find_def(entry->permission);
                if(!def || effective_level(def->kind)=="deny")
                {fail_response(response,HTTP_FORBIDDEN,ERR_NOT_ALLOWED,llsd::map("class",entry->permission));return;}
            }
            FSAssistantPreparedOperation prepared;std::string error;
            std::vector<std::string> classes;
            if(*entry->permission)classes.emplace_back(entry->permission);
            if(!prepareOperation(input,classes,prepared,error))
            {fail_response(response,HTTP_BAD_REQUEST,error);return;}
            if(prepared.requiredClasses.empty())
            {fail_response(response,HTTP_FORBIDDEN,"Feature did not declare a permission scope");return;}
            const auto decision=fs_assistant::decide(effective_levels(prepared.requiredClasses));
            if(decision==fs_assistant::Decision::Deny)
            {fail_response(response,HTTP_FORBIDDEN,ERR_NOT_ALLOWED,policy_error_details(op,prepared.requiredClasses));return;}
            if(decision==fs_assistant::Decision::Ask)enqueueAsk(std::move(prepared),response,api,op);
            else postEvent(prepared.request,response,api,op,false,&prepared);
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
                extra["capabilities"] = assistant_capabilities();
            }
            fail_response(response, HTTP_FORBIDDEN, ERR_NOT_ALLOWED, extra);
            return;
        }
        if((api=="LLInventory" && op=="createItem") || (api=="LLAppearance" && !is_read_op(api,op)))
        {
            FSAssistantPreparedOperation prepared;std::string error;
            if(!prepareOperation(input,{find_def(kind)->id},prepared,error)){fail_response(response,HTTP_BAD_REQUEST,error);return;}
            const auto decision=fs_assistant::decide(effective_levels(prepared.requiredClasses));
            if(decision==fs_assistant::Decision::Deny){fail_response(response,HTTP_FORBIDDEN,ERR_NOT_ALLOWED,policy_error_details(op,prepared.requiredClasses));return;}
            if(decision==fs_assistant::Decision::Ask)enqueueAsk(std::move(prepared),response,api,op);
            else postEvent(prepared.request,response,api,op,op_needs_confirm(op),&prepared);
            return;
        }
        if (api == "LLInventory" && (op == "previewBatchRename" || op == "previewBatchMove" || op == "previewBulkUndo" || op == "bulkHistory"))
        {
            std::string error;
            LLSD scoped=input;scoped["__assistant_epoch"]=mIdentity["viewer_session"];
            LLSD result = op == "bulkHistory" ? FSInventoryBulk::history(scoped, error) : FSInventoryBulk::preview(scoped, error);
            result["viewer_session"]=mIdentity["viewer_session"];
            if (!error.empty()) fail_response(response, HTTP_BAD_REQUEST, error);
            else response->extendedResult(HTTP_OK, to_json(result), json_headers());
            return;
        }
        if (api == "LLInventory" && (op == "rename" || op == "move" || op == "batchRename" || op == "batchMove"))
        {
            FSAssistantPreparedOperation prepared;
            std::string error;
            if (!prepareOperation(input, {find_def(kind)->id}, prepared, error)) { fail_response(response, HTTP_BAD_REQUEST, error); return; }
            const bool review = gSavedSettings.controlExists("FSAssistantReviewBulkOperations") && gSavedSettings.getBOOL("FSAssistantReviewBulkOperations") && prepared.summary["subject_count"].asInteger() >= 10;
            if (level == "ask" || review) enqueueAsk(std::move(prepared), response, api, op);
            else postEvent(input, response, api, op, false, &prepared);
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
        if(mSocial)mSocial->privacyChanged();
        FSInventoryBulk::retireAssistant();
        ++mSessionGeneration;
        denyQueuedAsks(ERR_DENIED);
        for (auto& pending : mPending)
        {
            if (!pending.second.sent)
            {
                pending.second.sent = true;
                fail_response(pending.second.response, HTTP_SERVICE_UNAVAILABLE, "Viewer Event API bridge is shutting down; submitted work may still complete",asyncStatus(pending.first));
            }
        }
        mPending.clear();
        for(auto& entry:mAsync)entry.second->cancel("connection_changed");
        mAsync.clear();mAsyncReadScopes.clear();
    }

    void enable() { if(mSocial)mSocial->privacyChanged();mEnabled = true; ++mSessionGeneration; }
    bool enabled() const { return mEnabled; }
    void identity(const LLSD& value)
    {
        if(mSocial && mIdentity["viewer_session"]!=value["viewer_session"])mSocial->privacyChanged();
        mIdentity=value;
    }
    void nearbyMessage(const LLChat& message){if(mEnabled && mSocial)mSocial->nearbyMessage(message);}
    void refreshSocialPrivacy()
    {
        LLSD next=LLSD::emptyMap();
        for(const char* id:{"chat_read","friends_read","resident_search","profile_read","notifications_read"})
            if(const auto* def=find_def(id))next[id]=effective_level(def->kind);
        if(!llsd_equals(next,mSocialReadPolicy))
        {mSocialReadPolicy=next;if(mSocial)mSocial->privacyChanged();}
    }
    const LLSD& identity() const { return mIdentity; }
    bool requestAllowed(const LLSD& request) const
    {
        if(!request.has("__assistant_guard"))return true;
        const auto found=mPending.find(request["__assistant_guard"].asUUID());
        return found!=mPending.end() && postedAllowed(found->second);
    }
    fs_assistant::AsyncGuard asyncContext(const LLSD& request) const
    {
        const auto found=mPending.find(request["__assistant_guard"].asUUID());
        return found==mPending.end()?fs_assistant::AsyncGuard():found->second.async;
    }
    LLSD asyncStatus(const LLUUID& id)
    {
        const auto found=mAsync.find(id);if(found==mAsync.end())return LLSD();
        auto& operation=*found->second;
        LLSD result=llsd::map("operation_id",id,"state",operation.state(),"submission_steps",static_cast<S32>(operation.submitted()),
            "pending_callbacks",static_cast<S32>(operation.pending()),"stop_reason",operation.reason(),"known_ids",LLSD::emptyArray());
        const auto scope=mAsyncReadScopes.find(id);
        const bool reveal=scope==mAsyncReadScopes.end() || fs_assistant::receiptVisible(scope->second,
            [](const std::string& permission){const auto* def=find_def(permission);return def?effective_level(def->kind):std::string("deny");});
        if(reveal)for(const auto& value:operation.ids())result["known_ids"].append(value);
        else result["results_restricted"]=true;
        return result;
    }
    void maintenance()
    {
        if(mSocial)mSocial->tick();
        for(auto it=mPending.begin();it!=mPending.end();)
        {
            if(!postedAllowed(it->second))
            {auto receipt=asyncStatus(it->first);receipt["error_code"]="session_changed";fail_response(it->second.response,HTTP_FORBIDDEN,"Assistant request expired or its session/permission changed",receipt);it=mPending.erase(it);}
            else ++it;
        }
    }
    void review() {showFront();}
    void setToken(std::string token) { mToken = std::move(token); }
    const std::string& token() const { return mToken; }

    void permissionsChanged()
    {
        refreshSocialPrivacy();
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

    void sharedPosePrivacyChanged()
    {
        std::vector<LLUUID> revoked;
        for(const auto& entry:mAsks)
            if(entry.second.api=="FSPose")
                if(const auto* op=fs_assistant::featureOperation(entry.second.api,entry.second.op))
                    if(std::string(op->capability)=="shared_pose_tools")revoked.push_back(entry.first);
        for(const auto& key:revoked)resolveAsk(key,false,"Native shared-pose privacy changed",false);
    }

    FSEventAPIBridge::StatusSnapshot snapshot() const
    {
        FSEventAPIBridge::StatusSnapshot result;
        result.enabled = mEnabled;
        result.ready = mEnabled;
        result.policyGeneration = gPolicyGeneration;
        result.pendingApprovals = static_cast<int>(mAsks.size());
        result.approvalRevision=mApprovalRevision;
        if(!mAskOrder.empty()){const auto found=mAsks.find(mAskOrder.front());if(found!=mAsks.end())result.pendingApprovalSummary=(found->second.api+" / "+found->second.op).substr(0,100);}
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
        LLUUID login,account;
        F64 deadline=0;
        U64 generation=0;
        std::shared_ptr<fs_assistant::ExecutionGate> gate;
        fs_assistant::AsyncGuard async;
    };
    bool postedAllowed(const Posted& posted) const
    {
        return (!posted.async || posted.async->allowed()) && mEnabled && !posted.sent && !gDisconnected && posted.generation==mSessionGeneration && posted.login==gAgent.getSessionID() &&
            posted.account==gAgentID && now_seconds()<posted.deadline && posted.gate && posted.gate->allowed() &&
            (!mIdentity["managed"].asBoolean() || !fs_assistant::needsActiveCharacter(posted.api,posted.op) || FSSessionWorker::assistantActiveAllowed(posted.api=="FSNavigation" && posted.op=="teleport"));
    }

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
        prepared.request["__assistant_epoch"]=mIdentity["viewer_session"];
        prepared.requiredClasses = classes;
        if(fs_assistant::featureAPI(input["api"].asString()))
        {
            const auto feature=feature_listener(input["api"].asString());
            const auto* entry=fs_assistant::featureOperation(input["api"].asString(),input["op"].asString());
            if(!feature || !entry){error="Feature is unavailable";return false;}
            if(!feature->prepareAssistant(prepared,error))return false;
            if(prepared.request["api"]!=input["api"] || prepared.request["op"]!=input["op"])
            {error="Feature changed its operation identity";return false;}
            if(*entry->permission && std::find(prepared.requiredClasses.begin(),prepared.requiredClasses.end(),entry->permission)==prepared.requiredClasses.end())
                prepared.requiredClasses.emplace_back(entry->permission);
            return true;
        }
        if (input["api"].asString() == "LLWorkspace") return fs_prepare_workspace(prepared, error);
        const std::string op = input["op"].asString();
        if (op == "executeBulkPlan" || op == "rename" || op == "move" || op == "batchRename" || op == "batchMove")
        {
            std::string plan_id;
            if (op == "executeBulkPlan")
            {
                plan_id = input["plan_id"].asUUID().asString();
                const std::string required = FSInventoryBulk::requiredClass(plan_id, error);
                if (!error.empty()) return false;
                prepared.requiredClasses = {required};
            }
            else if (!FSInventoryBulk::prepareLegacy(prepared.request, plan_id, error)) return false;
            prepared.validation["bulk_plan_id"] = plan_id;
            prepared.summary = FSInventoryBulk::summary(plan_id, error);
            return error.empty() && FSInventoryBulk::validate(plan_id, error);
        }
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
        if(fs_assistant::featureAPI(prepared.request["api"].asString()))
        {
            const auto feature=feature_listener(prepared.request["api"].asString());
            if(!feature){error="Feature became unavailable";return false;}
            return feature->validateAssistant(prepared,error);
        }
        if (prepared.request["api"].asString() == "LLWorkspace") return fs_validate_workspace(prepared, error);
        const std::string op = prepared.request["op"].asString();
        if (prepared.validation.has("bulk_plan_id")) return FSInventoryBulk::validate(prepared.validation["bulk_plan_id"].asString(), error);
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
        request["__assistant_guard"]=request_id;
        request["reply"] = mReplyPump.getName();
        Posted posted;
        posted.response = response;
        posted.api = api;
        posted.op = op;
        if(mPending.size()>=64){fail_response(response,HTTP_SERVICE_UNAVAILABLE,"Too many pending assistant operations");return;}
        posted.login=gAgent.getSessionID();posted.account=gAgentID;posted.generation=mSessionGeneration;
        posted.deadline=deadline>0.0?deadline:now_seconds()+CHAIN_GUARD_SECONDS;
        std::vector<std::string> required=prepared?prepared->requiredClasses:std::vector<std::string>{};
        if(required.empty())if(const auto* def=find_def(classify(api,op)))required.push_back(def->id);
        posted.gate=std::make_shared<fs_assistant::ExecutionGate>(required);
        mExecutions.erase(std::remove_if(mExecutions.begin(),mExecutions.end(),[](const auto& gate){return gate.expired();}),mExecutions.end());
        mExecutions.emplace_back(posted.gate);
        const bool durable=fs_assistant::requiresAsyncAuthority(api,op,is_read_op(api,op));
        if(durable)
        {
            for(auto it=mAsync.begin();it!=mAsync.end() && mAsync.size()>=64;)
                if(it->second->pending()==0 && it->second->state()!="pending"){mAsyncReadScopes.erase(it->first);it=mAsync.erase(it);}else ++it;
            if(mAsync.size()>=64){fail_response(response,HTTP_SERVICE_UNAVAILABLE,"Too many active assistant operations");return;}
            std::weak_ptr<State> weak=weak_from_this();
            posted.async=std::make_shared<fs_assistant::AsyncOperation>([weak,gate=posted.gate,generation=mSessionGeneration,
                account=gAgentID,login=gAgent.getSessionID(),grid=LLGridManager::instance().getGrid(),
                managed=FSSessionWorker::managed(),deadline=posted.deadline]() -> std::string
            {
                const auto self=weak.lock();
                if(!self || !self->mEnabled || self->mSessionGeneration!=generation)return "connection_changed";
                if(gDisconnected || LLApp::isExiting() || LLStartUp::getStartupState()!=STATE_STARTED || account!=gAgentID || login!=gAgent.getSessionID() || grid!=LLGridManager::instance().getGrid() || managed!=FSSessionWorker::managed())return "login_changed";
                if(now_seconds()>=deadline)return "deadline_expired";
                if(!gate->allowed())return "permission_revoked";
                return {};
            });
            if(prepared && (api=="LLAppearance" || fs_assistant::featureAPI(api)))
            {
                const auto frozen=*prepared;
                posted.async->checkWith([weak,frozen](){std::string error;const auto self=weak.lock();return self && self->validateOperation(frozen,error)?std::string():std::string("operation_source_changed");});
            }
            if(fs_assistant::needsActiveCharacter(api,op))
                posted.async->checkWith([api,op](){return FSSessionWorker::assistantActiveAllowed(api=="FSNavigation" && op=="teleport")?std::string():std::string("requires_active_character");});
            mAsync.emplace(request_id,posted.async);
            if(fs_assistant::featureAPI(api))
            {
                auto& reads=mAsyncReadScopes[request_id];
                const auto* entry=fs_assistant::featureOperation(api,op);
                if(entry && *entry->receiptRead)reads.emplace_back(entry->receiptRead);
                else for(const auto& permission:required)
                    if(const auto* def=find_def(permission))if(def->privateRead)reads.push_back(permission);
            }
        }
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
        if (prepared && (api == "LLWorkspace" || prepared->validation.has("bulk_plan_id") || op == "replaceLinks" || op == "snapshotUpload"))
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
            context.allowed = [weak, classes, generation, gate, account=gAgentID, login=gAgent.getSessionID(), api, op]()
            {
                auto self = weak.lock();
                return self && gate->allowed() && self->mEnabled && self->mSessionGeneration == generation && !gDisconnected && account==gAgentID && login==gAgent.getSessionID() &&
                    (!self->mIdentity["managed"].asBoolean() || !fs_assistant::needsActiveCharacter(api,op) || FSSessionWorker::assistantActiveAllowed(api=="FSNavigation" && op=="teleport")) &&
                    fs_assistant::decide(effective_levels(classes)) != fs_assistant::Decision::Deny;
            };
            if (api == "LLWorkspace")
            {
                std::string error; LLSD reply = fs_execute_workspace(*prepared, context, error);
                if (!error.empty()) { fail_response(response, HTTP_FORBIDDEN, error); mPending.erase(request_id); return; }
                reply["reqid"] = request_id; onReply(reply); return;
            }
            if (prepared->validation.has("bulk_plan_id"))
            {
                const std::string plan_id = prepared->validation["bulk_plan_id"].asString();
                FSInventoryBulk::execute(plan_id, context, [weak, request, op](const LLSD& result)
                {
                    if (auto self = weak.lock())
                    {
                        LLSD reply = op == "executeBulkPlan" ? result : FSInventoryBulk::legacyResult(request, result);
                        reply["reqid"] = request["reqid"];
                        self->onReply(reply);
                    }
                });
                return;
            }
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
        if(mAsks.size()>=32){fail_response(response,HTTP_SERVICE_UNAVAILABLE,"Too many pending assistant approvals");return;}
        prepared.summary["actor_name"]=mIdentity["name"];prepared.summary["actor_account"]=mIdentity["account"];prepared.summary["actor_grid"]=mIdentity["grid"];
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
        ++mApprovalRevision;
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
        if (found == mAsks.end() || found->second.gate.finished())
        {
            return;
        }
        Ask& ask = found->second;
        const bool canPresent=!FSSessionWorker::managed() || FSSessionWorker::assistantActiveAllowed();
        if(ask.dialogPosted)
        {
            if(auto* dialog=ask.dialog.get())
            {
                if(!canPresent)dialog->setVisible(false);
                else if(!dialog->getVisible()){dialog->setVisible(true);dialog->setFocus(true);}
            }
            return;
        }
        if(!canPresent)return;
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
            effective_levels(found->second.prepared.requiredClasses), mEnabled && (!FSSessionWorker::managed() || FSSessionWorker::assistantActiveAllowed()), expired);
        Ask ask = found->second;
        // Remove the terminal request before dismissing any UI: close callbacks
        // and old Yes callbacks cannot find or resurrect it.
        mAsks.erase(found);
        ++mApprovalRevision;
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
        if(!postedAllowed(found->second))
        {
            if (found->second.async) found->second.async->seal();
            auto receipt = asyncStatus(request_id);
            receipt["error_code"] = "session_changed";
            fail_response(found->second.response, HTTP_FORBIDDEN,
                "Assistant response belongs to an expired request", receipt);
            mPending.erase(found);
            return true;
        }
        found->second.sent = true;
        LLSD body = reply;
        body["viewer_session"]=mIdentity["viewer_session"];
        if(found->second.async)
        {
            if(reply.has("error"))found->second.async->cancel("native_operation_failed");
            found->second.async->seal();
            // A domain receipt may identify an earlier write being queried.
            // Keep its stable ID; expose this request's transport receipt separately.
            if(!body.has("operation_id"))body["operation_id"]=request_id;
            body["bridge_operation_id"]=request_id;
            body["operation"]=asyncStatus(request_id);
        }
        if (found->second.api == "LLInventory" && found->second.op == "status" && body.isMap())
        {
            body["permissions"] = effective_permissions();
            body["policy_generation"] = gPolicyGeneration;
            body["capabilities"] = assistant_capabilities();
        }
        found->second.response->extendedResult(HTTP_OK, to_json(body), json_headers());
        mPending.erase(found);
        return true;
    }

    std::string mToken;
    std::shared_ptr<FSAssistantSocial> mSocial;
    LLSD mSocialReadPolicy;
    LLSD mIdentity;
    LLEventStream mReplyPump;
    LLTempBoundListener mReplyConnection;
    std::map<LLUUID, Posted> mPending;
    std::map<LLUUID,fs_assistant::AsyncGuard> mAsync;
    std::map<LLUUID,std::vector<std::string>> mAsyncReadScopes;
    std::map<LLUUID, Ask> mAsks;
    std::deque<LLUUID> mAskOrder;
    std::vector<std::weak_ptr<fs_assistant::ExecutionGate>> mExecutions;
    U64 mSessionGeneration = 1;
    U64 mApprovalRevision=0;
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
        // Rotate stale login credentials before authorization, including legacy
        // ordinary clients that do not yet send an expected session reference.
        FSEventAPIBridge::instance().refreshSession();
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
    FSInventoryBulk::permissionsChanged();
    if (instanceExists() && instance().mState) instance().mState->permissionsChanged();
}

int FSEventAPIBridge::getPolicyGeneration()
{
    return gPolicyGeneration;
}

std::string FSEventAPIBridge::getPermissionLevel(const std::string& class_id)
{
    const auto* def = find_def(class_id);
    return def ? effective_level(def->kind) : "deny";
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
    if (enabled && FSSessionWorker::assistantPaused())
    {
        mLastError = "Local Assistant access is paused by the host";
        return false;
    }
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
        mSessionKey.clear();refreshSession();
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
    const std::string filename =
        "fs-mcp-" + std::to_string(LLApp::getPid()) + ".json";
    mDiscoveryPath = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, filename);
#if LL_WINDOWS
    mHostedRegistry=mHostedRegistry || FSSessionWorker::managed();
    if(mHostedRegistry)
    {
        const auto directory=fs_assistant::managedDiscoveryDirectory();
        if(directory.empty()){mLastError="Unable to protect managed assistant discovery";state->disable();return false;}
        mDiscoveryPath=ll_convert<std::string>((std::filesystem::path(directory)/ll_convert<std::wstring>(filename)).wstring());
    }
#endif
    U16 port = 0;
    LLHTTPNode& root = LLIOHTTPServer::create(
        gAPRPoolp, *mPump, LLSocket::PORT_EPHEMERAL, "127.0.0.1", port, 75.f);
    root.addNode(BRIDGE_PATH, new Node(state));
    mPort = port;

    mState = std::move(state);
    mSessionKey.clear();refreshSession();
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
    if (!mState || !mState->enabled() || mDiscoveryPath.empty())
    {
        return false;
    }
    const std::string temporary = mDiscoveryPath + ".tmp";
    if(mHostedRegistry && !mState->identity()["ready"].asBoolean()){removeDiscoveryFile();return true;}
    LLSD discovery = llsd::map(
        "api_version", 1,
        "apiVersion", API_VERSION,
        "pid", LLApp::getPid(),
        "port", mPort,
        "token", mState->token(),
        "host", "127.0.0.1",
        "scheme", "http",
        "path", BRIDGE_PATH);
    const auto& identity=mState->identity();
    for(auto it=identity.beginMap();it!=identity.endMap();++it)discovery[it->first]=it->second;
    const std::string payload = to_json(discovery);

    LLFILE* file = LLFile::fopen(temporary, "wb");
#if LL_WINDOWS
    if(mHostedRegistry && file && !fs_assistant::protectDiscoveryPath(ll_convert<std::wstring>(temporary)))
    {LLFile::close(file);LLFile::remove(temporary,ENOENT);return false;}
#endif
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

std::string FSEventAPIBridge::controllerLoginReference()
{
    if(LLStartUp::getStartupState()!=STATE_STARTED || gDisconnected)
    {mControllerLoginKey.clear();mControllerLoginReference.clear();return {};}
    if(mState && mState->enabled())
    {mControllerLoginKey.clear();mControllerLoginReference.clear();refreshSession();if(mState->enabled())return mViewerSession;}
    const auto worker=FSSessionWorker::assistantIdentity();
    // The native session UUID is only an internal invalidation key. It never
    // leaves this process; the host receives a fresh unrelated opaque UUID.
    const auto key=gAgentID.asString()+":"+gAgent.getSessionID().asString()+":"+LLGridManager::instance().getGrid()+":"+
        (FSSessionWorker::managed()?"managed":"standalone")+":"+worker["worker"].asString()+":"+worker["generation"].asString()+":"+mViewerSession;
    if(key!=mControllerLoginKey || mControllerLoginReference.empty())
    {mControllerLoginKey=key;mControllerLoginReference=LLUUID::generateNewID().asString();}
    return mControllerLoginReference;
}

void FSEventAPIBridge::refreshSession()
{
    // Preview cleanup must run even while the authenticated bridge is paused.
    fs_preferences_tick();
    if(!mState || !mState->enabled())return;
    mState->maintenance();
    const bool ready=LLStartUp::getStartupState()==STATE_STARTED && !gDisconnected;
    const bool managed=FSSessionWorker::managed();
    const auto grid=LLGridManager::instance().getGrid();
    const auto key=gAgentID.asString()+":"+gAgent.getSessionID().asString()+":"+grid+":"+(managed?"managed":"standalone")+":"+(ready?"ready":"offline");
    std::string name;LLAgentUI::buildFullname(name);
    auto identity=llsd::map("viewer_session",mViewerSession,"managed",managed,"account",gAgentID.asString(),"grid",grid,"name",name,"ready",ready);
    identity["display_name"]=name;
    identity["active"]=!managed || FSSessionWorker::assistantActiveAllowed();
    const auto workerInfo=FSSessionWorker::assistantIdentity();
    for(auto it=workerInfo.beginMap();it!=workerInfo.endMap();++it)if(!it->second.asString().empty())identity[it->first]=it->second;
    if(key==mSessionKey)
    {
        if(!llsd_equals(mState->identity(),identity)){mState->identity(identity);if(!writeDiscovery()){mState->disable();removeDiscoveryFile();}}
        return;
    }
    mState->disable();
    mSessionKey=key;mViewerSession=LLUUID::generateNewID().asString();
    const auto token=make_token();
    if(token.empty()){removeDiscoveryFile();mLastError="Unable to refresh the assistant connection";return;}
    mState->setToken(token);
    identity["viewer_session"]=mViewerSession;mState->identity(identity);
    mState->enable();
    if(!writeDiscovery()){mState->disable();removeDiscoveryFile();mLastError="Unable to update assistant discovery";}
}

void FSEventAPIBridge::nearbyMessage(const LLChat& message)
{if(mState && mState->enabled())mState->nearbyMessage(message);}
void FSEventAPIBridge::reviewPending() {if(mState && FSSessionWorker::assistantActiveAllowed())mState->review();}
void FSEventAPIBridge::noteSharedPosePrivacyChanged()
{if(instanceExists() && instance().mState)instance().mState->sharedPosePrivacyChanged();}
bool FSEventAPIBridge::requestAllowed(const LLSD& request)
{
    if(!request.has("__assistant_guard"))return true;
    return instanceExists() && instance().mState && instance().mState->requestAllowed(request);
}
fs_assistant::AsyncGuard FSEventAPIBridge::asyncContext(const LLSD& request)
{
    return instanceExists() && instance().mState?instance().mState->asyncContext(request):fs_assistant::AsyncGuard();
}
fs_assistant::AsyncGuard fs_assistant_async_context(const LLSD& request){return FSEventAPIBridge::asyncContext(request);}
bool fs_assistant_request_allowed(const LLSD& request) {return FSEventAPIBridge::requestAllowed(request);}

void FSEventAPIBridge::stop()
{
    if (mState)
    {
        mState->disable();
        mState.reset();
    }
    removeDiscoveryFile();
    mDiscoveryPath.clear();
    mSessionKey.clear();mViewerSession.clear();
    mControllerLoginKey.clear();mControllerLoginReference.clear();
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

std::string FSEventAPIBridge::discoveryDirectory() const
{
#if LL_WINDOWS
    if(mHostedRegistry || FSSessionWorker::managed())return ll_convert<std::string>(fs_assistant::managedDiscoveryDirectory());
#endif
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "");
}

std::string FSEventAPIBridge::configurationDiscovery() const
{
    std::vector<std::string> roots{gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "")};
#if LL_WINDOWS
    // Generated setup connects ordinary viewers and private hosted profiles.
    // Deliberate external overrides are kept exactly as provided.
    const DWORD overrideSize=GetEnvironmentVariableW(L"FIRESTORM_MCP_DISCOVERY",nullptr,0);
    if(overrideSize>32768)return {};
    if(overrideSize){std::vector<wchar_t> value(overrideSize);const DWORD read=GetEnvironmentVariableW(L"FIRESTORM_MCP_DISCOVERY",value.data(),overrideSize);if(!read || read>=overrideSize)return {};return ll_convert<std::string>(std::wstring(value.data(),read));}
    roots.push_back(ll_convert<std::string>(fs_assistant::managedDiscoveryDirectory()));
    PWSTR roaming=nullptr;
    if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&roaming)))
    {
        const std::filesystem::path base(roaming);CoTaskMemFree(roaming);
        for(const auto* name:{L"Folderstorm_x64",L"FolderstormOS_x64",L"Folderstorm",L"FolderstormOS",L"Firestorm_x64",L"FirestormOS_x64",L"Firestorm",L"FirestormOS"})
            roots.push_back(ll_convert<std::string>((base/name/L"user_settings").wstring()));
    }
    return fs_assistant::discoveryScope(roots,';');
#else
    const auto* overrideValue=std::getenv("FIRESTORM_MCP_DISCOVERY");
    if(overrideValue && *overrideValue)return overrideValue;
    return fs_assistant::discoveryScope(roots,':');
#endif
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
