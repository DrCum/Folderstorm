/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsworldlistener.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "llagent.h"
#include "llapp.h"
#include "llaudioengine.h"
#include "lldate.h"
#include "llmediaentry.h"
#include "llparcel.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llstartup.h"
#include "llstreamingaudio.h"
#include "llstring.h"
#include "lltextureentry.h"
#include "lltimer.h"
#include "llviewercontrol.h"
#include "llviewermedia.h"
#include "llviewernetwork.h"
#include "llviewerobjectlist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "llvovolume.h"
#include "rlvactions.h"
#include "rlvhandler.h"

#include <algorithm>
#include <cmath>

namespace
{
bool ready()
{
    return LLStartUp::getStartupState() == STATE_STARTED && !gDisconnected &&
        gAgent.getID().notNull() && gAgent.getSessionID().notNull() && gAgent.getRegion();
}
fs_world::Identity identity()
{
    auto* region = gAgent.getRegion();
    return {gAgent.getID().asString(), gAgent.getSessionID().asString(),
        LLGridManager::instance().getGrid(), region ? region->getRegionID().asString() : ""};
}
bool visible(const std::string& op)
{
    return fs_world::visible(op, !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC),
        !gRlvHandler.hasBehaviour(RLV_BHVR_SHOWNAMES), RlvActions::canShowNearbyAgents());
}
bool arguments(const LLSD& request, double& radius, int& limit, std::string& error)
{
    if (!fs_world::operation(request["op"].asString()))
    { error = "Unknown world observation"; return false; }
    radius = fs_world::DEFAULT_RADIUS;
    limit = fs_world::DEFAULT_ROWS;
    if (request.has("radius"))
    {
        if (!request["radius"].isReal() && !request["radius"].isInteger())
        { error = "radius must be a number"; return false; }
        radius = request["radius"].asReal();
    }
    if (request.has("limit"))
    {
        if (!request["limit"].isInteger()) { error = "limit must be an integer"; return false; }
        limit = request["limit"].asInteger();
    }
    // Live snapshots have no cursors. Reject them rather than silently giving
    // an unrelated page after a region/login transition.
    if (request.has("offset") || request.has("cursor") || !fs_world::bounds(radius, limit))
    { error = "World observations require radius 1..256 and limit 1..100; pagination is unavailable"; return false; }
    return true;
}
LLSD base(const LLSD& request, U64 generation, int limit)
{
    LLSD result = llsd::map("world_observation", 1, "op", request["op"],
        "observed_at", LLDate::now().asString(), "account", gAgent.getID(),
        "grid", LLGridManager::instance().getGrid(), "limit", limit, "truncated", false);
    // A bridge reference identifies the actual login without exposing the
    // simulator SessionID, which is an authentication credential.
    result["viewer_session"] = FSEventAPIBridge::instanceExists() ?
        FSEventAPIBridge::instance().sessionReference() : std::string();
    result["region_generation"] = std::to_string(generation);
    result["region_generation_scope"] = "transitions_observed_by_this_service";
    result["coverage"] = "loaded_viewer_state_only";
    result["complete"] = false;
    result["freshness"] = "last_native_update_unknown";
    result["render_allowed"] = FSSessionWorker::renderAllowed();
    result["background_audio_muted"] = FSSessionWorker::backgroundAudioMuted();
    const LLSD worker = FSSessionWorker::assistantIdentity();
    if (worker.has("profile")) result["profile"] = worker["profile"];
    result["status"] = "observed";
    return result;
}
void unavailable(LLSD& result, const char* reason)
{
    result["status"] = "unavailable";
    result["reason"] = reason;
}
bool inRange(const LLVector3d& position, const LLVector3d& origin, double radius)
{
    return std::isfinite(position.mdV[0]) && std::isfinite(position.mdV[1]) &&
        std::isfinite(position.mdV[2]) && dist_vec_squared(position, origin) <= radius * radius;
}
LLSD objectPosition(LLViewerObject* object, const LLVector3d& origin)
{
    return llsd::map("id", object->getID(), "region_id", object->getRegion()->getRegionID(),
        "global_position", ll_sd_from_vector3d(object->getPositionGlobal()),
        "region_position", ll_sd_from_vector3(object->getPositionRegion()),
        "distance", std::sqrt(dist_vec_squared(object->getPositionGlobal(), origin)));
}
void appendRows(LLSD& result, const fs_world::Rows<LLSD>& rows, bool scanTruncated)
{
    result["entries"] = LLSD::emptyArray();
    for (const auto& row : rows.values()) result["entries"].append(row.second);
    result["truncated"] = rows.truncated() || scanTruncated;
    result["scan_truncated"] = scanTruncated;
    result["order"] = "stable_id";
    result["pagination"] = "unavailable_live_snapshot";
}
void location(LLSD& result)
{
    auto* region = gAgent.getRegion();
    result["region_id"] = region->getRegionID();
    result["region_name"] = utf8str_truncate(region->getName(), 256);
    result["region_position"] = ll_sd_from_vector3(gAgent.getPositionAgent());
    result["global_position"] = ll_sd_from_vector3d(gAgent.getPositionGlobal());
    const LLQuaternion quat = gAgent.getQuat();
    result["orientation"] = llsd::array(quat.mQ[0], quat.mQ[1], quat.mQ[2], quat.mQ[3]);
    result["orientation_format"] = "quaternion_xyzw";
}
bool currentParcel(const LLParcel* parcel)
{
    auto& manager=LLViewerParcelMgr::instance();
    return parcel && fs_world::parcelCurrent(parcel->getLocalID(),parcel->getRegionID().asString(),
        gAgent.getRegion()->getRegionID().asString(),manager.getTeleportInProgress(),
        manager.inAgentParcel(gAgent.getPositionGlobal()));
}
void parcel(LLSD& result)
{
    const LLParcel* parcel = LLViewerParcelMgr::instance().getAgentParcel();
    if (!currentParcel(parcel))
        return unavailable(result, "current_region_parcel_not_loaded");
    result["region_id"] = gAgent.getRegion()->getRegionID();
    result["parcel_local_id"] = parcel->getLocalID();
    result["name"] = utf8str_truncate(parcel->getName(), 256);
    result["description"] = utf8str_truncate(parcel->getDesc(), 1024);
    result["owner_id"] = parcel->getOwnerID();
    result["group_owned"] = parcel->getIsGroupOwned();
    // Raw flags also contain administrative state; publish only this schema.
    result["flags"] = llsd::map("allow_fly", parcel->getAllowFly(),
        "allow_damage", parcel->getAllowDamage(),
        "allow_group_scripts", parcel->getAllowGroupScripts(),
        "allow_other_scripts", parcel->getAllowOtherScripts());
    result["provenance"] = "current_agent_parcel_cache";
}
void avatars(LLSD& result, double radius, int limit)
{
    const LLVector3d origin = gAgent.getPositionGlobal();
    fs_world::Rows<LLSD> rows(limit);
    int scanned = 0;
    bool scanTruncated = false;
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        if (scanned++ >= fs_world::MAX_SCAN) { scanTruncated = true; break; }
        // Other LLCharacter subclasses must not be treated as avatars.
        auto* avatar = dynamic_cast<LLVOAvatar*>(character);
        if (!avatar || avatar->isDead() || avatar->isSelf() || avatar->isControlAvatar() ||
            !avatar->getRegion() || !inRange(avatar->getPositionGlobal(), origin, radius)) continue;
        LLSD row = objectPosition(avatar, origin);
        // getFullname reads loaded FirstName/LastName fields. Name-cache get()
        // can schedule an HTTP request, so this service deliberately avoids it.
        const auto name = avatar->getFullname();
        row["name_status"] = name.empty() ? "unresolved" : "loaded_legacy_name";
        if (!name.empty()) row["legacy_name"] = utf8str_truncate(name, 256);
        row["display_name_status"] = "unavailable_without_name_lookup";
        row["provenance"] = "loaded_avatar";
        rows.add(avatar->getID().asString(), row);
    }
    appendRows(result, rows, scanTruncated);
}
bool observableObject(LLViewerObject* object, const LLVector3d& origin, double radius, bool rootsOnly)
{
    return object && !object->isDead() && !object->isAvatar() && object->getVolume() &&
        !object->isAttachment() && object->getRegion() && (!rootsOnly || object->isRoot()) &&
        inRange(object->getPositionGlobal(), origin, radius);
}
void objects(LLSD& result, double radius, int limit)
{
    const LLVector3d origin = gAgent.getPositionGlobal();
    fs_world::Rows<LLSD> rows(limit);
    const S32 count = gObjectList.getNumObjects();
    for (S32 i = 0; i < std::min(count, fs_world::MAX_SCAN); ++i)
    {
        auto* object = gObjectList.getObject(i);
        if (!observableObject(object, origin, radius, true)) continue;
        LLSD row = objectPosition(object, origin);
        row["name_status"] = "unavailable_without_property_fetch";
        row["owner_status"] = "unavailable_without_property_fetch";
        row["provenance"] = "loaded_root_volume";
        rows.add(object->getID().asString(), row);
    }
    appendRows(result, rows, count > fs_world::MAX_SCAN);
}
void audio(LLSD& result, double radius, int limit, const fs_world::Epoch& epoch)
{
    result["provenance"] = "viewer_sound_history";
    result["history_window"] = "since_first_observed_login_or_region";
    result["history_since_viewer_seconds"] = epoch.audioSince();
    result["range_basis"] = "historically_observed_global_position";
    result["audibility"] = "unknown";
    if (!gAudiop) return unavailable(result, "audio_engine_unavailable");
    result["engine_muted"] = gAudiop->getMuted();
    const LLVector3d origin = gAgent.getPositionGlobal();
    const F64 now = LLTimer::getElapsedSeconds();
    fs_world::Rows<LLSD> rows(limit);
    int scanned = 0;
    bool scanTruncated = false;
    for (const auto& entry : gSoundHistory)
    {
        if (scanned++ >= fs_world::MAX_SCAN) { scanTruncated = true; break; }
        const auto& item = entry.second;
        // Sound history is process-global. Never disclose history retained
        // from another login/region before this service observed its boundary.
        if ((item.mType != LLAudioEngine::AUDIO_TYPE_SFX && item.mType != LLAudioEngine::AUDIO_TYPE_AMBIENT) ||
            !epoch.containsSound(item.mTimeStarted, now) ||
            !inRange(item.mPosition, origin, radius)) continue;
        LLSD row = llsd::map("history_id", entry.first, "source_id", item.mSourceID,
            "owner_id", item.mOwnerID, "asset_id", item.mAssetID,
            "observed_global_position", ll_sd_from_vector3d(item.mPosition),
            "history_playing_flag", item.mPlaying, "looped", item.mIsLooped,
            "triggered", item.mIsTrigger, "age_seconds", std::max(0., now - item.mTimeStarted));
        row["position_state"] = "historical_observation";
        row["current_playback"] = "unknown";
        row["source_loaded"] = false;
        if (auto* source = gObjectList.findObject(item.mSourceID))
            row["source_loaded"] = !source->isDead() && source->getRegion();
        row["attribution"] = item.mSourceID.isNull() ? "unknown" : "observed_source_id";
        row["owner_is_emitter"] = "not_inferred";
        rows.add(entry.first.asString(), row);
    }
    appendRows(result, rows, scanTruncated);
    result["empty_does_not_prove_silence"] = true;
}
void media(LLSD& result, double radius, int limit)
{
    // URLs are deliberately omitted, including paths, userinfo, query strings
    // and hosts. Source association remains useful without leaking secrets.
    result["urls"] = "omitted";
    result["audibility"] = "unknown";
    const LLParcel* parcel = LLViewerParcelMgr::instance().getAgentParcel();
    const bool parcelKnown = currentParcel(parcel);
    result["parcel"]["status"] = parcelKnown ? "observed" : "unavailable";
    if (parcelKnown)
    {
        result["parcel"]["region_id"] = gAgent.getRegion()->getRegionID();
        result["parcel"]["parcel_local_id"] = parcel->getLocalID();
        result["parcel"]["music_configured"] = !parcel->getMusicURL().empty();
        result["parcel"]["media_configured"] = !parcel->getMediaURL().empty();
        result["parcel"]["provenance"] = "current_agent_parcel_configuration";
    }
    else result["parcel"]["reason"] = "current_region_parcel_not_loaded";
    auto* stream = gAudiop ? gAudiop->getStreamingAudioImpl() : nullptr;
    result["stream"]["status"] = stream ? "observed" : "unavailable";
    if (stream)
    {
        const auto url = stream->getURL();
        const auto playing = gAudiop->isInternetStreamPlaying();
        result["stream"]["state"] = playing == LLAudioEngine::AUDIO_PLAYING ? "playing" :
            playing == LLAudioEngine::AUDIO_PAUSED ? "paused" : "stopped";
        result["stream"]["source_configured"] = !url.empty();
        if (parcelKnown && !url.empty())
            result["stream"]["matches_agent_parcel_music"] = url == parcel->getMusicURL();
        else result["stream"]["parcel_music_comparison"] = "unknown";
        result["stream"]["provenance"] = "native_streaming_audio_state";
    }
    else result["stream"]["reason"] = "streaming_audio_unavailable";
    const LLVector3d origin = gAgent.getPositionGlobal();
    fs_world::Rows<LLSD> rows(limit);
    const S32 count = gObjectList.getNumObjects();
    int facesScanned = 0;
    bool scanTruncated = count > fs_world::MAX_SCAN;
    for (S32 i = 0; i < std::min(count, fs_world::MAX_SCAN); ++i)
    {
        auto* object = gObjectList.getObject(i);
        if (!observableObject(object, origin, radius, false)) continue;
        auto* volume = dynamic_cast<LLVOVolume*>(object);
        if (!volume) continue;
        for (U8 face = 0; face < object->getNumTEs(); ++face)
        {
            if (facesScanned++ >= fs_world::MAX_SCAN) { scanTruncated = true; break; }
            const auto* te = object->getTE(face);
            const auto* entry = te ? te->getMediaData() : nullptr;
            const auto impl = volume->getMediaImpl(face);
            if (!entry && !impl && !(te && te->hasMedia())) continue;
            LLSD row = llsd::map("object_id", object->getID(), "face", face,
                "region_id", object->getRegion()->getRegionID(), "provenance", "loaded_object_face_media");
            row["configuration_status"] = entry ? "observed" : "unavailable";
            if (entry) row["source_configured"] = !entry->getCurrentURL().empty() || !entry->getHomeURL().empty();
            row["instance_loaded"] = static_cast<bool>(impl);
            // Native isMediaPlaying also includes MEDIA_LOADING.
            row["playback"] = !impl ? "unknown" : impl->isMediaPlaying() ? "playing_or_loading" :
                impl->isMediaPaused() ? "paused" : "unknown";
            rows.add(object->getID().asString() + ":" + std::to_string(face), row);
        }
        if (facesScanned > fs_world::MAX_SCAN) break;
    }
    appendRows(result, rows, scanTruncated);
    result["empty_does_not_prove_no_media"] = true;
}
}

FSWorldListener::FSWorldListener() : LLEventAPI("FSWorld", "Bounded read-only loaded world observations")
{
    for (const char* op : {"location", "parcel", "avatars", "objects", "audio", "media"})
        add(op, "Read currently observable viewer state; requires world_read", &FSWorldListener::observe);
}
LLSD FSWorldListener::assistantLimits() const
{
    return llsd::map("version", 1, "max_rows", fs_world::MAX_ROWS, "max_scan", fs_world::MAX_SCAN,
        "max_radius", fs_world::MAX_RADIUS, "pagination", false, "media_urls", false,
        "property_fetch", false, "name_lookup", false);
}
bool FSWorldListener::prepareAssistant(FSAssistantPreparedOperation& operation, std::string& error) const
{
    double radius; int limit;
    if (!arguments(operation.request, radius, limit, error)) return false;
    operation.requiredClasses = {"world_read"};
    // Reads have no approval-material targets or pending callbacks. Their
    // actual identity/restrictions are captured again synchronously at dispatch.
    operation.summary = llsd::map("op", operation.request["op"], "subject_count", 0);
    return true;
}
bool FSWorldListener::validateAssistant(const FSAssistantPreparedOperation& operation, std::string& error) const
{
    double radius; int limit;
    return arguments(operation.request, radius, limit, error);
}
void FSWorldListener::observe(const LLSD& request)
{
    Response response(LLSD(), request);
    if (!fs_assistant_request_allowed(request))
        return response.error("Assistant request expired or its session/permission changed");
    fs_assistant::AsyncScope scope(fs_assistant_async_context(request));
    if (!fs_assistant::asyncAllowed()) return response.error("Assistant authority expired");
    const auto op = request["op"].asString();
    double radius; int limit; std::string error;
    if (!arguments(request, radius, limit, error)) return response.error(error);
    const auto source = identity();
    mEpoch.observe(source, LLTimer::getElapsedSeconds());
    LLSD result = base(request, mEpoch.generation(), limit);
    if (!ready()) unavailable(result, "logged_in_region_unavailable");
    else if (!visible(op))
    {
        result["status"] = "restricted";
        result["reason"] = "native_visibility_restriction";
    }
    else
    {
        if (op != "location" && op != "parcel") result["radius"] = radius;
        if (op == "location") location(result);
        else if (op == "parcel") parcel(result);
        else if (op == "avatars") avatars(result, radius, limit);
        else if (op == "objects") objects(result, radius, limit);
        else if (op == "audio") audio(result, radius, limit, mEpoch);
        else if (op == "media") media(result, radius, limit);
    }
    // No suspension, fetch, rendering, selection, media control or focus. Check
    // again at delivery so a future implementation cannot accidentally return
    // an old login's result or bypass newly imposed native restrictions.
    if (!fs_assistant_request_allowed(request) || !fs_assistant::asyncAllowed() || !(source == identity()))
        return response.error("World observation identity or authority changed");
    if (result["status"].asString() == "observed" && (!ready() || !visible(op)))
        return response.error("World observation became unavailable or restricted");
    response.setResponse(result);
}
void fs_initialize_world_api()
{
    static FSWorldListener listener;
}
