/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsnavigationlistener.h"
#include "fsnavigationmodel.h"
#include "fsassistantworldmodel.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "llagent.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llstartup.h"
#include "lltimer.h"
#include "llviewernetwork.h"
#include "llviewerregion.h"
#include "llvoavatarself.h"
#include "rlvactions.h"
#include "rlvhandler.h"
namespace
{
double now() { return static_cast<double>(LLTimer::getTotalSeconds()); }
bool ready() { return LLStartUp::getStartupState()==STATE_STARTED && !gDisconnected && !LLApp::isExiting() && gAgent.getRegion() && isAgentAvatarValid(); }
std::string source()
{
    if(!ready()) return {};
    return gAgent.getID().asString()+":"+gAgent.getSessionID().asString()+":"+LLGridManager::instance().getGrid()+":"+FSEventAPIBridge::instance().sessionReference()+":"+ll_stream_notation_sd(FSSessionWorker::assistantIdentity());
}
struct Service
{
    fs_assistant_world::Tickets<LLSD> prepared;
    std::string source,preparedSource,token,region,state="idle",reason;
    LLVector3d destination;
    double deadline=0.;
    bool flying=false,teleport=false;
    fs_assistant::AsyncTaskPtr task;
};
Service& service() { static Service value; return value; }
bool ownsPilot() { const auto& self=service(); return fs_navigation::owns(self.token,gAgent.getAutoPilot(),gAgent.getAutoPilotBehaviorName()); }
void finish(const std::string& state,const std::string& reason,bool stop)
{
    auto& self=service(); const auto task=self.task;
    // Revoke our task before touching native state; callbacks cannot resume it.
    self.state=state; self.reason=reason; self.task.reset();
    if(stop && !self.teleport && ownsPilot()) gAgent.stopAutoPilot(true);
    if(stop && self.teleport && task) gAgent.cancelAssistantTeleport(task->guard());
    if(task && task->guard())
    { if(state=="arrived") task->guard()->acknowledge(self.token); else task->guard()->cancel(reason); }
}
LLSD material(const LLSD& request)
{
    LLSD value=LLSD::emptyMap();
    for(const char* key:{"op","position","mode","stop_distance","timeout_seconds","flying","operation_id"}) if(request.has(key)) value[key]=request[key];
    return value;
}
LLSD baseline()
{
    return llsd::map("position",ll_sd_from_vector3d(gAgent.getPositionGlobal()),"region",gAgent.getRegion()->getRegionID(),"flying",gAgent.getFlying(),"pilot",gAgent.getAutoPilot(),"pilot_behavior",gAgent.getAutoPilotBehaviorName(),"teleport_state",static_cast<S32>(gAgent.getTeleportState()),"operation_id",service().token);
}
LLSD approvalBaseline(const LLSD& request)
{
    // Stopping is tied to ownership, not the avatar's changing position.
    if(request["op"].asString()=="stop")
    {
        const auto& self=service();
        return llsd::map("operation_id",self.token,"teleport",self.teleport,"owned",self.task && (self.teleport?gAgent.hasAssistantTeleport(self.task->guard()):ownsPilot()));
    }
    return baseline();
}
bool arguments(const LLSD& request,std::string& error)
{
    const auto op=request["op"].asString();
    if(!fs_assistant::featureOperation("FSNavigation",op)) { error="invalid_args: unknown navigation operation"; return false; }
    if(op=="moveTo" || op=="teleport")
    {
        if(!request["position"].isArray() || request["position"].size()!=3) { error="invalid_args: global position [x,y,z] required"; return false; }
        for(const auto& component:llsd::inArray(request["position"]))
            if((!component.isReal() && !component.isInteger()) || !std::isfinite(component.asReal()) || component.asReal()<0. || component.asReal()>4294967295.) { error="invalid_args: nonnegative finite global coordinates required"; return false; }
        if(request["position"][2].asReal()>4096.) { error="invalid_args: destination height must be 0..4096m"; return false; }
    }
    if(op=="moveTo")
    {
        if(request["mode"].asString()!="walk" && request["mode"].asString()!="fly") { error="invalid_args: explicit walk/fly mode required"; return false; }
        const auto& distance=request["stop_distance"]; const auto& seconds=request["timeout_seconds"];
        if((!distance.isReal() && !distance.isInteger()) || !std::isfinite(distance.asReal()) || !seconds.isInteger() || !fs_navigation::bounds(0.,distance.asReal(),seconds.asInteger())) { error="invalid_args: stop_distance .25..4 and timeout_seconds 1..30 required"; return false; }
    }
    if(op=="flyingSet" && !request["flying"].isBoolean()) { error="invalid_args: explicit flying boolean required"; return false; }
    if(op=="stop" && (!request["operation_id"].isString() || request["operation_id"].asString().empty() || request["operation_id"].asString().size()>64)) { error="invalid_args: exact operation_id required"; return false; }
    return true;
}
bool native(const LLSD& request,std::string& error)
{
    if(!ready()) { error="not_ready: ready avatar and exact login required"; return false; }
    if(!RlvActions::canShowLocation()) { error="restricted: location hidden"; return false; }
    const auto op=request["op"].asString();
    if(op=="status") return true;
    if(!FSSessionWorker::assistantActiveAllowed(op=="stop" && service().teleport)) { error="requires_active_character: navigation requires active ownership"; return false; }
    if(op=="stop")
    {
        const auto& self=service();
        if(self.source!=source() || self.token!=request["operation_id"].asString() || !self.task || (self.teleport?!gAgent.hasAssistantTeleport(self.task->guard()):!ownsPilot())) { error="conflict: no matching assistant-owned movement"; return false; }
        return true;
    }
    if(service().task || gAgent.getAutoPilot() || gAgent.getTeleportState()!=LLAgent::TELEPORT_NONE || gAgent.hasTeleportRequest()) { error="busy: finish the existing movement or teleport first"; return false; }
    if(gAgentAvatarp->isSitting() || !RlvActions::canStand()) { error="restricted: stand manually before navigation"; return false; }
    if(op=="moveTo")
    {
        const auto dest=ll_vector3d_from_sd(request["position"]); const auto local=gAgent.getRegion()->getPosRegionFromGlobal(dest);
        if(!fs_navigation::bounds(dist_vec(dest,gAgent.getPositionGlobal()),request["stop_distance"].asReal(),request["timeout_seconds"].asInteger()) || local.mV[0]<0.f || local.mV[1]<0.f || local.mV[0]>=gAgent.getRegion()->getWidth() || local.mV[1]>=gAgent.getRegion()->getWidth()) { error="invalid_args: movement must stay within this region and 64m"; return false; }
        if(request["mode"].asString()=="walk" && gAgent.getFlying()) { error="conflict: land before requesting a walk"; return false; }
    }
    const bool flight=(op=="moveTo" && request["mode"].asString()=="fly") || (op=="flyingSet" && request["flying"].asBoolean());
    if(flight && (!gAgent.canFly() || !RlvActions::canFly())) { error="restricted: native flight is unavailable"; return false; }
    if(op=="teleport")
    {
        const auto dest=ll_vector3d_from_sd(request["position"]);
        if(RlvActions::isLocalTp(dest)?!RlvActions::canTeleportToLocal(dest):!RlvActions::canTeleportToLocation()) { error="restricted: native teleport restriction"; return false; }
    }
    return true;
}
LLSD status()
{
    const auto& self=service(); LLSD result=llsd::map("navigation_tools",1,"viewer_session",FSEventAPIBridge::instance().sessionReference(),"account_id",gAgent.getID(),"grid",LLGridManager::instance().getGrid(),"position",ll_sd_from_vector3d(gAgent.getPositionGlobal()),"flying",gAgent.getFlying(),"native_autopilot",gAgent.getAutoPilot(),"native_teleport_state",static_cast<S32>(gAgent.getTeleportState()));
    if(self.source==source())
    { result["operation_id"]=self.token; result["state"]=self.state; result["reason"]=self.reason; result["assistant_owned"]=static_cast<bool>(self.task); if(self.task) { result["destination"]=ll_sd_from_vector3d(self.destination); if(!self.teleport && ownsPilot()) result["native_target"]=ll_sd_from_vector3d(gAgent.getAutoPilotTargetGlobal()); } }
    else { result["state"]="idle"; result["assistant_owned"]=false; }
    return result;
}
}
void fs_navigation_tick()
{
    auto& self=service();
    if(!self.task)
    {
        if(!self.source.empty() && self.source!=source()) { self.prepared.clear(); self.token.clear(); self.state="idle"; self.reason.clear(); self.source=source(); }
        return;
    }
    if(!ready() || self.source!=source()) { finish("cancelled","source_changed",true); return; }
    const auto region=gAgent.getRegion()->getRegionID().asString();
    if(self.teleport)
    {
        if(!gAgent.hasAssistantTeleport(self.task->guard()))
        {
            // A new native request owns the agent, or the native request ended.
            // Arrival is observed only near the requested destination.
            const auto& acknowledged=self.task->guard()->ids();
            const bool arrived=std::find(acknowledged.begin(),acknowledged.end(),"native_teleport_finished")!=acknowledged.end() && dist_vec(gAgent.getPositionGlobal(),self.destination)<5.;
            finish(arrived?"arrived":"ended",arrived?"destination_observed":"native_teleport_ended_or_replaced",false); return;
        }
        if(!fs_assistant::asyncTaskAllowed(self.task) || now()>=self.deadline) { finish("cancelled","teleport_authority_or_timeout",true); return; }
        return;
    }
    if(!ownsPilot()) { finish("ended","native_movement_ended_or_replaced",false); return; }
    const bool restricted=RlvActions::canShowLocation() && RlvActions::canStand() && !gAgentAvatarp->isSitting() && self.flying==gAgent.getFlying() && (!self.flying || (RlvActions::canFly() && gAgent.canFly()));
    if(!fs_navigation::canContinue(fs_assistant::asyncTaskAllowed(self.task),self.region==region,FSSessionWorker::assistantActiveAllowed(),restricted,now(),self.deadline)) finish("cancelled","movement_authority_region_restriction_or_timeout",true);
}
FSNavigationListener::FSNavigationListener():LLEventAPI("FSNavigation","Owned bounded navigation for an exact viewer login")
{
    gIdleCallbacks.addFunction([](void*){fs_navigation_tick();});
    for(const char* op:{"status","moveTo","flyingSet","teleport","stop"}) add(op,"Exact session, native restrictions and explicit movement ownership",&FSNavigationListener::dispatch);
}
LLSD FSNavigationListener::assistantLimits()const
{ return llsd::map("version",1,"max_distance",64,"max_move_seconds",30,"global_coordinates",true,"arrival_guaranteed",false,"automatic_retry",false); }
bool FSNavigationListener::prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error)const
{
    fs_navigation_tick();
    auto& current=service(); if(current.preparedSource!=source()) { current.prepared.clear(); current.preparedSource=source(); }
    if(!arguments(operation.request,error) || !native(operation.request,error)) return false;
    const auto op=operation.request["op"].asString(); operation.requiredClasses={"world_read"};
    if(op!="status" && op!="stop") operation.requiredClasses.emplace_back(op=="teleport"?"navigation_teleport":"navigation_move");
    const auto nonce=LLUUID::generateNewID().asString(); service().prepared.put(nonce,source(),now(),llsd::map("material",material(operation.request),"baseline",approvalBaseline(operation.request)));
    if(!service().prepared.get(nonce,source(),now())) { error="busy: too many live navigation requests"; return false; }
    operation.validation["navigation_prepared"]=nonce; operation.request["__navigation_prepared"]=nonce;
    operation.summary=llsd::map("op",op,"action",op=="teleport"?"Teleport your avatar":op=="stop"?"Stop assistant-owned navigation":"Change avatar movement","subject_count",1,"target",gAgent.getID().asString(),"value",ll_stream_notation_sd(material(operation.request)),"source",ll_stream_notation_sd(baseline()));
    return true;
}
bool FSNavigationListener::validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error)const
{
    auto* entry=service().prepared.get(operation.validation["navigation_prepared"].asString(),source(),now());
    if(!entry || !llsd_equals(entry->value["material"],material(operation.request)) || !ready()) { error="session_changed: navigation source expired or changed"; return false; }
    if(entry->executed)
    {
        if(operation.request["op"].asString()=="teleport")
        {
            const auto dest=ll_vector3d_from_sd(operation.request["position"]);
            if(!RlvActions::canShowLocation() || !RlvActions::canStand() || (RlvActions::isLocalTp(dest)?!RlvActions::canTeleportToLocal(dest):!RlvActions::canTeleportToLocation())) { error="restricted: teleport authority changed"; return false; }
            const auto& self=service();
            if(self.task && self.task->guard()->submitted()==0)
            {
                if(gAgentAvatarp->isSitting() || !llsd_equals(entry->value["baseline"]["position"],ll_sd_from_vector3d(gAgent.getPositionGlobal())) || entry->value["baseline"]["region"].asUUID()!=gAgent.getRegion()->getRegionID()) { error="conflict: avatar moved before queued teleport submission"; return false; }
            }
        }
        return true; // Our movement legitimately changes the baseline.
    }
    if(!native(operation.request,error)) return false;
    if(operation.request["op"].asString()!="status" && !llsd_equals(entry->value["baseline"],approvalBaseline(operation.request))) { error="conflict: avatar position or movement changed during approval"; return false; }
    return true;
}
void FSNavigationListener::dispatch(const LLSD& request)
{
    Response response(LLSD(),request);
    if(!request.has("__assistant_guard") || !fs_assistant_request_allowed(request)) return response.error("not_permitted: guarded bridge required");
    const auto guard=fs_assistant_async_context(request); fs_assistant::AsyncScope scope(guard);
    if(!guard || !fs_assistant::asyncAllowed()) return response.error("session_changed: navigation authority changed");
    auto& self=service(); const auto op=request["op"].asString();
    auto* entry=self.prepared.get(request["__navigation_prepared"].asString(),source(),now()); std::string error;
    if(!entry || entry->executed || !llsd_equals(entry->value["material"],material(request)) || request["__assistant_epoch"].asString()!=FSEventAPIBridge::instance().sessionReference()) return response.error("session_changed: exact unconsumed navigation preparation required");
    if(!arguments(request,error) || !native(request,error)) return response.error(error);
    if(op!="status" && !llsd_equals(entry->value["baseline"],approvalBaseline(request))) return response.error("conflict: navigation baseline changed");
    if(op=="status") { entry->executed=true; response.result(status()); return; }
    if(op=="stop") { entry->executed=true; finish("cancelled","explicit_stop",true); response.result(status()); return; }
    if(op!="teleport" && !fs_assistant::asyncSubmit()) return response.error("not_permitted: navigation revoked before execution");
    entry->executed=true;
    if(op=="flyingSet") { gAgent.setFlying(request["flying"].asBoolean()); LLSD result=status(); result["state"]=gAgent.getFlying()==request["flying"].asBoolean()?"applied":"refused"; response.result(result); return; }
    self.source=source(); self.region=gAgent.getRegion()->getRegionID().asString(); self.token="assistant-nav-"+LLUUID::generateNewID().asString(); self.destination=ll_vector3d_from_sd(request["position"]);
    self.teleport=op=="teleport"; self.flying=request["mode"].asString()=="fly"; self.reason.clear(); self.state=self.teleport?"teleport_pending":"moving"; self.deadline=now()+(self.teleport?60.:request["timeout_seconds"].asInteger()); self.task=fs_assistant::asyncTask();
    if(self.teleport) gAgent.teleportViaLocation(self.destination);
    else
    {
        if(self.flying) gAgent.setFlying(true);
        gAgent.startAutoPilotGlobal(self.destination,self.token,nullptr,[](bool success,void*)
        {
            auto& current=service();
            if(current.task && !current.teleport && gAgent.getAutoPilotBehaviorName()==current.token) finish(success?"arrived":"ended",success?"native_autopilot_target_reached":"native_autopilot_ended",false);
        },nullptr,static_cast<F32>(request["stop_distance"].asReal()),.03f,self.flying);
    }
    fs_navigation_tick(); response.result(status());
}
void fs_initialize_navigation_api() { static FSNavigationListener listener; }
