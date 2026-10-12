/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fssquirtynotes.h"
#include "fssquirtypolicy.h"

#include "llagent.h"
#include "llavatarnamecache.h"
#include "llcallingcard.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llmutelist.h"
#include "lluserrelations.h"
#include "llsecapi.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include <boost/json.hpp>
#include <chrono>
#include <deque>

namespace
{
using Clock = std::chrono::steady_clock;
double now() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

struct Request
{
    S32 port;
    std::string token;
    std::string title;
    bool check = false;
    double expires = 0.;
    U64 generation = 0;
    bool automatic = true;
};

FSSquirtyPolicy::Activity activity;
std::deque<Request> requests;
bool running = false;
double retry_after = 0.;
U64 generation = 0;
std::string last_status = "NotTested";

FSSquirtyPolicy::Mode mode(const char* name)
{
    const S32 value = gSavedPerAccountSettings.getS32(name);
    return value == 1 ? FSSquirtyPolicy::Mode::All :
           value == 2 ? FSSquirtyPolicy::Mode::Custom : FSSquirtyPolicy::Mode::Off;
}

bool selected(const char* setting, const LLUUID& id)
{
    return gSavedPerAccountSettings.getLLSD(setting).has(id.asString());
}

bool ready() { return LLStartUp::getStartupState() == STATE_STARTED && !gAgent.getID().isNull(); }

std::string requestStatus(const LLSD& result, bool check)
{
    const auto http = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(
        result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
    if (http.getType() == 401) return "BadToken";
    if (http.getType() == 429) return "Busy";
    const auto& raw = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
    if (raw.size() > 65536) return "Failed";
    boost::system::error_code error;
    const auto json = boost::json::parse(std::string(raw.begin(), raw.end()), error);
    if (error) return http ? "Failed" : "Unavailable";
    if (check && http.getType() == 200 && json.is_object())
    {
        const auto displays = json.as_object().if_contains("displays");
        if (displays && displays->is_array()) return "Connected";
    }
    if (!check && http.getType() == 202 && json.is_object())
    {
        auto accepted = json.as_object().if_contains("accepted");
        if (accepted && accepted->is_bool() && accepted->as_bool()) return "Queued";
    }
    if (json.is_object())
    {
        auto reason = json.as_object().if_contains("error");
        if (reason && reason->is_string() && reason->as_string() == "Reminders are paused") return "Paused";
    }
    return http ? "Failed" : "Unavailable";
}

void pump()
{
    if (running || requests.empty()) return;
    running = true;
    LLCoros::instance().launch("squirtyNotes", []()
    {
        while (!requests.empty())
        {
            Request value = std::move(requests.front());
            requests.pop_front();
            if (value.generation != generation || value.expires < now() ||
                (value.automatic && (!ready() || gAgent.isDoNotDisturb()))) continue;
            last_status = "Sending";
            std::string outcome = "Unavailable";
            try
            {
                LLCoreHttpUtil::HttpCoroutineAdapter adapter("SquirtyNotes", LLCore::HttpRequest::DEFAULT_POLICY_ID);
                auto request = std::make_shared<LLCore::HttpRequest>();
                auto options = std::make_shared<LLCore::HttpOptions>();
                options->setTimeout(2);
                options->setTransferTimeout(2);
                options->setRetries(0);
                options->setFollowRedirects(false);
                options->setPrivateLocalRequest(true);
                auto headers = std::make_shared<LLCore::HttpHeaders>();
                headers->append("Authorization", "Bearer " + value.token);
                headers->append("Content-Type", "application/json");
                const std::string base = "http://127.0.0.1:" + std::to_string(value.port) + "/v1/";
                LLSD result;
                if (value.check)
                {
                    result = adapter.getRawAndSuspend(request, base + "displays", options, headers);
                }
                else
                {
                    boost::json::object body;
                    body["title"] = value.title;
                    const std::string json = boost::json::serialize(body);
                    LLCore::BufferArray::ptr_t raw(new LLCore::BufferArray());
                    raw->append(json.data(), json.size());
                    result = adapter.postRawAndSuspend(request, base + "squirts", raw, options, headers);
                }
                outcome = requestStatus(result, value.check);
            }
            catch (const std::exception&)
            {
                // Exception text can contain headers; display only our status key.
            }
            if (value.generation != generation) continue;
            last_status = outcome;
            if (outcome != "Connected" && outcome != "Queued")
            {
                retry_after = now() + 30.;
                requests.clear();
            }
        }
        running = false;
    });
}

void enqueue(Request value, bool automatic)
{
    if (value.port < 1024 || value.port > 65535 || !FSSquirtyPolicy::validToken(value.token))
    {
        last_status = "SetupNeeded";
        return;
    }
    if (automatic && now() < retry_after) return;
    if (requests.size() >= 20) { last_status = "Busy"; return; }
    value.generation = generation;
    value.automatic = automatic;
    value.expires = now() + 10.;
    requests.push_back(std::move(value));
    pump();
}

void notify(const std::string& title)
{
    enqueue({gSavedSettings.getS32("FSSquirtyPort"), FSSquirtyNotes::token(), utf8str_truncate(title, 120)}, true);
}
}

namespace FSSquirtyNotes
{
void reset()
{
    ++generation;
    requests.clear();
    activity.reset();
    retry_after = 0.;
    last_status = "NotTested";
}

void seedFriends()
{
    LLAvatarTracker::buddy_map_t friends;
    LLAvatarTracker::instance().copyBuddyList(friends);
    for (const auto& entry : friends)
        activity.seedPresence(entry.first.asString(), entry.second->isOnline());
}

void onIM(const LLUUID& sender, const std::string& name)
{
    if (!ready() || sender.isNull() || sender == gAgent.getID()) return;
    if (activity.incoming(sender.asString(), now(), gSavedPerAccountSettings.getU32("FSSquirtyIMSilence"),
                          mode("FSSquirtyIMMode"), selected("FSSquirtyIMResidents", sender), gAgent.isDoNotDisturb()))
        notify(name + " sent you an IM");
}

void onFriendStatus(const LLUUID& id, bool online)
{
    const bool squirt = activity.presence(id.asString(), online, mode("FSSquirtyOnlineMode"),
        selected("FSSquirtyOnlineResidents", id), !ready() || gAgent.isDoNotDisturb() || LLMuteList::getInstance()->isMuted(id));
    if (!squirt) return;
    const U64 expected_generation = generation;
    LLAvatarNameCache::get(id, [id, expected_generation](const LLUUID&, const LLAvatarName& name)
    {
        const auto* friend_info = LLAvatarTracker::instance().getBuddyInfo(id);
        if (expected_generation == generation && ready() && !gAgent.isDoNotDisturb() &&
            friend_info && friend_info->isOnline() && !LLMuteList::getInstance()->isMuted(id) &&
            FSSquirtyPolicy::allows(mode("FSSquirtyOnlineMode"), selected("FSSquirtyOnlineResidents", id)))
            notify(name.getCompleteName() + " is online");
    });
}

std::string token()
{
    return gSecAPIHandler ? gSecAPIHandler->getProtectedData("squirty_notes", "local_api").asString() : "";
}

void setToken(const std::string& value)
{
    if (!gSecAPIHandler) return;
    if (value.empty()) gSecAPIHandler->deleteProtectedData("squirty_notes", "local_api");
    else gSecAPIHandler->setProtectedData("squirty_notes", "local_api", LLSD(value));
}

std::string status() { return last_status; }
void test(S32 port, const std::string& value, bool squirt)
{
    enqueue({port, value, "Folderstorm test squirt", !squirt}, false);
}
void configurationChanged()
{
    ++generation;
    requests.clear();
    retry_after = 0.;
    last_status = "NotTested";
}
}
