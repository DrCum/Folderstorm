/**
 * @file fseventapibridge.cpp
 * @brief Authenticated loopback bridge for viewer Event APIs.
 *
 * Action classes are enforced here, before an event is posted. purge and
 * emptyTrash are hard-denied. A settings value cannot enable them.
 */

#include "llviewerprecompiledheaders.h"

#include "fseventapibridge.h"

#include "llapr.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "lleventapi.h"
#include "llevents.h"
#include "llfile.h"
#include "llhttpconstants.h"
#include "llhttpnode.h"
#include "lliohttpserver.h"
#include "lliosocket.h"
#include "llnotificationsutil.h"
#include "llsdjson.h"
#include "lltimer.h"
#include "lluuid.h"
#include "llviewercontrol.h"

#include <boost/json.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <deque>
#include <iomanip>
#include <initializer_list>
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

enum class ActionClass
{
    Unknown,
    Read,
    Camera,
    Create,
    Edit,
    Move,
    Trash,
    NoCopy,
    Wear,
    Links,
    Permanent
};

struct ClassDef
{
    ActionClass kind;
    const char* id;
    const char* title;
    const char* fallback;
    bool ask;
};

const ClassDef CLASS_DEFS[] = {
    {ActionClass::Read, "read", "Read inventory and outfits", "allow", false},
    {ActionClass::Camera, "camera", "Move camera and take pictures", "allow", true},
    {ActionClass::Create, "create", "Create folders and items", "allow", true},
    {ActionClass::Edit, "edit", "Rename and edit details", "allow", true},
    {ActionClass::Move, "move", "Move and copy", "allow", true},
    {ActionClass::Trash, "trash", "Trash (can be restored)", "allow", true},
    {ActionClass::NoCopy, "nocopy", "Move no-copy items during a copy", "ask", true},
    {ActionClass::Wear, "wear", "Wear and detach", "ask", true},
    {ActionClass::Links, "links", "Replace links", "ask", true},
};

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

bool level_allowed(const ClassDef& def, const std::string& level)
{
    if (level == "allow" || level == "deny")
    {
        return true;
    }
    return def.ask && level == "ask";
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

std::string first_text(const LLSD& input, std::initializer_list<const char*> keys)
{
    for (const char* key : keys)
    {
        if (!input.has(key))
        {
            continue;
        }
        const std::string text = input[key].asString();
        if (!text.empty())
        {
            return text;
        }
    }
    return {};
}

std::string target_label(const LLSD& input)
{
    const std::string named = first_text(
        input, {"name", "folder_name", "new_name", "preset", "path"});
    if (!named.empty())
    {
        return named;
    }
    if (input.has("items_id"))
    {
        const LLSD& items = input["items_id"];
        if (items.isArray() && items.size() > 0)
        {
            return items.size() == 1 ? items[0].asString()
                                     : items[0].asString() + " +" + std::to_string(items.size() - 1);
        }
        const std::string one = items.asString();
        if (!one.empty())
        {
            return one;
        }
    }
    return first_text(
        input, {"folder_id", "id", "item_id", "source_id", "outfit_id", "parent_id"});
}

std::string action_phrase(const std::string& op, ActionClass kind)
{
    if (op == "wearOutfit")
    {
        return "Wear outfit";
    }
    if (op == "wearItems")
    {
        return "Wear items";
    }
    if (op == "detachItems")
    {
        return "Detach";
    }
    if (op == "trash")
    {
        return "Move to Trash";
    }
    if (op == "restore")
    {
        return "Restore";
    }
    if (op == "confirmCopy")
    {
        return "Move no-copy items";
    }
    if (op == "replaceLinks")
    {
        return "Replace links";
    }
    if (op == "snapshot")
    {
        return "Take a picture";
    }
    if (op == "set" || op == "setPose" || op == "reset")
    {
        return "Move the camera";
    }
    if (const ClassDef* def = find_def(kind))
    {
        return def->title;
    }
    return "Local assistant";
}

std::string action_message(const std::string& op, ActionClass kind, const LLSD& input)
{
    const std::string phrase = action_phrase(op, kind);
    const std::string target = target_label(input);
    if (target.empty())
    {
        return phrase;
    }
    return phrase + " " + target;
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

void fail_response(const LLHTTPNode::ResponsePtr& response, S32 code, const std::string& error, const LLSD& extra = LLSD())
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
            enqueueAsk(input, response, api, op, kind);
            return;
        }
        postEvent(input, response, api, op, op_needs_confirm(op));
    }

    void disable()
    {
        mEnabled = false;
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

    void enable() { mEnabled = true; }
    bool enabled() const { return mEnabled; }
    void setToken(std::string token) { mToken = std::move(token); }
    const std::string& token() const { return mToken; }

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
        LLSD request;
        std::string api;
        std::string op;
        bool needsConfirm = false;
        std::string title;
        std::string message;
        F64 enqueuedAt = 0.0;
        F64 chainDeadline = 0.0;
        F64 dialogDeadline = 0.0;
        bool dialogPosted = false;
        bool finished = false;
        LLNotificationPtr notification;
    };

    void postEvent(LLSD request, ResponsePtr response, const std::string& api, const std::string& op, bool needsConfirm)
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
        LLEventPumps::instance().obtain(api).post(request);
    }

    void enqueueAsk(const LLSD& input, ResponsePtr response, const std::string& api, const std::string& op, ActionClass kind)
    {
        Ask ask;
        ask.key = LLUUID::generateNewID();
        ask.response = response;
        ask.request = input;
        ask.api = api;
        ask.op = op;
        ask.needsConfirm = op_needs_confirm(op);
        ask.title = find_def(kind) ? find_def(kind)->title : "Local assistant";
        ask.message = action_message(op, kind, input);
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
            if (found == mAsks.end() || found->second.finished)
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
        if (found == mAsks.end() || found->second.finished || found->second.dialogPosted)
        {
            return;
        }
        Ask& ask = found->second;
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
        ask.notification = LLNotificationsUtil::add(
            "LocalAssistantConfirm",
            llsd::map("TITLE", ask.title, "MESSAGE", ask.message),
            LLSD(),
            [weak, key](const LLSD& notification, const LLSD& response)
            {
                std::shared_ptr<State> self = weak.lock();
                if (!self)
                {
                    return;
                }
                const bool allow = LLNotificationsUtil::getSelectedOption(notification, response) == 0;
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
        if (found == mAsks.end() || found->second.finished)
        {
            return;
        }
        Ask ask = found->second;
        found->second.finished = true;
        if (!fromDialog && ask.notification)
        {
            LLNotificationPtr notification = ask.notification;
            found->second.notification = nullptr;
            LLNotificationsUtil::cancel(notification);
        }
        mAsks.erase(found);
        mAskOrder.erase(std::remove(mAskOrder.begin(), mAskOrder.end(), key), mAskOrder.end());

        const bool expired = now_seconds() >= ask.chainDeadline;
        if (!allow || !mEnabled || expired)
        {
            // Send the denial ourselves while the chain is still inside its 75s budget.
            // extendedResult no-ops if that chain has already dropped the pipe.
            fail_response(ask.response, HTTP_FORBIDDEN, expired ? ERR_TIMED_OUT : (error.empty() ? ERR_DENIED : error));
            scheduleShow();
            return;
        }
        postEvent(ask.request, ask.response, ask.api, ask.op, ask.needsConfirm);
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
            LL_WARNS("EventAPIBridge") << "Unable to generate authentication token" << LL_ENDL;
            return false;
        }
        mState->setToken(std::move(token));
        mState->enable();
        if (!writeDiscovery())
        {
            mState->disable();
            return false;
        }
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
        mState->disable();
        return false;
    }
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
