/**
 * @file fseventapibridge.cpp
 * @brief Authenticated loopback bridge for viewer Event APIs.
 */

#include "llviewerprecompiledheaders.h"

#include "fseventapibridge.h"

#include "llapr.h"
#include "llapp.h"
#include "lldir.h"
#include "lleventapi.h"
#include "llevents.h"
#include "llfile.h"
#include "llhttpconstants.h"
#include "llhttpnode.h"
#include "lliohttpserver.h"
#include "lliosocket.h"
#include "llsdjson.h"
#include "lluuid.h"

#include <boost/json.hpp>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <cstdio>
#include <cstring>
#include <cerrno>
#include <iomanip>
#include <map>
#include <sstream>

#if !LL_WINDOWS
#include <sys/stat.h>
#endif

namespace
{
constexpr const char* BRIDGE_PATH = "/firestorm/event-api";
constexpr const char* API_VERSION = "1";
constexpr std::size_t TOKEN_BYTES = 32;
constexpr std::size_t MAX_REQUEST_BYTES = 1024 * 1024;

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
}

class FSEventAPIBridge::State
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
            response->extendedResult(
                HTTP_BAD_REQUEST,
                to_json(llsd::map("error", "Both api and op are required")),
                json_headers());
            return;
        }

        LLSD request(input);
        const LLUUID request_id = LLUUID::generateNewID();
        request["reqid"] = request_id;
        request["reply"] = mReplyPump.getName();
        mPending.emplace(request_id, response);

        if (!LLEventAPI::getInstance(api))
        {
            auto found = mPending.find(request_id);
            if (found != mPending.end())
            {
                found->second->extendedResult(
                    HTTP_NOT_FOUND,
                    to_json(llsd::map("error", "Event API was not found or did not accept the request")),
                    json_headers());
                mPending.erase(found);
            }
            return;
        }
        LLEventPumps::instance().obtain(api).post(request);
    }

    void disable()
    {
        mEnabled = false;
        for (auto& pending : mPending)
        {
            pending.second->extendedResult(
                HTTP_SERVICE_UNAVAILABLE,
                to_json(llsd::map("error", "Viewer Event API bridge is shutting down")),
                json_headers());
        }
        mPending.clear();
    }

    const std::string& token() const { return mToken; }

private:
    bool onReply(const LLSD& reply)
    {
        const LLUUID request_id = reply["reqid"].asUUID();
        auto found = mPending.find(request_id);
        if (found == mPending.end())
        {
            return false;
        }

        found->second->extendedResult(HTTP_OK, to_json(reply), json_headers());
        mPending.erase(found);
        return true;
    }

    std::string mToken;
    LLEventStream mReplyPump;
    LLTempBoundListener mReplyConnection;
    std::map<LLUUID, ResponsePtr> mPending;
    bool mEnabled;
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
            response->extendedResult(
                HTTP_UNAUTHORIZED,
                to_json(llsd::map("error", "Unauthorized")),
                json_headers());
            return;
        }

        const std::string body = input.asString();
        if (body.empty() || body.size() > MAX_REQUEST_BYTES)
        {
            response->extendedResult(
                HTTP_BAD_REQUEST,
                to_json(llsd::map("error", "Invalid request size")),
                json_headers());
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
            response->extendedResult(
                HTTP_BAD_REQUEST,
                to_json(llsd::map("error", error.what())),
                json_headers());
        }
    }

    void options(ResponsePtr response, const LLSD&) const override
    {
        response->status(HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed");
    }

private:
    std::shared_ptr<State> mState;
};

FSEventAPIBridge::FSEventAPIBridge() = default;

FSEventAPIBridge::~FSEventAPIBridge()
{
    stop();
}

bool FSEventAPIBridge::start(LLPumpIO& pump)
{
    if (mState)
    {
        return true;
    }

    std::string token = make_token();
    if (token.empty())
    {
        LL_WARNS("EventAPIBridge") << "Unable to generate authentication token" << LL_ENDL;
        return false;
    }

    auto state = std::make_shared<State>(std::move(token));
    U16 port = 0;
    LLHTTPNode& root = LLIOHTTPServer::create(
        gAPRPoolp, pump, LLSocket::PORT_EPHEMERAL, "127.0.0.1", port);
    root.addNode(BRIDGE_PATH, new Node(state));

    const std::string filename =
        "firestorm-mcp-" + std::to_string(LLApp::getPid()) + ".json";
    mDiscoveryPath = gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, filename);
    const std::string temporary = mDiscoveryPath + ".tmp";

    LLSD discovery = llsd::map(
        "apiVersion", API_VERSION,
        "pid", LLApp::getPid(),
        "port", port,
        "token", state->token(),
        "endpoint", BRIDGE_PATH);
    const std::string payload = to_json(discovery);

    LLFILE* file = LLFile::fopen(temporary, "wb");
    const bool write_ok =
        file && std::fwrite(payload.data(), 1, payload.size(), file) == payload.size();
    const bool close_ok = !file || LLFile::close(file) == 0;
    if (!write_ok || !close_ok)
    {
        LLFile::remove(temporary, ENOENT);
        state->disable();
        mDiscoveryPath.clear();
        LL_WARNS("EventAPIBridge") << "Unable to write discovery file" << LL_ENDL;
        return false;
    }
#if !LL_WINDOWS
    chmod(temporary.c_str(), S_IRUSR | S_IWUSR);
#endif
    if (LLFile::rename(temporary, mDiscoveryPath) != 0)
    {
        LLFile::remove(temporary, ENOENT);
        state->disable();
        mDiscoveryPath.clear();
        return false;
    }

    mState = std::move(state);
    LL_INFOS("EventAPIBridge") << "Local Event API bridge listening on 127.0.0.1:"
                                << port << LL_ENDL;
    return true;
}

void FSEventAPIBridge::stop()
{
    if (mState)
    {
        mState->disable();
        mState.reset();
    }
    if (!mDiscoveryPath.empty())
    {
        LLFile::remove(mDiscoveryPath, ENOENT);
        mDiscoveryPath.clear();
    }
}

bool FSEventAPIBridge::isRunning() const
{
    return static_cast<bool>(mState);
}

const std::string& FSEventAPIBridge::getDiscoveryPath() const
{
    return mDiscoveryPath;
}
