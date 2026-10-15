/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsgesturelistener.h"
#include "fsassistantgesturemodel.h"
#include "fscontentpermissions.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "fsassetblacklist.h"
#include "fsnearbychathub.h"
#include "llagent.h"
#include "llaudioengine.h"
#include "llassetstorage.h"
#include "llfilesystem.h"
#include "llgesturemgr.h"
#include "llmultigesture.h"
#include "llstartup.h"
#include "lltimer.h"
#include "llviewercontrol.h"
#include "llviewerregion.h"
#include "llviewermessage.h"
#include "llvoavatarself.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace {
using namespace fs_assistant_gesture;
using Response=LLEventAPI::Response;
FSGestureListener* listener=nullptr;
double now(){return LLTimer::getTotalSeconds();}
bool ready(){return !gDisconnected && LLStartUp::getStartupState()==STATE_STARTED &&
    gAgent.getID().notNull() && gAgent.getSessionID().notNull() && gAgent.getRegion();}
LLSD source(){auto result=fs_content::identity();result["region"]=gAgent.getRegion()?gAgent.getRegion()->getRegionID():LLUUID::null;return result;}
bool readable(const LLViewerInventoryItem* item)
{
    return ready() && item && item->isFinished() && !item->getIsLinkType() &&
        item->getType()==LLAssetType::AT_GESTURE && item->getAssetUUID().notNull() &&
        (fs_content::agentInventory(item->getUUID()) || fs_content::library(item->getUUID())) &&
        gAgent.allowOperation(PERM_COPY,item->getPermissions(),GP_OBJECT_MANIPULATE);
}
LLSD evidence(const LLViewerInventoryItem* item)
{
    auto result=fs_content::evidence(item);
    if(item)
    {result["name"]=item->getName();result["group_id"]=item->getPermissions().getGroup();
        result["base_permissions"]=item->getPermissions().getMaskBase();result["next_owner_permissions"]=item->getPermissions().getMaskNextOwner();
        result["item_flags"]=static_cast<S32>(item->getFlags());result["inventory_type"]=static_cast<S32>(item->getInventoryType());}
    return result;
}
bool nativeAllowed(const Plan& plan,std::string& error)
{
    if(!ready() || !isAgentAvatarValid()){error="native_avatar_unavailable";return false;}
    if(!FSSessionWorker::assistantActiveAllowed()){error="requires_active_character";return false;}
    if(!gSavedPerAccountSettings.getBOOL("FSGesturesEnabled") || !RlvActions::canPlayGestures())
    {error="native_gestures_disabled_or_restricted";return false;}
    if(plan.chat && (gRlvHandler.hasBehaviour(RLV_BHVR_SENDCHAT) || gRlvHandler.hasBehaviour(RLV_BHVR_REDIRCHAT) ||
        gRlvHandler.hasBehaviour(RLV_BHVR_REDIREMOTE) || gRlvHandler.hasBehaviour(RLV_BHVR_RECVCHAT) ||
        gRlvHandler.hasBehaviour(RLV_BHVR_RECVEMOTE)))
    {error="nearby_chat_restricted";return false;}
    if(plan.sound && (!gAudiop || gAudiop->getMuted() || gSavedSettings.getBOOL("MuteAudio") ||
        gSavedSettings.getBOOL("MuteSounds") || FSSessionWorker::backgroundAudioMuted()))
    {error="native_sound_muted_or_unavailable";return false;}
    for(const auto& step:plan.steps)
    {
        if(step.effect==Effect::Chat && wstring_to_utf8str(utf8str_to_wstring(step.value))!=step.value)
        {error="invalid_chat_utf8";return false;}
        if((step.effect==Effect::AnimationStart || step.effect==Effect::Sound) &&
            FSAssetBlacklist::instance().isBlacklisted(LLUUID(step.value),step.effect==Effect::Sound?LLAssetType::AT_SOUND:LLAssetType::AT_ANIMATION))
        {error="native_asset_blacklisted";return false;}
    }
    return true;
}
bool foreign(const LLUUID& id)
{
    if(!isAgentAvatarValid())return true;
    for(const auto& entry:gAgentAvatarp->mAnimationSources)
        if(entry.second==id && entry.first!=gAgent.getID())return true;
    return false;
}
const char* effectName(Effect effect)
{
    switch(effect){case Effect::AnimationStart:return "animation_start";case Effect::AnimationStop:return "animation_stop";
        case Effect::Sound:return "sound_trigger";case Effect::Chat:return "nearby_chat_literal";
        case Effect::WaitTime:return "wait_time";case Effect::WaitAnimations:return "wait_animations";}
    return "unsupported";
}
LLSD effects(const Plan& plan)
{
    LLSD rows=LLSD::emptyArray();
    for(const auto& step:plan.steps)
    {LLSD row;row["effect"]=effectName(step.effect);
        if(step.effect==Effect::Chat)row["text"]=step.value;
        else if(!step.value.empty())row["asset_id"]=LLUUID(step.value);
        if(step.effect==Effect::WaitTime || step.effect==Effect::WaitAnimations)row["maximum_seconds"]=step.seconds;
        rows.append(row);}
    return rows;
}
bool arguments(const LLSD& request,std::string& error)
{
    const auto op=request["op"].asString();
    if(op=="list")
    {if(request.has("limit") && (!request["limit"].isInteger() || request["limit"].asInteger()<1 || request["limit"].asInteger()>100)){error="invalid_args: limit 1..100";return false;}return true;}
    if(op!="status" && op!="play" && op!="stop"){error="unsupported_operation";return false;}
    if(!uuid(request["item_id"].asString())){error="invalid_args: exact gesture item_id required";return false;}
    if(op=="play" && (!uuid(request["asset_id"].asString()) || request["asset_hash"].asString().size()!=64 ||
        request["client_request_id"].asString().empty() || request["client_request_id"].asString().size()>128))
    {error="invalid_args: inspected asset_id, SHA256 asset_hash and unique client_request_id required";return false;}
    if(op=="stop" && !uuid(request["playback_id"].asString())){error="invalid_args: exact assistant playback_id required";return false;}
    return true;
}
}

struct FSGestureListener::State : std::enable_shared_from_this<FSGestureListener::State> {
    struct Cached {LLSD evidence;Plan plan;std::string hash;double until=0;};
    struct Playback {
        LLUUID id,item;std::string client;LLSD identity;Cached frozen;fs_assistant::AsyncTaskPtr task;
        Timeline timeline;std::map<LLUUID,std::uint64_t> animations;std::set<LLUUID> signaled;
        bool incomplete=false;std::string failure;unsigned sounds=0,chats=0,submitted=0;
        Playback(const Cached& material,const LLUUID& itemID,const std::string& clientID):
            id(LLUUID::generateNewID()),item(itemID),client(clientID),identity(source()),frozen(material),
            task(fs_assistant::asyncTask()),timeline(material.plan.steps,now()){}
    };
    struct Load {std::weak_ptr<State> owner;LLSD request,itemEvidence,identity;LLUUID item,asset;
        fs_assistant::AsyncTaskPtr task;double until=0;std::shared_ptr<Response> response;};
    LLSD identity;std::map<LLUUID,Cached> cache;std::unique_ptr<Playback> playing;AnimationLeases leases;
    std::map<std::string,LLSD> receipts;std::deque<std::string> receiptOrder;
    static std::map<std::uintptr_t,std::shared_ptr<Load>>& loads(){static std::map<std::uintptr_t,std::shared_ptr<Load>> value;return value;}
    static std::uintptr_t token(){static std::uintptr_t value=0;return ++value;}
    void sync()
    {
        if(identity!=source())
        {
            auto previous=identity,current=source();previous.erase("region");current.erase("region");
            const bool newLogin=previous!=current;
            finish("login_or_region_changed");identity=source();cache.clear();leases.clear();
            if(newLogin){receipts.clear();receiptOrder.clear();}
        }
        for(auto it=cache.begin();it!=cache.end();)if(now()>it->second.until)it=cache.erase(it);else ++it;
    }
    LLSD row(const LLUUID& id)
    {
        LLSD result;auto* item=gInventory.getItem(id);result["item_id"]=id;
        if(!readable(item)){result["status"]="unavailable";result["reason"]="exact_native_copyable_gesture_required";return result;}
        result["asset_id"]=item->getAssetUUID();result["name"]=utf8str_truncate(item->getName(),255);
        result["active"]=LLGestureMgr::instance().isGestureActive(id);result["native_playing"]=LLGestureMgr::instance().isGesturePlaying(id);
        const auto found=cache.find(id);
        if(found==cache.end() || found->second.evidence!=evidence(item))result["status"]="not_inspected";
        else {result["status"]="inspected";result["asset_hash"]=found->second.hash;result["hash_algorithm"]="sha256_raw_asset";
            result["effects"]=effects(found->second.plan);result["trigger"]=found->second.plan.trigger;
            result["chat_requires_permission"]="chat_send_nearby";std::string reason;result["playable_now"]=nativeAllowed(found->second.plan,reason);if(!reason.empty())result["play_refusal"]=reason;}
        result["execution"]="simulator_visible_guarded_timeline";result["sound_delivery"]="submitted_only_not_audibility";
        if(playing && playing->item==id)result["assistant_playback"]=receipt(*playing,"pending");
        else for(auto it=receiptOrder.rbegin();it!=receiptOrder.rend();++it)
        {const auto& previous=receipts.at(*it);if(previous["item_id"].asUUID()==id){result["assistant_playback"]=previous;break;}}
        return result;
    }
    LLSD receipt(const Playback& play,const char* state)
    {
        LLSD result;result["state"]=state;result["playback_id"]=play.id;result["item_id"]=play.item;
        result["asset_id"]=play.frozen.evidence["asset_id"];result["asset_hash"]=play.frozen.hash;
        result["effects"]=effects(play.frozen.plan);result["step_index"]=static_cast<S32>(play.timeline.index());
        result["submitted_effects"]=static_cast<S32>(play.submitted);result["sound_triggers_submitted"]=static_cast<S32>(play.sounds);
        result["chat_messages_submitted"]=static_cast<S32>(play.chats);result["cleanup_complete"]=!play.incomplete;
        result["cleanup_scope"]="assistant_owned_animation_contributions_only";
        result["submitted_sounds_cannot_be_recalled"]=play.sounds>0;result["submitted_chat_cannot_be_recalled"]=play.chats>0;
        result["sound_delivery"]="submitted_only_not_audibility";result["delivery"]="not_confirmed";
        result["authority_duration"]="bridge_request_deadline_may_cancel_before_120_seconds";
        if(!play.failure.empty())result["reason"]=play.failure;
        return result;
    }
    void remember(const std::string& client,LLSD result)
    {
        if(!receipts.count(client))receiptOrder.push_back(client);
        receipts[client]=std::move(result);
    }
    bool owned(Playback& play,const LLUUID& id,std::uint64_t lease)
    {return cleanupAllowed(ready() && play.identity==source(),leases.owns(id.asString(),lease),foreign(id));}
    void finish(const std::string& reason)
    {
        if(!playing)return;
        auto& play=*playing;play.failure=reason;
        // Cleanup is authority-independent but remains pinned to the original
        // login/region and exact unsuperseded animation contribution.
        for(const auto& entry:play.animations)
        {
            if(owned(play,entry.first,entry.second))gAgent.sendAnimationRequest(entry.first,ANIM_REQUEST_STOP);
            else play.incomplete=true;
            leases.release(entry.first.asString());
        }
        const auto state=reason=="completed"?"submitted":play.submitted?"partially_submitted":"cancelled_before_submission";
        remember(play.client,receipt(play,state));playing.reset();
    }
    bool pendingAnimations(Playback& play)
    {
        for(auto it=play.animations.begin();it!=play.animations.end();)
        {
            if(!owned(play,it->first,it->second)){play.incomplete=true;play.failure="animation_ownership_superseded";play.timeline.stop(play.failure);return true;}
            if(gAgentAvatarp->mSignaledAnimations.count(it->first)){play.signaled.insert(it->first);++it;}
            else if(play.signaled.count(it->first)){leases.release(it->first.asString());it=play.animations.erase(it);}
            else ++it;
        }
        return !play.animations.empty();
    }
    bool leasesIntact(Playback& play)
    {
        for(const auto& entry:play.animations)if(!owned(play,entry.first,entry.second))
        {play.incomplete=true;play.failure="animation_ownership_superseded";return false;}
        return true;
    }
    bool apply(Playback& play,const Step& step)
    {
        std::string error;
        if(!fs_assistant::asyncTaskAllowed(play.task) || play.identity!=source() || !nativeAllowed(play.frozen.plan,error))
        {play.failure=error.empty()?"authority_or_source_changed":error;return false;}
        const LLUUID id((step.effect==Effect::AnimationStart || step.effect==Effect::AnimationStop || step.effect==Effect::Sound)?step.value:std::string());
        const bool start=step.effect==Effect::AnimationStart;
        if(start || step.effect==Effect::AnimationStop)
        {
            const auto current=play.animations.find(id);
            if(current!=play.animations.end() && !owned(play,id,current->second))
            {play.incomplete=true;play.failure="animation_ownership_superseded";return false;}
            if(start && current==play.animations.end() &&
                (foreign(id) || gAgentAvatarp->mSignaledAnimations.count(id) || gAgentAvatarp->mPlayingAnimations.count(id)))
            {play.failure="animation_already_owned_by_native_or_foreign_source";return false;}
            if(!start && current==play.animations.end()){play.failure="animation_stop_not_owned";return false;}
            if(!fs_assistant::asyncTaskSubmit(play.task))return false;
            leases.acquire(id.asString());gAgent.sendAnimationRequest(id,start?ANIM_REQUEST_START:ANIM_REQUEST_STOP);
            if(start)play.animations[id]=leases.current(id.asString());
            else {play.animations.erase(id);leases.release(id.asString());}
        }
        else if(step.effect==Effect::Sound)
        {
            if(!fs_assistant::asyncTaskSubmit(play.task))return false;
            send_sound_trigger(id,1.f);++play.sounds;
        }
        else if(step.effect==Effect::Chat)
        {
            if(!fs_assistant::asyncTaskSubmit(play.task))return false;
            const auto text=utf8str_to_wstring(step.value);
            // Audited literal path: no commands, gesture triggers, volume or
            // channel parsing. The native sender still enforces RLV routing.
            FSNearbyChat::sendChatFromViewer(text,text,CHAT_TYPE_NORMAL,false,0);++play.chats;
        }
        else return false;
        ++play.submitted;return true;
    }
    void tick()
    {
        sync();
        for(auto it=loads().begin();it!=loads().end();)
        {
            const auto load=it->second;
            if(load->owner.lock().get()!=this){++it;continue;}
            if(now()>=load->until || !fs_assistant::asyncTaskAllowed(load->task))
            {load->response->error("asset_load_expired_or_authority_revoked");it=loads().erase(it);}else ++it;
        }
        if(!playing)return;
        auto& play=*playing;fs_assistant::AsyncScope scope(play.task->guard());
        play.timeline.tick(now(),[&](){std::string error;return fs_assistant::asyncTaskAllowed(play.task) &&
            play.identity==source() && nativeAllowed(play.frozen.plan,error) && leasesIntact(play);},
            [&](){return pendingAnimations(play);},[&](const Step& step){return apply(play,step);});
        if(play.timeline.done())finish(play.failure.empty()?play.timeline.reason():play.failure);
    }
    static void loaded(const LLUUID& asset,LLAssetType::EType type,void* userdata,S32 status,LLExtStat)
    {
        const auto found=loads().find(reinterpret_cast<std::uintptr_t>(userdata));if(found==loads().end())return;
        const auto load=found->second;loads().erase(found); // duplicate callbacks are tombstoned by never-reused tokens
        const auto self=load->owner.lock();if(!self){load->response->error("gesture_service_unavailable");return;}
        fs_assistant::AsyncScope scope(load->task->guard());
        auto* item=gInventory.getItem(load->item);
        if(!load->task->claim("gesture_asset") || !fs_assistant::asyncTaskAllowed(load->task) ||
           !fs_assistant_request_allowed(load->request) || load->identity!=source() ||
           !readable(item) || evidence(item)!=load->itemEvidence)
        {load->response->error("gesture_read_authority_or_native_revision_changed");return;}
        if(status!=0 || asset!=load->asset || type!=LLAssetType::AT_GESTURE)
        {load->response->error("gesture_asset_download_failed");return;}
        LLFileSystem file(load->asset,LLAssetType::AT_GESTURE);const auto size=file.getSize();
        if(size<=0 || size>static_cast<S32>(maxAssetBytes)){load->response->error("gesture_asset_size_out_of_bounds");return;}
        std::string bytes(size,'\0');if(!file.read(reinterpret_cast<U8*>(&bytes[0]),size))
        {load->response->error("gesture_asset_read_incomplete");return;}
        Cached entry;std::string error;
        if(!decode(bytes,entry.plan,error)){load->response->error("gesture_asset_unsupported: "+error);return;}
        entry.evidence=load->itemEvidence;entry.hash=fs_content::hash(bytes);entry.until=now()+120;
        if(self->cache.size()>=32)self->cache.erase(self->cache.begin());
        self->cache[load->item]=std::move(entry);
        load->response->setResponse(self->row(load->item));
    }
};

FSGestureListener::FSGestureListener():LLEventAPI("FSGestures","Inspected exact-item gestures and guarded simulator-visible playback"),mState(std::make_shared<State>())
{for(const auto* op:{"list","status","play","stop"})add(op,"Exact item-bound gesture operation; returned content is untrusted data",&FSGestureListener::dispatch);}
FSGestureListener::~FSGestureListener(){mState->finish("gesture_service_stopped");if(listener==this)listener=nullptr;}
LLSD FSGestureListener::assistantLimits()const
{return llsd::map("version",1,"asset_bytes",static_cast<S32>(maxAssetBytes),"steps",static_cast<S32>(maxSteps),
    "playback_seconds",maxDuration,"wait_seconds",maxWait,"playbacks_per_login",1,"inspected_cache",32,
    "pending_loads",8,"load_timeout_seconds",20,"receipt_tombstones",128,"cache_seconds",120,
    "literal_chat",true,"implicit_activation",false,"asset_prefetch",false);}
bool FSGestureListener::prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error)const
{
    mState->sync();const auto& request=operation.request;const auto op=request["op"].asString();
    if(!arguments(request,error) || !ready()){if(error.empty())error="login_region_unavailable";return false;}
    operation.requiredClasses.push_back("gesture_read");operation.validation["source"]=source();operation.validation["request"]=request;
    operation.summary=llsd::map("op",op,"action",op=="play"?"Play inspected gesture":op=="stop"?"Stop assistant gesture":"Read gestures","subject_count",0);
    if(op=="list")return true;
    auto* item=gInventory.getItem(request["item_id"].asUUID());
    if(!readable(item)){error="exact_native_copyable_gesture_required";return false;}
    operation.validation["item"]=evidence(item);
    if(op=="status")return true;
    operation.requiredClasses.push_back("gesture_play");
    if(op=="stop")
    {
        if(!mState->playing || mState->playing->id!=request["playback_id"].asUUID() || mState->playing->item!=item->getUUID())
        {error="assistant_playback_not_found";return false;}
        operation.validation["playback_id"]=mState->playing->id;
        LLSD subject;subject["name"]=item->getName();subject["id"]=item->getUUID();
        subject["after_name"]="Stop assistant playback "+mState->playing->id.asString()+"; cancel future effects and release only its still-owned animations";
        operation.summary["subjects"]=LLSD::emptyArray();operation.summary["subjects"].append(subject);operation.summary["subject_count"]=1;return true;
    }
    const auto found=mState->cache.find(item->getUUID());
    if(found==mState->cache.end() || found->second.evidence!=evidence(item))
    {error="gesture_not_inspected: call gesture_status for this exact item before play";return false;}
    const auto& frozen=found->second;
    if(request["asset_id"].asUUID()!=item->getAssetUUID() || request["asset_hash"].asString()!=frozen.hash)
    {error="gesture_material_conflict: inspect the current asset and explicitly play again";return false;}
    if(!nativeAllowed(frozen.plan,error))return false;
    if(frozen.plan.chat)operation.requiredClasses.push_back("chat_send_nearby");
    operation.validation["asset_hash"]=frozen.hash;operation.validation["effects"]=effects(frozen.plan);
    operation.summary["subjects"]=LLSD::emptyArray();LLSD subject;
    subject["name"]=item->getName();subject["id"]=item->getUUID();subject["after_name"]="Asset "+item->getAssetUUID().asString()+" SHA256 "+frozen.hash;
    operation.summary["subjects"].append(subject);
    for(const auto& step:frozen.plan.steps)
    {LLSD row;row["name"]=effectName(step.effect);row["after_name"]=step.value.empty()?std::to_string(step.seconds)+" seconds":step.value;operation.summary["subjects"].append(row);}
    operation.summary["subject_count"]=operation.summary["subjects"].size();operation.summary["effects"]=effects(frozen.plan);
    return true;
}
bool FSGestureListener::validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error)const
{
    const auto& request=operation.request;const auto op=request["op"].asString();
    if(operation.validation["source"]!=source() || operation.validation["request"]!=request || !ready())
    {error="gesture_source_or_material_changed";return false;}
    if(op=="list")return true;
    auto* item=gInventory.getItem(request["item_id"].asUUID());
    if(!readable(item) || operation.validation["item"]!=evidence(item)){error="gesture_native_revision_or_permission_changed";return false;}
    if(op=="status")return true;
    if(op=="stop")return true; // dispatch compares the exact playback ID again; completed work is harmless
    const auto found=mState->cache.find(item->getUUID());
    const auto* frozen=mState->playing && mState->playing->client==request["client_request_id"].asString()?
        &mState->playing->frozen:found!=mState->cache.end()?&found->second:nullptr;
    if(!frozen || frozen->hash!=operation.validation["asset_hash"].asString() ||
       effects(frozen->plan)!=operation.validation["effects"])
    {error="gesture_frozen_material_expired";return false;}
    return nativeAllowed(frozen->plan,error);
}
void FSGestureListener::dispatch(const LLSD& request)
{
    auto response=std::make_shared<Response>(LLSD(),request);
    if(!fs_assistant_request_allowed(request)){response->error("expired_or_not_permitted");return;}
    const auto guard=fs_assistant_async_context(request);fs_assistant::AsyncScope scope(guard);
    if(!guard || !guard->allowed()){response->error("missing_or_expired_authority");return;}
    mState->sync();std::string error;if(!arguments(request,error)){response->error(error);return;}
    const auto op=request["op"].asString();
    if(op=="list")
    {
        LLSD result;result["entries"]=LLSD::emptyArray();int limit=request.has("limit")?request["limit"].asInteger():50;
        const auto& active=LLGestureMgr::instance().getActiveGestures();std::set<LLUUID> ids;
        for(const auto& entry:active){if(ids.size()>=101)break;ids.insert(entry.first);}
        for(const auto& entry:mState->cache){if(ids.size()>=101)break;ids.insert(entry.first);}
        for(const auto& id:ids){if(result["entries"].size()>=limit)break;if(readable(gInventory.getItem(id)))result["entries"].append(mState->row(id));}
        result["truncated"]=active.size()>static_cast<std::size_t>(limit) || ids.size()>static_cast<std::size_t>(limit);
        result["coverage"]="native_active_and_assistant_inspected_items";result["complete"]=false;
        result["cold_inspection"]="gesture_status_exact_item";response->setResponse(result);return;
    }
    const LLUUID id=request["item_id"].asUUID();auto* item=gInventory.getItem(id);
    if(!readable(item)){response->error("exact_native_copyable_gesture_required");return;}
    if(op=="status")
    {
        auto result=mState->row(id);if(result["status"].asString()=="inspected"){response->setResponse(result);return;}
        if(!gAssetStorage || State::loads().size()>=8){response->error("gesture_asset_loader_unavailable_or_busy");return;}
        auto load=std::make_shared<State::Load>();load->owner=mState;load->request=request;load->itemEvidence=evidence(item);
        load->identity=source();load->item=id;load->asset=item->getAssetUUID();load->task=fs_assistant::asyncTask();load->until=now()+20;load->response=response;
        const auto token=State::token();State::loads()[token]=load;
        // Item-bound permission-bearing load; never an arbitrary asset API.
        gAssetStorage->getInvItemAsset(LLHost(),gAgent.getID(),gAgent.getSessionID(),item->getPermissions().getOwner(),
            LLUUID::null,id,item->getAssetUUID(),LLAssetType::AT_GESTURE,State::loaded,reinterpret_cast<void*>(token),true);return;
    }
    if(op=="stop")
    {
        if(!mState->playing || mState->playing->id!=request["playback_id"].asUUID() || mState->playing->item!=id)
        {response->error("assistant_playback_not_found");return;}
        if(!fs_assistant::asyncSubmit()){response->error("expired_or_not_permitted");return;}
        const auto client=mState->playing->client;mState->finish("explicit_stop");response->setResponse(mState->receipts.at(client));return;
    }
    const auto client=request["client_request_id"].asString();const auto previous=mState->receipts.find(client);
    if(previous!=mState->receipts.end())
    {
        if(previous->second["item_id"].asUUID()!=id || previous->second["asset_id"].asUUID()!=request["asset_id"].asUUID() || previous->second["asset_hash"]!=request["asset_hash"])
        {response->error("client_request_id_material_conflict");return;}
        response->setResponse(mState->playing && mState->playing->client==client?mState->receipt(*mState->playing,"pending"):previous->second);return;
    }
    if(mState->playing){response->error("gesture_playback_busy: explicitly stop the exact assistant playback first");return;}
    // Never evict an uncertain outcome and later turn a retry into a new play.
    // The bounded tombstones last for this login/region; exhaustion fails closed.
    if(mState->receipts.size()>=128){response->error("gesture_request_tombstone_capacity: reconnect before further playback");return;}
    const auto found=mState->cache.find(id);
    if(found==mState->cache.end() || found->second.evidence!=evidence(item) || found->second.hash!=request["asset_hash"].asString() ||
       item->getAssetUUID()!=request["asset_id"].asUUID() || !nativeAllowed(found->second.plan,error))
    {response->error(error.empty()?"gesture_material_changed_or_not_inspected":error);return;}
    mState->playing=std::make_unique<State::Playback>(found->second,id,client);
    mState->remember(client,mState->receipt(*mState->playing,"pending"));mState->tick();
    response->setResponse(mState->playing?mState->receipt(*mState->playing,"pending"):mState->receipts.at(client));
}
void FSGestureListener::tick(){mState->tick();}
void FSGestureListener::animationRequest(const LLUUID& id){mState->leases.observed(id.asString());}
void FSGestureListener::animationReset(){mState->leases.invalidate();}
void fs_initialize_gesture_api(){if(!listener)listener=new FSGestureListener();}
void fs_assistant_gestures_tick(){if(listener)listener->tick();}
void fs_assistant_animation_request(const LLUUID& id){if(listener)listener->animationRequest(id);}
void fs_assistant_animation_reset(){if(listener)listener->animationReset();}
