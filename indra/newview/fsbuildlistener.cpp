/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsbuildlistener.h"
#include "fsassistantworldmodel.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "fscommon.h"
#include "llagent.h"
#include "llapp.h"
#include "llcoros.h"
#include "llcorehttputil.h"
#include "llinventorymodel.h"
#include "llinventorydefines.h"
#include "message.h"
#include "llmaterialtable.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llselectmgr.h"
#include "llstartup.h"
#include "lltextureentry.h"
#include "lltimer.h"
#include "lltooldraganddrop.h"
#include "llviewerinventory.h"
#include "llviewernetwork.h"
#include "llviewerobjectlist.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llvolumemessage.h"
#include "llworld.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include <sstream>

namespace
{
using fs_assistant_world::Tickets;
double now() { return static_cast<double>(LLTimer::getTotalSeconds()); }
bool ready() { return LLStartUp::getStartupState()==STATE_STARTED && !gDisconnected && !LLApp::isExiting() && gAgent.getRegion() && gAgent.getID().notNull() && gAgent.getSessionID().notNull(); }
std::string source()
{
    if (!ready()) return {};
    // Private fingerprint; never return this value or the simulator SessionID.
    return gAgent.getID().asString()+":"+gAgent.getSessionID().asString()+":"+LLGridManager::instance().getGrid()+":"+
        gAgent.getRegion()->getRegionID().asString()+":"+FSEventAPIBridge::instance().sessionReference()+":"+ll_pretty_print_sd(FSSessionWorker::assistantIdentity());
}
struct Service { Tickets<LLSD> revisions, approvals; std::string source; };
Service& service()
{
    static Service value; const auto current=source();
    if(value.source!=current) { value.revisions.clear(); value.approvals.clear(); value.source=current; }
    return value;
}
bool inventoryOp(const std::string& op) { return op=="permissionsGet" || op=="nextOwnerSet"; }
bool readOp(const std::string& op) { return op=="objectGet" || op=="permissionsGet"; }
LLSD material(const LLSD& request)
{
    LLSD result=LLSD::emptyMap();
    for (const char* key : {"op","id","expected_revision","position","rotation","scale","face","color","glow","fullbright","uv_scale","uv_offset","uv_rotation","texture_item","material_item","shape","copy","modify","transfer"})
        if (request.has(key)) result[key]=request[key];
    return result;
}
bool vectorValue(const LLSD& value, int count, double low, double high)
{
    if (!value.isArray() || value.size()!=count) return false;
    for (const auto& component : llsd::inArray(value)) if ((!component.isReal() && !component.isInteger()) || !std::isfinite(component.asReal()) || component.asReal()<low || component.asReal()>high) return false;
    return true;
}
bool idValue(const LLSD& value) { return (value.isString() || value.isUUID()) && LLUUID::validate(value.asString()) && LLUUID(value.asString()).notNull(); }
bool arguments(const LLSD& request, std::string& error)
{
    const auto op=request["op"].asString();
    if (!fs_assistant::featureOperation("FSBuild",op)) { error="invalid_args: unknown building operation"; return false; }
    if (op!="rezPrimitive" && !idValue(request["id"])) { error="invalid_args: exact object or inventory id is required"; return false; }
    if (!readOp(op) && op!="rezPrimitive" && op!="rezInventory" && !idValue(request["expected_revision"])) { error="invalid_args: read this target first and supply its expected_revision"; return false; }
    for (const char* key : {"position","scale","rotation","color","uv_scale","uv_offset"}) if (request.has(key))
    {
        const std::string name(key); const int count=name=="rotation" || name=="color" ? 4 : name=="uv_scale" || name=="uv_offset" ? 2 : 3;
        const double low=name=="color" ? 0. : name=="scale" ? .001 : -1.e9;
        const double high=name=="color" ? 1. : name=="scale" ? 256. : 1.e9;
        if (!vectorValue(request[key],count,low,high)) { error="invalid_args: nonfinite or out-of-range vector"; return false; }
    }
    if (request.has("rotation"))
    { std::array<double,4> q{}; for(int i=0;i<4;++i) q[i]=request["rotation"][i].asReal(); if(!fs_assistant_world::quaternion(q)) { error="invalid_args: normalized quaternion [x,y,z,w] required"; return false; } }
    if (op=="transform" && !request.has("position") && !request.has("rotation") && !request.has("scale")) { error="invalid_args: empty transform"; return false; }
    if (op=="surface" || op=="material")
    {
        if (!request["face"].isInteger() || request["face"].asInteger()<0 || request["face"].asInteger()>63) { error="invalid_args: face must be 0..63"; return false; }
        if (op=="material" && !idValue(request["material_item"])) { error="invalid_args: inventory material_item required"; return false; }
        if (op=="surface" && !request.has("color") && !request.has("glow") && !request.has("fullbright") && !request.has("uv_scale") && !request.has("uv_offset") && !request.has("uv_rotation") && !request.has("texture_item")) { error="invalid_args: empty surface edit"; return false; }
    }
    for (const char* key : {"glow","uv_rotation"}) if (request.has(key))
        if ((!request[key].isReal() && !request[key].isInteger()) || !std::isfinite(request[key].asReal()) || request[key].asReal()<(std::string(key)=="glow"?0.:-6.283186) || request[key].asReal()>(std::string(key)=="glow"?1.:6.283186)) { error="invalid_args: invalid surface scalar"; return false; }
    if (request.has("fullbright") && !request["fullbright"].isBoolean()) { error="invalid_args: fullbright must be boolean"; return false; }
    if (request.has("texture_item") && !idValue(request["texture_item"])) { error="invalid_args: texture_item must be an inventory UUID"; return false; }
    if (op=="nextOwnerSet")
    {
        for(const char* key : {"copy","modify","transfer"}) if(!request[key].isBoolean()) { error="invalid_args: explicit Copy/Modify/Transfer booleans required"; return false; }
        if(!fs_assistant_world::nextOwner(request["copy"].asBoolean(),request["transfer"].asBoolean())) { error="not_permitted: no-copy items must remain transferable"; return false; }
    }
    if (op=="rezPrimitive" || op=="rezInventory")
    {
        if (!request.has("position")) { error="invalid_args: explicit region position required"; return false; }
        if(op=="rezPrimitive" && (!request.has("rotation") || !request.has("scale") || (request["shape"].asString()!="cube" && request["shape"].asString()!="sphere" && request["shape"].asString()!="cylinder" && request["shape"].asString()!="cone"))) { error="invalid_args: cube/sphere/cylinder/cone with explicit scale and rotation required"; return false; }
    }
    return true;
}
LLViewerInventoryItem* item(const LLSD& value)
{
    auto* result=gInventory.getItem(LLUUID(value.asString()));
    const auto trash=gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if (!result || !result->isFinished() || result->getIsLinkType() || !gInventory.isObjectDescendentOf(result->getUUID(),gInventory.getRootFolderID()) ||
        (trash.notNull() && gInventory.isObjectDescendentOf(result->getUUID(),trash)) || result->getPermissions().getOwner()!=gAgent.getID()) return nullptr;
    return result;
}
LLSD itemState(LLViewerInventoryItem* value)
{
    LLSD result; value->asLLSD(result); result["flags"]=static_cast<S32>(value->getFlags()); return result;
}
LLSD objectState(LLViewerObject* value)
{
    LLSD result=llsd::map("id",value->getID(),"local_id",static_cast<S32>(value->getLocalID()),"region_id",value->getRegion()->getRegionID(),
        "position",ll_sd_from_vector3(value->getPositionRegion()),"rotation",ll_sd_from_quaternion(value->getRotation()),"scale",ll_sd_from_vector3(value->getScale()),
        "owned",value->permYouOwner(),"modify",value->permModify(),"move",value->permMove(),"physical",value->flagUsePhysics(),"children",value->numChildren());
    result["linked_members"]=LLSD::emptyMap();
    for(auto child : value->getChildren())
        result["linked_members"][child->getID().asString()]=llsd::map("position",ll_sd_from_vector3(child->getPosition()),"rotation",ll_sd_from_quaternion(child->getRotation()),"scale",ll_sd_from_vector3(child->getScale()),"local_id",static_cast<S32>(child->getLocalID()));
    result["faces"]=LLSD::emptyArray();
    for(S32 face=0;face<value->getNumTEs();++face)
    { LLSD row=value->getTE(face)->asLLSD(); row["face"]=face; row["material_id"]=value->getRenderMaterialID(face); result["faces"].append(row); }
    return result;
}
LLViewerObject* object(const LLSD& value)
{
    auto* result=gObjectList.findObject(LLUUID(value.asString()));
    if(!result || result->isDead() || result->getPCode()!=LL_PCODE_VOLUME || result->getParent() || result->isAttachment() || result->getRegion()!=gAgent.getRegion() || result->getNumTEs()>64 || result->numChildren()>255) return nullptr;
    return result;
}
bool location(const LLSD& request, std::string& error)
{
    if (!request.has("position")) return true;
    const auto pos=ll_vector3_from_sd(request["position"]); auto* world=LLWorld::getInstance();
    if(gRlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH))
    {
        static RlvCachedBehaviourModifier<float> farTouch(RLV_MODIFIER_FARTOUCHDIST);
        if(dist_vec_squared(gAgent.getPositionGlobal(),gAgent.getRegion()->getPosGlobalFromRegion(pos))>farTouch*farTouch) { error="restricted: destination exceeds native interaction distance"; return false; }
    }
    if(pos.mV[0]<0 || pos.mV[1]<0 || pos.mV[0]>=gAgent.getRegion()->getWidth() || pos.mV[1]>=gAgent.getRegion()->getWidth() || pos.mV[2]<world->getMinPrimZPos() || pos.mV[2]>world->getMaxPrimZPos() ||
        dist_vec(gAgent.getPositionGlobal(),gAgent.getRegion()->getPosGlobalFromRegion(pos))>64.) { error="not_permitted: position must be in this region, within native height limits and 64m of the avatar"; return false; }
    return true;
}
bool native(const LLSD& request, LLSD& state, std::string& error)
{
    if(!ready()) { error="not_ready: a ready login is required"; return false; }
    const auto op=request["op"].asString();
    if(!inventoryOp(op) && (!RlvActions::canShowLocation() || gRlvHandler.hasBehaviour(RLV_BHVR_INTERACT))) { error="restricted: world interaction or location hidden"; return false; }
    if(inventoryOp(op) || op=="rezInventory")
    {
        auto* target=item(request["id"]); if(!target) { error="not_permitted: finished owned agent-inventory item outside Trash required"; return false; }
        state=itemState(target);
        if(op=="nextOwnerSet")
        {
            const auto& p=target->getPermissions();
            if(LLInventoryType::cannotRestrictPermissions(target->getInventoryType()) || !gAgent.allowOperation(PERM_OWNER,p,GP_OBJECT_SET_SALE) || !gAgent.allowOperation(PERM_TRANSFER,p,GP_OBJECT_MANIPULATE)) { error="not_permitted: native Properties disallows next-owner changes"; return false; }
            const U32 wanted=(request["copy"].asBoolean()?PERM_COPY:0U)|(request["modify"].asBoolean()?PERM_MODIFY:0U)|(request["transfer"].asBoolean()?PERM_TRANSFER:0U);
            const U32 mutableBits=PERM_COPY|PERM_MODIFY|PERM_TRANSFER;
            if((wanted & ~(p.getMaskBase() & p.getMaskOwner())) || (target->getInventoryType()==LLInventoryType::IT_SETTINGS && ((wanted^p.getMaskNextOwner())&PERM_COPY))) { error="not_permitted: requested rights exceed native base/owner limits"; return false; }
            LLPermissions changed(p);
            // Set Copy first, Transfer last; native fair-use never allows no-copy/no-transfer.
            changed.setNextOwnerBits(gAgent.getID(),gAgent.getGroupID(),request["copy"].asBoolean(),PERM_COPY);
            changed.setNextOwnerBits(gAgent.getID(),gAgent.getGroupID(),request["modify"].asBoolean(),PERM_MODIFY);
            changed.setNextOwnerBits(gAgent.getID(),gAgent.getGroupID(),request["transfer"].asBoolean(),PERM_TRANSFER);
            if((changed.getMaskNextOwner()&mutableBits)!=wanted) { error="not_permitted: native permission hierarchy rejects these rights"; return false; }
        }
        if(op=="rezInventory" && (target->getActualType()!=LLAssetType::AT_OBJECT || !target->getPermissions().allowCopyBy(gAgent.getID()))) { error="not_permitted: only copyable inventory objects can be rezzed"; return false; }
    }
    else if(op!="rezPrimitive")
    {
        auto* target=object(request["id"]); if(!target) { error="not_found: loaded unattached root volume in current region required"; return false; }
        state=objectState(target);
        if(!readOp(op) && (!target->permYouOwner() || !target->permModify() || !target->permMove() || target->flagUsePhysics() || !RlvActions::canEdit(target) || !RlvActions::canInteract(target))) { error="not_permitted: owned modifiable movable nonphysical root required"; return false; }
        if(op=="transform" && request.has("scale") && target->numChildren()!=0) { error="unsupported: linked-set resizing requires native Build"; return false; }
        if(op=="surface" || op=="material")
        {
            const auto face=request["face"].asInteger();
            if(face>=target->getNumTEs()) { error="invalid_args: face does not exist"; return false; }
            if(op=="surface" && target->getRenderMaterialID(face).notNull()) { error="unsupported: PBR face surface overrides require native Build; material assignment is available separately"; return false; }
            const auto key=op=="material"?"material_item":"texture_item";
            if(request.has(key))
            {
                auto* asset=item(request[key]); const auto type=op=="material"?LLAssetType::AT_MATERIAL:LLAssetType::AT_TEXTURE;
                if(!asset || asset->getActualType()!=type || asset->getAssetUUID().isNull() || (asset->getPermissions().getMaskOwner()&(PERM_COPY|PERM_MODIFY|PERM_TRANSFER))!=(PERM_COPY|PERM_MODIFY|PERM_TRANSFER)) { error="not_permitted: owned full-permission inventory asset required"; return false; }
                state["asset_item"]=itemState(asset);
            }
            if(op=="material" && gAgent.getRegionCapability("ModifyMaterialParams").empty()) { error="unsupported: simulator lacks material editing capability"; return false; }
        }
    }
    if(!readOp(op) && !inventoryOp(op))
    { if(!location(request,error)) return false; if(!FSSessionWorker::assistantActiveAllowed()) { error="requires_active_character: world changes require active ownership"; return false; } }
    if(request.has("scale"))
    {
        auto* world=LLWorld::getInstance();
        for(const auto& v:llsd::inArray(request["scale"])) if(v.asReal()<world->getRegionMinPrimScale() || v.asReal()>(std::min)(64.f,world->getRegionMaxPrimScaleNoMesh())) { error="not_permitted: scale exceeds native primitive limits or 64m"; return false; }
    }
    if(op=="rezPrimitive" || op=="rezInventory")
    {
        auto* parcels=LLViewerParcelMgr::getInstance();
        if(!RlvActions::canRez() || !RlvActions::canEdit(ERlvCheckType::All) || !parcels->inAgentParcel(gAgent.getRegion()->getPosGlobalFromRegion(ll_vector3_from_sd(request["position"]))) || !parcels->allowAgentBuild()) { error="not_permitted: rez requires current buildable agent parcel and native permission"; return false; }
        state["agent_position"]=ll_sd_from_vector3d(gAgent.getPositionGlobal());
        state["rez_group"]=FSCommon::getGroupForRezzing();
    }
    return true;
}
std::string ticket(Tickets<LLSD>& store, const LLSD& state)
{
    const auto id=LLUUID::generateNewID().asString(); store.put(id,source(),now(),state);
    return store.get(id,source(),now()) ? id : std::string();
}
bool baseline(const LLSD& request, const LLSD& state, std::string& error)
{
    const auto op=request["op"].asString();
    if(readOp(op) || op=="rezPrimitive" || op=="rezInventory") return true;
    auto* revision=service().revisions.get(request["expected_revision"].asString(),source(),now());
    LLSD compared=state; compared.erase("asset_item");
    if(!revision || !llsd_equals(revision->value,compared)) { error="conflict: target changed or revision expired; read it again"; return false; }
    return true;
}
LLSD resultBase() { return llsd::map("building_tools",1,"viewer_session",FSEventAPIBridge::instance().sessionReference(),"account_id",gAgent.getID(),"grid",LLGridManager::instance().getGrid()); }
void packAgent(LLMessageSystem* msg)
{ msg->nextBlockFast(_PREHASH_AgentData); msg->addUUIDFast(_PREHASH_AgentID,gAgent.getID()); msg->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID()); }
void rez(const LLSD& request)
{
    auto* region=gAgent.getRegion(); auto* msg=gMessageSystem; const auto pos=ll_vector3_from_sd(request["position"]);
    if(request["op"].asString()=="rezInventory")
    {
        auto* target=item(request["id"]);
        msg->newMessageFast(_PREHASH_RezObject); packAgent(msg); msg->addUUIDFast(_PREHASH_GroupID,FSCommon::getGroupForRezzing());
        msg->nextBlock("RezData"); msg->addUUIDFast(_PREHASH_FromTaskID,LLUUID::null); msg->addU8Fast(_PREHASH_BypassRaycast,1);
        msg->addVector3Fast(_PREHASH_RayStart,pos); msg->addVector3Fast(_PREHASH_RayEnd,pos); msg->addUUIDFast(_PREHASH_RayTargetID,LLUUID::null);
        msg->addBOOLFast(_PREHASH_RayEndIsIntersection,false); msg->addBOOLFast(_PREHASH_RezSelected,false); msg->addBOOLFast(_PREHASH_RemoveItem,false);
        pack_permissions_slam(msg,target->getFlags(),target->getPermissions()); msg->nextBlockFast(_PREHASH_InventoryData); target->packMessage(msg);
    }
    else
    {
        LLVolumeParams params; const auto shape=request["shape"].asString();
        params.setType(shape=="cube"?LL_PCODE_PROFILE_SQUARE:shape=="sphere"?LL_PCODE_PROFILE_CIRCLE_HALF:LL_PCODE_PROFILE_CIRCLE,shape=="sphere"?LL_PCODE_PATH_CIRCLE:LL_PCODE_PATH_LINE);
        params.setBeginAndEndS(0.f,1.f); params.setBeginAndEndT(0.f,1.f); params.setRatio(shape=="cone"?0.f:1.f,shape=="cone"?0.f:1.f); params.setShear(0.f,0.f);
        msg->newMessageFast(_PREHASH_ObjectAdd); packAgent(msg); msg->addUUIDFast(_PREHASH_GroupID,FSCommon::getGroupForRezzing());
        msg->nextBlockFast(_PREHASH_ObjectData); msg->addU8Fast(_PREHASH_Material,LL_MCODE_WOOD); msg->addU32Fast(_PREHASH_AddFlags,0);
        LLVolumeMessage::packVolumeParams(&params,msg); msg->addU8Fast(_PREHASH_PCode,LL_PCODE_VOLUME);
        msg->addVector3Fast(_PREHASH_Scale,ll_vector3_from_sd(request["scale"])); msg->addQuatFast(_PREHASH_Rotation,ll_quaternion_from_sd(request["rotation"]));
        msg->addVector3Fast(_PREHASH_RayStart,pos); msg->addVector3Fast(_PREHASH_RayEnd,pos); msg->addU8Fast(_PREHASH_BypassRaycast,1);
        msg->addUUIDFast(_PREHASH_RayTargetID,LLUUID::null); msg->addBOOLFast(_PREHASH_RayEndIsIntersection,false); msg->addU8Fast(_PREHASH_State,0);
    }
    msg->sendReliable(region->getHost());
}
void transform(LLViewerObject* target,const LLSD& request)
{
    U8 type=UPD_LINKED_SETS; U8 data[36]; S32 offset=0;
    auto put=[&](const LLVector3& value,int flag) { htolememcpy(data+offset,value.mV,MVT_LLVector3,12); offset+=12; type|=static_cast<U8>(flag); };
    if(request.has("position")) { target->setPositionRegion(ll_vector3_from_sd(request["position"])); put(target->getPosition(),UPD_POSITION); }
    if(request.has("rotation")) { target->setRotation(ll_quaternion_from_sd(request["rotation"])); put(target->getRotation().packToVector3(),UPD_ROTATION); }
    if(request.has("scale")) { target->setScale(ll_vector3_from_sd(request["scale"])); put(target->getScale(),UPD_SCALE); }
    auto* msg=gMessageSystem; msg->newMessageFast(_PREHASH_MultipleObjectUpdate); packAgent(msg); msg->nextBlockFast(_PREHASH_ObjectData);
    msg->addU32Fast(_PREHASH_ObjectLocalID,target->getLocalID()); msg->addU8Fast(_PREHASH_Type,type); msg->addBinaryDataFast(_PREHASH_Data,data,offset); msg->sendReliable(target->getRegion()->getHost());
}
}
FSBuildListener::FSBuildListener() : LLEventAPI("FSBuild","Source-bound building and inventory next-owner permissions")
{ for(const char* op:{"objectGet","transform","surface","material","rezPrimitive","rezInventory","permissionsGet","nextOwnerSet"}) add(op,"Exact ready login; native permissions and restrictions",&FSBuildListener::dispatch); }
LLSD FSBuildListener::assistantLimits() const
{ return llsd::map("version",1,"max_faces",64,"max_distance",64,"revision_seconds",120,"copyable_rez_only",true,"linked_scale",false,"pbr_surface_overrides",false); }
bool FSBuildListener::prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error) const
{
    LLSD state; if(!arguments(operation.request,error) || !native(operation.request,state,error) || !baseline(operation.request,state,error)) return false;
    const auto op=operation.request["op"].asString();
    operation.requiredClasses=inventoryOp(op)?std::vector<std::string>{"read"}:std::vector<std::string>{"world_read"};
    if(!readOp(op)) operation.requiredClasses.emplace_back(op=="nextOwnerSet"?"inventory_permissions":op=="rezPrimitive" || op=="rezInventory"?"build_rez":"build_edit");
    if(op=="rezInventory" || operation.request.has("texture_item") || operation.request.has("material_item")) operation.requiredClasses.emplace_back("read");
    LLSD frozen=llsd::map("material",material(operation.request),"state",state);
    const auto nonce=ticket(service().approvals,frozen); if(nonce.empty()) { error="busy: too many live building requests"; return false; }
    operation.validation["build_prepared"]=nonce; operation.request["__build_prepared"]=nonce;
    operation.summary=llsd::map("op",op,"action",op=="nextOwnerSet"?"Change inventory next-owner permissions":op=="rezPrimitive" || op=="rezInventory"?"Rez an object (submission only)":"Edit an owned object", "subject_count",1,"target",operation.request["id"].asString(),"value",ll_stream_notation_sd(material(operation.request)));
    if(!readOp(op)) operation.summary["subjects"].append(llsd::map("name",operation.request["id"].asString(),"before_name",ll_stream_notation_sd(state),"after_name",ll_stream_notation_sd(material(operation.request))));
    return true;
}
bool FSBuildListener::validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error) const
{
    auto* entry=service().approvals.get(operation.validation["build_prepared"].asString(),source(),now());
    if(!entry || !llsd_equals(entry->value["material"],material(operation.request))) { error="session_changed: building approval expired or source changed"; return false; }
    if(entry->executed)
    {
        // AIS may wait behind other inventory work. Optimistic local state is
        // already changed, but late ownership/parent/asset changes still veto it.
        if(operation.request["op"].asString()=="nextOwnerSet")
        {
            auto* current=item(operation.request["id"]);
            if(!current) { error="target_changed: item no longer editable"; return false; }
            const auto latest=itemState(current);
            if(!llsd_equals(latest,entry->value["state"]) && !llsd_equals(latest,entry->value["after"])) { error="target_changed: inventory changed before deferred submission"; return false; }
        }
        return true;
    }
    LLSD state; if(!native(operation.request,state,error)) return false;
    if(!llsd_equals(state,entry->value["state"])) { error="conflict: native target changed while awaiting approval"; return false; }
    return true;
}
void FSBuildListener::dispatch(const LLSD& request)
{
    Response response(LLSD(),request);
    if(!request.has("__assistant_guard") || !fs_assistant_request_allowed(request)) return response.error("not_permitted: bridge guard required");
    const auto guard=fs_assistant_async_context(request); fs_assistant::AsyncScope scope(guard);
    if(!guard || !fs_assistant::asyncAllowed()) return response.error("session_changed: building authority changed");
    auto* entry=service().approvals.get(request["__build_prepared"].asString(),source(),now()); LLSD state; std::string error;
    if(!entry || entry->value["dispatched"].asBoolean() || entry->executed || !llsd_equals(entry->value["material"],material(request)) || request["__assistant_epoch"].asString()!=FSEventAPIBridge::instance().sessionReference()) return response.error("session_changed: exact unconsumed preparation required");
    if(!arguments(request,error) || !native(request,state,error) || !llsd_equals(state,entry->value["state"])) return response.error(error.empty()?"conflict: target changed":error);
    const auto op=request["op"].asString(); LLSD result=resultBase();
    if(readOp(op))
    {
        const auto revision=ticket(service().revisions,state); if(revision.empty()) return response.error("busy: too many live revisions; wait for expiry");
        result["target"]=state; result["expected_revision"]=revision; result["status"]="observed"; entry->executed=true; response.result(result); return;
    }
    if(op=="material")
    {
        // Own the exact transport rather than adding unguarded work to the
        // shared native PBR queue. No AsyncScope may live across suspension.
        const auto task=fs_assistant::asyncTask(); const auto cap=gAgent.getRegionCapability("ModifyMaterialParams");
        const auto asset=item(request["material_item"])->getAssetUUID();
        LLSD data=LLSD::emptyArray(); data.append(llsd::map("object_id",LLUUID(request["id"].asString()),"side",request["face"],"asset_id",asset,"gltf_json",""));
        entry->value["dispatched"]=true;
        const auto nonce=request["__build_prepared"].asString(); const auto origin=source();
        LLCoros::instance().launch("assistantMaterial",[task,cap,data,nonce,origin]()
        {
            auto adapter=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("assistantMaterial",LLCore::HttpRequest::DEFAULT_POLICY_ID);
            auto http=std::make_shared<LLCore::HttpRequest>();
            auto* prepared=service().approvals.get(nonce,origin,now());
            if(!prepared || !fs_assistant::asyncTaskSubmit(task)) return;
            prepared->executed=true;
            // No automatic retry and no redirect to another capability host.
            auto options=std::make_shared<LLCore::HttpOptions>(); options->setRetries(0); options->setFollowRedirects(false);
            const LLSD reply=adapter->postAndSuspend(http,cap,data,options);
            const auto status=LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(reply[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
            if(status && reply["success"].asBoolean()) task->guard()->acknowledge("material_http_accepted");
            else task->guard()->cancel("material_transport_failed_or_unconfirmed");
        });
        result["status"]="queued"; result["confirmation"]="inspect operation receipt and native object; no automatic retry"; response.result(result); return;
    }
    if(!fs_assistant::asyncSubmit()) return response.error("not_permitted: authority revoked before submission");
    entry->executed=true;
    if(op=="nextOwnerSet")
    {
        auto* original=item(request["id"]); LLPermissions permissions(original->getPermissions());
        permissions.setNextOwnerBits(gAgent.getID(),gAgent.getGroupID(),request["copy"].asBoolean(),PERM_COPY);
        permissions.setNextOwnerBits(gAgent.getID(),gAgent.getGroupID(),request["modify"].asBoolean(),PERM_MODIFY);
        permissions.setNextOwnerBits(gAgent.getID(),gAgent.getGroupID(),request["transfer"].asBoolean(),PERM_TRANSFER);
        LLPointer<LLViewerInventoryItem> changed=new LLViewerInventoryItem(original); changed->setPermissions(permissions);
        if(original->getActualType()==LLAssetType::AT_OBJECT && permissions.getMaskNextOwner()!=original->getPermissions().getMaskNextOwner()) changed->setFlags(changed->getFlags()|LLInventoryItemFlags::II_FLAGS_OBJECT_SLAM_PERM);
        entry->value["after"]=itemState(changed);
        changed->updateServer(false); gInventory.updateItem(changed); gInventory.notifyObservers(); result["target"]=itemState(changed);
    }
    else if(op=="rezPrimitive" || op=="rezInventory") { rez(request); result["object_id_confirmed"]=false; result["permissions_source"]="inventory item for inventory rez; simulator defaults for primitive rez"; }
    else if(op=="transform") transform(object(request["id"]),request);
    else if(op=="surface")
    {
        auto* target=object(request["id"]); const U8 face=static_cast<U8>(request["face"].asInteger());
        if(request.has("color")) { const auto& c=request["color"]; target->setTEColor(face,LLColor4(static_cast<F32>(c[0].asReal()),static_cast<F32>(c[1].asReal()),static_cast<F32>(c[2].asReal()),static_cast<F32>(c[3].asReal()))); }
        if(request.has("glow")) target->setTEGlow(face,static_cast<F32>(request["glow"].asReal()));
        if(request.has("fullbright")) target->setTEFullbright(face,request["fullbright"].asBoolean()?1:0);
        if(request.has("uv_scale")) target->setTEScale(face,static_cast<F32>(request["uv_scale"][0].asReal()),static_cast<F32>(request["uv_scale"][1].asReal()));
        if(request.has("uv_offset")) target->setTEOffset(face,static_cast<F32>(request["uv_offset"][0].asReal()),static_cast<F32>(request["uv_offset"][1].asReal()));
        if(request.has("uv_rotation")) target->setTERotation(face,static_cast<F32>(request["uv_rotation"].asReal()));
        if(request.has("texture_item")) target->setTETexture(face,item(request["texture_item"])->getAssetUUID());
        target->sendTEUpdate();
    }
    result["status"]="submitted"; result["simulator_confirmed"]=false; response.result(result);
}
void fs_initialize_build_api() { static FSBuildListener listener; }
