/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsobjecttools.h"
#include "fsobjecttoolsmodel.h"
#include "fsassistantfeature.h"
#include "fscontentpermissions.h"
#include "fscontentpolicy.h"
#include "fscontentupload.h"
#include "fsinventorybulk.h"
#include "llsdutil_math.h"
#include "rlvcommon.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "llassetstorage.h"
#include "llapp.h"
#include "llchat.h"
#include "lleventapi.h"
#include "llfilesystem.h"
#include "llfloaterreg.h"
#include "llpreviewscript.h"
#include "llscripteditor.h"
#include "llviewerassetupload.h"
#include "llinventorydefines.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "message.h"
#include "llsdserialize.h"
#include "llstartup.h"
#include "llselectmgr.h"
#include "lltimer.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoinventorylistener.h"
#include "rlvactions.h"
#include <set>

namespace {
double now(){return LLTimer::getTotalSeconds();}
bool ready()
{return LLStartUp::getStartupState()==STATE_STARTED && !gDisconnected && !LLApp::isExiting() &&
    gAgent.getRegion() && gAgent.getID().notNull() && FSEventAPIBridge::instanceExists() && FSEventAPIBridge::instance().isRunning();}
std::string source()
{
    if(!ready())return {};
    return fs_content::hash(ll_stream_notation_sd(fs_content::identity())+":"+
        gAgent.getRegion()->getRegionID().asString()+":"+FSEventAPIBridge::instance().sessionReference());
}
bool id(const LLSD& value){return (value.isUUID() || value.isString()) && LLUUID::validate(value.asString()) && value.asUUID().notNull();}
LLViewerObject* target(const LLUUID& uuid,std::string& error)
{
    auto* object=gObjectList.findObject(uuid);
    if(!ready() || !object || object->isDead() || object->getRegion()!=gAgent.getRegion() ||
       object->getPCode()!=LL_PCODE_VOLUME || object->isAttachment() || !object->permYouOwner() || !object->permModify())
    {error="not_permitted: exact loaded owned modifiable unattached prim in this region required";return nullptr;}
    if(!RlvActions::canEdit(object) || !RlvActions::canInteract(object) || !RlvActions::canShowLocation())
    {error="restricted: native object editing unavailable";return nullptr;}
    return object;
}
LLSD objectEvidence(LLViewerObject* object)
{
    return llsd::map("object_id",object->getID(),"root_id",object->getRootEdit()->getID(),
        "local_id",static_cast<S32>(object->getLocalID()),"region_id",object->getRegion()->getRegionID(),
        "inventory_serial",static_cast<S32>(object->getInventorySerial()),
        "server_generation",std::to_string(object->getInventoryServerGeneration()),
        "change_generation",std::to_string(object->getInventoryChangeGeneration()));
}
bool scriptReadable(const LLViewerInventoryItem* item)
{
    return item && item->getActualType()==LLAssetType::AT_LSL_TEXT && item->getAssetUUID().notNull() &&
        item->getPermissions().getOwner()==gAgent.getID() && !item->getIsLinkType() &&
        gAgent.allowOperation(PERM_COPY,item->getPermissions(),GP_OBJECT_MANIPULATE) &&
        gAgent.allowOperation(PERM_MODIFY,item->getPermissions(),GP_OBJECT_MANIPULATE) &&
        !gRlvHandler.hasBehaviour(RLV_BHVR_VIEWSCRIPT);
}
LLSD itemEvidence(const LLViewerInventoryItem* item)
{LLSD value;item->asLLSD(value);value["asset_id"]=item->getAssetUUID();value["native_asset_type"]=static_cast<S32>(item->getActualType());return value;}
LLSD itemRow(const LLViewerInventoryItem* item)
{
    LLSD value=llsd::map("item_id",item->getUUID(),"name",fs_content::excerpt(item->getName(),255),
        "type",LLAssetType::lookup(item->getActualType()),"native_source_readable",scriptReadable(item),
        "copy",gAgent.allowOperation(PERM_COPY,item->getPermissions(),GP_OBJECT_MANIPULATE),
        "modify",gAgent.allowOperation(PERM_MODIFY,item->getPermissions(),GP_OBJECT_MANIPULATE));
    if(scriptReadable(item))value["asset_id"]=item->getAssetUUID();
    return value;
}
struct Snapshot
{
    std::string source,epoch;LLSD object;double expires=0.;
    std::map<LLUUID,LLPointer<LLViewerInventoryItem>> items;
};
struct SourceSnapshot {std::string source,epoch,text,hash;LLSD object,item;double expires=0.;};
struct ContentsRead;
struct Mutation;
struct RunningRead;
struct Running {std::string source,epoch;LLUUID token,asset;bool running=false,mono=false;double at=0.;};
struct Service
{
    std::string source;std::map<LLUUID,Snapshot> snapshots;std::map<LLUUID,SourceSnapshot> scripts;
    std::vector<std::shared_ptr<ContentsRead>> reads;
    fs_object_tools::Journal<LLSD> diagnostics;
    std::map<std::string,std::uint64_t> cursors;
    fs_object_tools::Receipts<std::shared_ptr<Mutation>> operations;
    std::map<std::pair<LLUUID,LLUUID>,Running> running;
    std::vector<std::shared_ptr<RunningRead>> runningReads;
    std::map<LLUUID,LLHost> derezTransactions;
};
Service& service(){static Service value;return value;}
void syncService()
{
    auto& self=service();const auto current=source();
    if(self.source==current)return;
    self.source=current;self.snapshots.clear();self.scripts.clear();self.cursors.clear();self.diagnostics.clear();
    self.operations.clear();self.running.clear();
    // Destroy listeners only from maintenance, never from an inventory callback.
}
LLSD resultBase(){return llsd::map("viewer_session",FSEventAPIBridge::instance().sessionReference(),"account",gAgent.getID(),"grid",LLGridManager::instance().getGrid());}
void reply(const LLSD& request,const LLSD& result)
{if(fs_assistant_request_allowed(request))sendReply(result,request);}
Snapshot* contentsSnapshot(const LLSD& request,std::string& error)
{
    const auto it=service().snapshots.find(request["snapshot_id"].asUUID());
    if(it==service().snapshots.end() || it->second.source!=source() || it->second.epoch!=request["__assistant_epoch"].asString() || now()>it->second.expires)
    {error="snapshot_expired: fetch Contents again";return nullptr;}
    auto* object=target(it->second.object["object_id"].asUUID(),error);
    if(!object || object->isInventoryDirty() || !llsd_equals(objectEvidence(object),it->second.object))
    {error="conflict: task inventory changed; fetch Contents again";return nullptr;}
    return &it->second;
}
LLSD contentsPage(const LLSD& request,const LLUUID& token,const Snapshot& value,std::string& error)
{
    const S32 offset=request.has("offset")?request["offset"].asInteger():0;
    const S32 limit=request.has("limit")?request["limit"].asInteger():50;
    if(offset<0 || offset>4096 || limit<1 || limit>100){error="invalid_args: offset 0..4096, limit 1..100";return {};}
    LLSD result=resultBase();result["snapshot_id"]=token;result["target"]=value.object;
    result["complete"]=true;result["entries"]=LLSD::emptyArray();S32 index=0;
    for(const auto& entry:value.items)if(index++>=offset && result["entries"].size()<limit)result["entries"].append(itemRow(entry.second));
    result["total"]=static_cast<S32>(value.items.size());result["has_more"]=offset+result["entries"].size()<static_cast<S32>(value.items.size());
    return result;
}
struct ContentsRead:LLVOInventoryListener
{
    LLSD request;std::string source;LLUUID object;fs_assistant::AsyncTaskPtr task;
    U64 generation=0;double deadline=0.;bool done=false;
    ContentsRead(const LLSD& value,LLViewerObject* prim):request(value),source(::source()),object(prim->getID()),task(fs_assistant::asyncTask()),
        generation(prim->getInventoryServerGeneration()),deadline(now()+25.)
    {registerVOInventoryListener(prim,nullptr);}
    void start(LLViewerObject* prim){prim->dirtyInventory();requestVOInventory();}
    void inventoryChanged(LLViewerObject* prim,LLInventoryObject::object_list_t* inventory,S32,void*)override
    {
        if(done || prim->getInventoryServerGeneration()<=generation)return;
        fs_assistant::AsyncScope scope(task->guard());std::string error;
        if(source!=::source() || !fs_assistant::asyncAllowed() || !fs_assistant_request_allowed(request) || !target(object,error))
        {done=true;return;}
        if(!inventory || !prim->isInventoryServerComplete())
        {done=true;reply(request,llsd::map("error","incomplete: simulator Contents could not be fully decoded"));return;}
        Snapshot value;value.source=source;value.epoch=request["__assistant_epoch"].asString();value.object=objectEvidence(prim);value.expires=now()+120.;
        std::size_t bytes=0;
        for(const auto& entry:*inventory)
        {
            auto* item=dynamic_cast<LLViewerInventoryItem*>(entry.get());if(!item)continue;
            bytes+=item->getName().size()+item->getDescription().size()+512;
            if(item->getUUID().isNull() || value.items.count(item->getUUID()) || value.items.size()>=4096 || bytes>2*1024*1024)
            {done=true;reply(request,llsd::map("error","incomplete: invalid or oversized task inventory"));return;}
            value.items.emplace(item->getUUID(),new LLViewerInventoryItem(item));
        }
        auto& entries=service().snapshots;
        for(auto it=entries.begin();it!=entries.end();)if(now()>it->second.expires)it=entries.erase(it);else ++it;
        if(entries.size()>=16){done=true;reply(request,llsd::map("error","capacity: finish existing Contents snapshots or wait for expiry"));return;}
        const auto token=LLUUID::generateNewID();auto result=contentsPage(request,token,value,error);
        if(error.empty())entries.emplace(token,std::move(value));else result=llsd::map("error",error);
        done=true;reply(request,result);
    }
};
struct AssetRead
{LLSD request,object,item;std::string source;fs_assistant::AsyncTaskPtr task;};
void scriptLoaded(const LLUUID& asset,LLAssetType::EType,void* userdata,S32 status,LLExtStat)
{
    std::unique_ptr<AssetRead> read(static_cast<AssetRead*>(userdata));
    fs_assistant::AsyncScope scope(read->task->guard());if(!read->task->claim("asset"))return;
    std::string error;auto* prim=target(read->object["object_id"].asUUID(),error);
    auto* item=prim?dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(read->item["item_id"].asUUID())):nullptr;
    if(read->source!=source() || !fs_assistant::asyncAllowed() || !fs_assistant_request_allowed(read->request) ||
       !prim || prim->isInventoryDirty() || !llsd_equals(objectEvidence(prim),read->object) || !scriptReadable(item) || !llsd_equals(itemEvidence(item),read->item))return;
    if(status || asset!=item->getAssetUUID()){reply(read->request,llsd::map("error","Task-bound source download failed"));return;}
    LLFileSystem file(asset,LLAssetType::AT_LSL_TEXT);const S32 size=file.getSize();
    if(size<0 || static_cast<std::size_t>(size)>fs_content::assetLimit){reply(read->request,llsd::map("error","Script asset exceeds bounded source limit"));return;}
    std::string text(static_cast<std::size_t>(size),'\0');
    if(size && (!file.read(reinterpret_cast<U8*>(&text[0]),size) || file.getLastBytesRead()!=size)){reply(read->request,llsd::map("error","Incomplete source download"));return;}
    if(!text.empty() && text.back()=='\0')text.pop_back();
    if(!fs_content::utf8(text)){reply(read->request,llsd::map("error","Script source is not valid UTF-8"));return;}
    const bool full=text.size()<=fs_content::textLimit;const auto hash=fs_content::hash(text);const auto token=LLUUID::generateNewID();
    auto result=resultBase();result["object_id"]=prim->getID();result["item_id"]=item->getUUID();result["asset_id"]=asset;
    result["text"]=fs_content::excerpt(text,fs_content::textLimit);result["complete"]=full;result["truncated"]=!full;result["atomic_revision_check"]=false;
    if(full)
    {
        auto& entries=service().scripts;
        for(auto it=entries.begin();it!=entries.end();)if(now()>it->second.expires)it=entries.erase(it);else ++it;
        if(entries.size()>=8){reply(read->request,llsd::map("error","capacity: source snapshots full; wait for expiry"));return;}
        entries.emplace(token,SourceSnapshot{source(),read->request["__assistant_epoch"].asString(),text,hash,read->object,read->item,now()+120.});
        result["source_snapshot"]=token;result["content_hash"]=hash;
    }
    reply(read->request,result);
}
bool mutationArguments(const LLSD&,std::string&);
bool prepareMutation(FSAssistantPreparedOperation&,std::string&);
bool validateMutation(const FSAssistantPreparedOperation&,std::string&);
void dispatchMutation(const LLSD&);
void tickMutations();
bool arguments(const LLSD& request,std::string& error)
{
    const auto op=request["op"].asString();
    if(!fs_assistant::featureOperation("FSObjects",op)){error="invalid_args: unknown object operation";return false;}
    if(op=="contents" || op=="scriptGet")
    {
        if(!id(request["object_id"]) || (op=="scriptGet" && (!id(request["item_id"]) || !id(request["snapshot_id"]))))
        {error="invalid_args: exact object_id and task item/snapshot for script source required";return false;}
        if(request.has("snapshot_id") && !id(request["snapshot_id"])){error="invalid_args: invalid snapshot";return false;}
        if((request.has("limit") && (!request["limit"].isInteger() || request["limit"].asInteger()<1 || request["limit"].asInteger()>100)) ||
           (request.has("offset") && (!request["offset"].isInteger() || request["offset"].asInteger()<0 || request["offset"].asInteger()>4096 || !id(request["snapshot_id"]))))
        {error="invalid_args: paginated reads need an exact snapshot and bounded integer offset/limit";return false;}
    }
    else if(op=="debugPoll")
    {
        if((request.has("object_id") && !id(request["object_id"])) ||
           (request.has("limit") && (!request["limit"].isInteger() || request["limit"].asInteger()<1 || request["limit"].asInteger()>100)) ||
           (request.has("cursor") && !id(request["cursor"])) ||
           (!request.has("cursor") && request["start"].asString()!="now" && request["start"].asString()!="retained"))
        {error="invalid_args: exact cursor or start=now/retained required";return false;}
    }
    else return mutationArguments(request,error);
    return true;
}
class ObjectListener:public LLEventAPI,public FSAssistantFeature
{
public:
    ObjectListener():LLEventAPI("FSObjects","Exact-prim script development and diagnostics")
    {for(const char* op:{"contents","scriptGet","debugPoll","scriptPreview","scriptSave","scriptStatus","scriptRunning","scriptReset","put","rename","remove","operationStatus","take","takeCopy","rezIdentify"})add(op,"Source-bound object tool",&ObjectListener::dispatch);}
    const char* assistantCapability()const override{return "task_inventory";}
    LLSD assistantCapabilities()const override{return llsd::map("task_inventory",1,"script_diagnostics",1,"task_scripts",1,"object_lifecycle",1);}
    LLSD assistantLimits()const override{return llsd::map("contents_items",4096,"source_bytes",65536,"diagnostic_events",2048,"diagnostic_bytes",2097152,"atomic_revision_check",false);}
    bool prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error)const override
    {
        syncService();const auto& request=operation.request;if(!ready() || !arguments(request,error))return false;
        const auto op=request["op"].asString();
        if(op!="contents" && op!="scriptGet" && op!="debugPoll")return prepareMutation(operation,error);
        operation.validation=llsd::map("source",source());
        if(op=="debugPoll")
        {
            if(gRlvHandler.hasBehaviour(RLV_BHVR_VIEWSCRIPT)){error="restricted: script diagnostics unavailable";return false;}
            return !request.has("object_id") || target(request["object_id"].asUUID(),error);
        }
        auto* prim=target(request["object_id"].asUUID(),error);if(!prim)return false;
        operation.requiredClasses.push_back("world_read");operation.validation["object"]=objectEvidence(prim);
        if(request.has("snapshot_id"))
        {auto* value=contentsSnapshot(request,error);if(!value || value->object["object_id"]!=request["object_id"].asUUID())return false;}
        if(op=="scriptGet")
        {
            operation.requiredClasses.push_back("read");auto* value=contentsSnapshot(request,error);if(!value)return false;
            const auto it=value->items.find(request["item_id"].asUUID());
            if(it==value->items.end() || !scriptReadable(it->second)){error="not_permitted: native COPY/MODIFY task script source required";return false;}
            operation.validation["item"]=itemEvidence(it->second);
        }
        return true;
    }
    bool validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error)const override
    {
        const auto op=operation.request["op"].asString();
        if(op!="contents" && op!="scriptGet" && op!="debugPoll")return validateMutation(operation,error);
        if(operation.validation["source"].asString()!=source()){error="session_changed";return false;}
        if(operation.request["op"].asString()=="debugPoll")return !gRlvHandler.hasBehaviour(RLV_BHVR_VIEWSCRIPT);
        auto* prim=target(operation.request["object_id"].asUUID(),error);if(!prim)return false;
        // Contents may legitimately refresh while a read is pending. Source reads
        // keep their exact task/item evidence and recheck it at delivery.
        if(operation.request["op"].asString()=="scriptGet" &&
            !llsd_equals(objectEvidence(prim),operation.validation["object"]))
        {error="conflict: task inventory changed";return false;}
        return true;
    }
    void dispatch(const LLSD& request)
    {
        if(!fs_assistant_request_allowed(request)){Response response(LLSD(),request);return response.error("expired_or_not_permitted");}
        fs_assistant::AsyncScope scope(fs_assistant_async_context(request));
        if(!fs_assistant::AsyncScope::current()){Response response(LLSD(),request);return response.error("Durable assistant context required");}
        syncService();std::string error;if(!arguments(request,error)){Response response(LLSD(),request);return response.error(error);}
        const auto op=request["op"].asString();
        if(op=="debugPoll")
        {
            Response response(LLSD(),request);auto& self=service();std::uint64_t position=0;
            if(request.has("cursor"))
            {const auto it=self.cursors.find(request["cursor"].asString());if(it==self.cursors.end())return response.error("cursor_expired: start a new diagnostic reader");position=it->second;}
            else if(request["start"].asString()=="now")position=self.diagnostics.latest();
            auto result=resultBase();result["gap"]=self.diagnostics.gap(position);result["events"]=LLSD::emptyArray();std::size_t bytes=0;bool more=false;
            const S32 limit=request.has("limit")?request["limit"].asInteger():50;
            for(const auto& entry:self.diagnostics.entries())
            {
                if(entry.sequence<=position)continue;
                std::string why;
                if(!target(entry.value["object_id"].asUUID(),why) || (request.has("object_id") && request["object_id"].asUUID()!=entry.value["object_id"].asUUID())){position=entry.sequence;continue;}
                if(result["events"].size()>=limit || bytes+entry.bytes>60*1024){more=true;break;}
                result["events"].append(entry.value);bytes+=entry.bytes;position=entry.sequence;
            }
            if(self.cursors.size()>=128)self.cursors.erase(self.cursors.begin());
            const auto next=LLUUID::generateNewID().asString();self.cursors[next]=position;result["next_cursor"]=next;result["has_more"]=more;
            response.setResponse(result);return;
        }
        if(op!="contents" && op!="scriptGet"){dispatchMutation(request);return;}
        auto* prim=target(request["object_id"].asUUID(),error);if(!prim){Response response(LLSD(),request);return response.error(error);}
        if(op=="contents")
        {
            if(request.has("snapshot_id"))
            {auto* value=contentsSnapshot(request,error);Response response(LLSD(),request);if(!value)return response.error(error);response.setResponse(contentsPage(request,request["snapshot_id"].asUUID(),*value,error));return;}
            if(service().reads.size()>=16){Response response(LLSD(),request);return response.error("capacity: too many pending Contents reads");}
            auto pending=std::make_shared<ContentsRead>(request,prim);service().reads.push_back(pending);pending->start(prim);return;
        }
        if(op=="scriptGet")
        {
            auto* value=contentsSnapshot(request,error);if(!value){Response response(LLSD(),request);return response.error(error);}
            auto* item=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(request["item_id"].asUUID()));
            if(!scriptReadable(item) || !gAssetStorage){Response response(LLSD(),request);return response.error("Native script source unavailable");}
            auto* pending=new AssetRead{request,objectEvidence(prim),itemEvidence(item),source(),fs_assistant::asyncTask()};
            gAssetStorage->getInvItemAsset(prim->getRegion()->getHost(),gAgent.getID(),gAgent.getSessionID(),item->getPermissions().getOwner(),
                prim->getID(),item->getUUID(),item->getAssetUUID(),LLAssetType::AT_LSL_TEXT,scriptLoaded,pending,true);return;
        }
    }
};
struct Mutation:LLVOInventoryListener
{
    LLSD request,before,item,receipt;std::string source,epoch,signature,key;
    fs_assistant::AsyncTaskPtr task;std::weak_ptr<LLResourceUploadInfo> upload;
    fs_content::UploadProgress progress;
    fs_object_tools::Acquisition acquisition;
    U64 generation=0;double deadline=0.,refreshAt=0.;bool submitted=false,finished=false,refresh=false;
    std::set<std::string> previousItems;
    void observe(LLViewerObject* prim){generation=prim->getInventoryServerGeneration();registerVOInventoryListener(prim,nullptr);}
    void inventoryChanged(LLViewerObject*,LLInventoryObject::object_list_t*,S32,void*)override;
};
struct RunningRead {LLSD request,item;std::string source;fs_assistant::AsyncTaskPtr task;double deadline=0.;bool done=false;};
bool mutationRead(const std::string& op){return op=="scriptStatus" || op=="scriptPreview" || op=="operationStatus";}
LLSD mutationMaterial(const LLSD& request)
{
    LLSD result=LLSD::emptyMap();
    for(const char* key:{"id","position","rotation","scale","shape","destination_folder","operation_id","op","object_id","item_id","snapshot_id","source_snapshot","content_hash","expected_asset_id","text","compiler","running_mode","running_snapshot","running","source_item","name"})
        if(request.has(key))result[key]=request[key];
    return result;
}
std::string mutationSignature(const LLSD& request){return fs_content::hash(ll_stream_notation_sd(mutationMaterial(request)));}
std::shared_ptr<Mutation> ownMutation(const LLSD& request)
{
    auto* entry=service().operations.find(request["client_request_id"].asString());
    return entry && entry->value && entry->value->source==source() && entry->value->epoch==request["__assistant_epoch"].asString()?entry->value:nullptr;
}
void settle(const std::shared_ptr<Mutation>& value,const std::string& state,const std::string& reason,bool pending=false)
{
    value->receipt["state"]=state;value->receipt["reason"]=reason;value->finished=true;
    auto* entry=service().operations.find(value->key);
    if(entry && entry->value==value){entry->terminal=true;entry->pending=pending?1:0;}
    if(!pending){value->task.reset();value->refresh=false;}
}
bool taskEditorBusy(const LLUUID& object,const LLUUID& item,const LLResourceUploadInfo* own=nullptr)
{
    if(LLViewerAssetUpload::hasPendingInventoryUpload(item,own))return true;
    const auto key=llsd::map("taskid",object,"itemid",item);
    auto* editor=LLFloaterReg::findTypedInstance<LLLiveLSLEditor>("preview_scriptedit",key);
    return editor && editor->assistantSourceBusy();
}
SourceSnapshot* scriptSnapshot(const LLSD& request,std::string& error)
{
    const auto it=service().scripts.find(request["source_snapshot"].asUUID());
    if(it==service().scripts.end() || it->second.source!=source() || it->second.epoch!=request["__assistant_epoch"].asString() ||
       it->second.expires<now() || it->second.hash!=request["content_hash"].asString() ||
       it->second.item["asset_id"].asUUID()!=request["expected_asset_id"].asUUID() ||
       it->second.object["object_id"].asUUID()!=request["object_id"].asUUID() || it->second.item["item_id"].asUUID()!=request["item_id"].asUUID())
    {error="conflict: complete fresh installed-script source snapshot required";return nullptr;}
    auto* prim=target(request["object_id"].asUUID(),error);
    auto* item=prim?dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(request["item_id"].asUUID())):nullptr;
    if(!prim || prim->isInventoryDirty() || !scriptReadable(item) || !llsd_equals(objectEvidence(prim),it->second.object) || !llsd_equals(itemEvidence(item),it->second.item))
    {error="conflict: installed script or Contents changed";return nullptr;}
    return &it->second;
}
bool prepareLifecycle(FSAssistantPreparedOperation&,std::string&);
void dispatchLifecycle(const LLSD&);
bool mutationArguments(const LLSD& request,std::string& error)
{
    const auto op=request["op"].asString();
    if(op=="operationStatus")
    {if(id(request["operation_id"]))return true;error="invalid_args: exact operation_id required";return false;}
    if(op=="take" || op=="takeCopy" || op=="rezIdentify")
    {
        if(!id(request["object_id"]) || !id(request["client_request_id"]) || (op=="rezIdentify"?!id(request["operation_id"]):(!id(request["snapshot_id"]) || !id(request["destination_folder"]))))
        {error="invalid_args: exact lifecycle target, snapshot/destination or receipt, and unique request UUID required";return false;}
        return true;
    }
    if(!id(request["object_id"]) || (op!="put" && !id(request["item_id"])))
    {error="invalid_args: exact object and task item required";return false;}
    if(!mutationRead(op) && !id(request["client_request_id"]))
    {error="invalid_args: unique client_request_id UUID required";return false;}
    if(op=="scriptSave" || op=="scriptPreview")
    {
        if(!id(request["source_snapshot"]) || !id(request["expected_asset_id"]) || request["content_hash"].asString().size()!=64 ||
           !request["text"].isString() || request["text"].asString().size()>fs_content::textLimit || !fs_content::utf8(request["text"].asString()) || request["compiler"].asString()!="mono")
        {error="invalid_args: complete source snapshot, hash, raw bounded UTF-8 and compiler=mono required";return false;}
        if(op=="scriptSave" && request["running_mode"].asString()!="stopped" && request["running_mode"].asString()!="preserve")
        {error="invalid_args: running_mode=stopped or preserve required";return false;}
        if(request["running_mode"].asString()=="preserve" && !id(request["running_snapshot"]))
        {error="invalid_args: preserve needs a fresh native running_snapshot";return false;}
    }
    else if(op!="scriptStatus" && !id(request["snapshot_id"]))
    {error="invalid_args: fresh Contents snapshot required";return false;}
    if(op=="put" && (!id(request["source_item"]) || (request.has("running") && !request["running"].isBoolean())))
    {error="invalid_args: exact source_item and optional explicit running boolean required";return false;}
    if(op=="scriptRunning" && !request["running"].isBoolean())
    {error="invalid_args: explicit running boolean required";return false;}
    if(op=="rename" && (!request["name"].isString() || request["name"].asString().empty() || request["name"].asString().size()>63 || !fs_content::utf8(request["name"].asString()) ||
        request["name"].asString().find_first_of("\r\n\t")!=std::string::npos))
    {error="invalid_args: nonempty single-line native item name of at most 63 UTF-8 bytes required";return false;}
    return true;
}
LLViewerInventoryItem* agentSource(const LLSD& request,std::string& error)
{
    auto* item=gInventory.getItem(request["source_item"].asUUID());
    const auto trash=gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    if(!item || !item->isFinished() || !fs_content::agentInventory(item->getUUID()) || item->getIsLinkType() ||
        (trash.notNull() && gInventory.isObjectDescendentOf(item->getUUID(),trash)) || item->getPermissions().getOwner()!=gAgent.getID() ||
        !gAgent.allowOperation(PERM_COPY,item->getPermissions(),GP_OBJECT_MANIPULATE))
    {error="not_permitted: owned copyable agent-inventory source required; links, Trash and no-copy transfers unavailable";return nullptr;}
    switch(item->getActualType())
    {case LLAssetType::AT_LSL_TEXT:case LLAssetType::AT_OBJECT:case LLAssetType::AT_NOTECARD:case LLAssetType::AT_SOUND:case LLAssetType::AT_TEXTURE:break;
     default:error="unsupported_type: supported Contents copies are scripts, objects, notecards, sounds and textures";return nullptr;}
    return item;
}
std::shared_ptr<Mutation> receiptOperation(const LLSD& request,std::string& error)
{
    for(const auto& entry:service().operations.entries())
        if(entry.second.value && entry.second.value->receipt["operation_id"].asUUID()==request["operation_id"].asUUID())
        {
            auto value=entry.second.value;
            if(value->source==source() && value->epoch==request["__assistant_epoch"].asString())return value;
        }
    error="operation_expired: receipt is unavailable for this exact source";return {};
}
bool requestedRunning(const LLSD& request,bool& running,std::string& error)
{
    running=false;if(request["running_mode"].asString()!="preserve")return true;
    const auto it=service().running.find({request["object_id"].asUUID(),request["item_id"].asUUID()});
    if(it==service().running.end() || it->second.source!=source() || it->second.epoch!=request["__assistant_epoch"].asString() ||
        it->second.token!=request["running_snapshot"].asUUID() || it->second.at+10.<now() || it->second.asset!=request["expected_asset_id"].asUUID())
    {error="running_snapshot_expired: query native script status again";return false;}
    running=it->second.running;return true;
}
bool prepareMutation(FSAssistantPreparedOperation& operation,std::string& error)
{
    const auto& request=operation.request;const auto op=request["op"].asString();
    operation.validation=llsd::map("source",source(),"material",mutationMaterial(request));
    operation.requiredClasses.push_back("world_read");operation.requiredClasses.push_back("task_inventory_read");
    if(op=="scriptSave" || op=="scriptPreview" || op=="put" || op=="operationStatus")operation.requiredClasses.push_back("read");
    if(op=="operationStatus")
    {
        auto value=receiptOperation(request,error);
        if(!RlvActions::canShowLocation() || (value && value->request["op"].asString()=="scriptSave" && gRlvHandler.hasBehaviour(RLV_BHVR_VIEWSCRIPT)))
        {error="restricted: operation details unavailable";return false;}
        return bool(value);
    }
    if(auto own=ownMutation(request))
    {if(own->signature!=mutationSignature(request)){error="request_id_reused: material arguments differ";return false;}operation.validation["replay"]=true;return true;}
    if(op=="take" || op=="takeCopy" || op=="rezIdentify")return prepareLifecycle(operation,error);
    auto* prim=target(request["object_id"].asUUID(),error);if(!prim)return false;
    operation.requiredClasses.push_back("world_read");operation.requiredClasses.push_back("task_inventory_read");
    operation.validation["object"]=objectEvidence(prim);
    LLViewerInventoryItem* item=nullptr;
    if(op!="put")
    {item=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(request["item_id"].asUUID()));if(!item){error="task_item_missing";return false;}operation.validation["item"]=itemEvidence(item);}
    if(op=="scriptStatus")
    {if(!scriptReadable(item)){error="not_permitted: native-readable task script required";return false;}return true;}
    if(!mutationRead(op))
    {
        for(const auto& entry:service().operations.entries())if(entry.second.pending && entry.second.value && entry.second.value->request["object_id"]==request["object_id"])
        {error="busy: reconcile the previous operation on this prim first";return false;}
    }
    if(op=="scriptSave" || op=="scriptPreview")
    {
        operation.requiredClasses.push_back("read");auto* value=scriptSnapshot(request,error);if(!value)return false;
        operation.validation["source_hash"]=value->hash;
        if(op=="scriptSave")
        {
            operation.requiredClasses.push_back("task_inventory_write");
            if(taskEditorBusy(prim->getID(),item->getUUID())){error="busy: close the native script editor and finish its saves";return false;}
            if(prim->getRegion()->getCapability("UpdateScriptTask").empty() || prim->getRegion()->getCapability("GetMetadata").empty())
            {error="unsupported_capability: task compile requires UpdateScriptTask and metadata to preserve its Experience";return false;}
            bool running=false;if(!requestedRunning(request,running,error))return false;
            operation.validation["running"]=running;if(running)operation.requiredClasses.push_back("script_run");
        }
        const auto diff=fs_content::diff(value->text,request["text"].asString());
        operation.summary["diff"]=llsd::map("before",diff.before,"after",diff.after,"truncated",diff.truncated);
        operation.summary["replacement_text"]=request["text"];operation.summary["compiler"]="mono";
    }
    else
    {
        if(item && item->getActualType()==LLAssetType::AT_LSL_TEXT && taskEditorBusy(prim->getID(),item->getUUID()))
        {error="busy: close the native script editor and finish its saves";return false;}
        auto* value=contentsSnapshot(request,error);if(!value || value->object["object_id"]!=request["object_id"].asUUID())return false;
        if(op=="put")
        {
            operation.requiredClasses.push_back("read");auto* src=agentSource(request,error);if(!src)return false;
            if(value->items.count(src->getUUID())){error="conflict: source identity already exists in this prim; edit the installed item explicitly";return false;}
            operation.validation["agent_source"]=itemEvidence(src);
            if(request["running"].asBoolean())
            {if(src->getActualType()!=LLAssetType::AT_LSL_TEXT){error="invalid_args: running is only valid for scripts";return false;}operation.requiredClasses.push_back("script_run");}
        }
        else if(op=="scriptRunning" || op=="scriptReset")
        {if(!scriptReadable(item) || taskEditorBusy(prim->getID(),item->getUUID())){error="not_permitted_or_busy: eligible script with no active native editor required";return false;}}
        else if(!gAgent.allowOperation(PERM_COPY,item->getPermissions(),GP_OBJECT_MANIPULATE) ||
            !gAgent.allowOperation(PERM_MODIFY,item->getPermissions(),GP_OBJECT_MANIPULATE))
        {error="not_permitted: native COPY/MODIFY Contents item required";return false;}
    }
    operation.summary["op"]=op;operation.summary["subject_count"]=1;operation.summary["subjects"]=LLSD::emptyArray();
    operation.summary["subjects"].append(llsd::map("id",prim->getID(),"name",prim->getID().asString(),"source",item?item->getName():request["source_item"].asString(),
        "destination",op+" in exact prim "+prim->getID().asString(),"after_name",request["name"].asString()));
    operation.summary["consequences"]=LLSD::emptyArray();
    if(op=="remove")operation.summary["consequences"].append("object_contents_irreversible");
    if(op=="scriptSave" || op=="scriptReset")operation.summary["consequences"].append("object_script_execution_reset");
    return true;
}
bool takeEligible(LLViewerObject*,const LLSD&,std::string&);
bool liveMutation(const std::shared_ptr<Mutation>& value)
{
    if(value->source!=source() || (!value->task && !value->submitted) || (value->finished && !value->submitted))return false;
    std::string error;auto* prim=target(value->request["object_id"].asUUID(),error);
    const auto kind=value->request["op"].asString();
    if(kind=="take" || kind=="takeCopy")
    {return value->submitted || (prim && llsd_equals(objectEvidence(prim),value->before) && takeEligible(prim,value->request,error));}
    if(!prim)return false;
    const auto current=objectEvidence(prim);
    for(const char* key:{"object_id","root_id","local_id","region_id"})if(current[key]!=value->before[key])return false;
    const auto op=value->request["op"].asString();
    if(!value->submitted && !llsd_equals(current,value->before))return false;
    if(op=="put")
    {auto* item=agentSource(value->request,error);return item && llsd_equals(itemEvidence(item),value->item);}
    auto* item=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(value->request["item_id"].asUUID()));
    if(op=="remove" && value->submitted && !item)return true;
    if(!item || !gAgent.allowOperation(PERM_MODIFY,item->getPermissions(),GP_OBJECT_MANIPULATE))return false;
    if(op=="scriptSave")
    {
        const auto upload=value->upload.lock();if(!scriptReadable(item) || taskEditorBusy(prim->getID(),item->getUUID(),upload.get()))return false;
        auto expected=value->item;
        if(value->receipt["source_saved"].asBoolean())expected["asset_id"]=value->receipt["asset_id"];
        return llsd_equals(itemEvidence(item),expected) || llsd_equals(itemEvidence(item),value->item);
    }
    return true;
}
bool validateMutation(const FSAssistantPreparedOperation& operation,std::string& error)
{
    const auto& request=operation.request;
    if(operation.validation["source"].asString()!=source()){error="session_changed";return false;}
    if(request["op"].asString()=="operationStatus")return bool(receiptOperation(request,error));
    if(auto own=ownMutation(request))
    {
        if(own->signature!=mutationSignature(request)){error="request_id_reused";return false;}
        if(operation.validation["replay"].asBoolean())return true;
        if(!liveMutation(own)){error="target_or_editor_changed";return false;}return true;
    }
    if(request["op"].asString()=="scriptStatus")
    {auto* prim=target(request["object_id"].asUUID(),error);return prim && !prim->isInventoryDirty();}
    FSAssistantPreparedOperation latest;latest.request=request;
    if(!prepareMutation(latest,error))return false;
    if(!llsd_equals(latest.validation,operation.validation)){error="conflict: task or source changed after approval";return false;}return true;
}
void requestRunning(LLViewerObject* prim,const LLUUID& item)
{
    LLMessageSystem* msg=gMessageSystem;msg->newMessageFast(_PREHASH_GetScriptRunning);msg->nextBlockFast(_PREHASH_Script);
    msg->addUUIDFast(_PREHASH_ObjectID,prim->getID());msg->addUUIDFast(_PREHASH_ItemID,item);msg->sendReliable(prim->getRegion()->getHost());
}
void Mutation::inventoryChanged(LLViewerObject* prim,LLInventoryObject::object_list_t* contents,S32,void*)
{
    if(source!=::source() || !submitted || !contents || !prim->isInventoryServerComplete() || prim->getInventoryServerGeneration()<=generation)return;
    generation=prim->getInventoryServerGeneration();const auto op=request["op"].asString();LLViewerInventoryItem* matched=nullptr;
    if(op=="put")
    {
        std::vector<fs_object_tools::Candidate> candidates;
        for(const auto& raw:*contents)
        {
            auto* candidate=dynamic_cast<LLViewerInventoryItem*>(raw.get());
            if(candidate)candidates.push_back({candidate->getUUID().asString(),candidate->getAssetUUID().asString(),static_cast<int>(candidate->getActualType())});
        }
        const auto match=fs_object_tools::correlateInsertion(previousItems,candidates,item["asset_id"].asUUID().asString(),item["native_asset_type"].asInteger());
        if(match.ambiguous){receipt["correlation"]="ambiguous";return;}
        if(!match.id.empty())matched=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(LLUUID(match.id)));
        if(!matched)return;receipt["task_item_id"]=matched->getUUID();receipt["destination_present"]=true;
    }
    else
    {
        matched=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(request["item_id"].asUUID()));
        if(op=="remove"){if(matched)return;receipt["destination_absent"]=true;}
        else if(op=="rename"){if(!matched || matched->getName()!=request["name"].asString())return;}
        else return;
    }
    receipt["state"]="simulator_observed";receipt["correlation"]="fresh_contents";finished=true;refresh=false;
    auto* entry=service().operations.find(key);if(entry && entry->value.get()==this){entry->terminal=true;entry->pending=0;}
    task.reset();
}
void finishTaskUpload(const std::shared_ptr<Mutation>& value,const LLSD& result,bool success,const std::string& reason)
{
    if(!value->progress.finish())return;
    const auto asset=result["new_asset"].asUUID();const bool confirmed=value->progress.sourceSubmitted() && asset.notNull() && result["state"].asString()=="complete";
    const auto outcome=value->progress.outcome(confirmed,success);
    value->receipt["source_saved"]=outcome.sourceSaved;value->receipt["source_saved_known"]=outcome.sourceSavedKnown;
    value->receipt["uncertain"]=outcome.uncertain;value->receipt["compiled_known"]=confirmed && result["compiled"].isBoolean();value->receipt["compiled"]=confirmed && result["compiled"].asBoolean();
    if(asset.notNull())value->receipt["asset_id"]=asset;
    value->receipt["diagnostics"]=LLSD::emptyArray();std::size_t bytes=0;
    for(const auto& raw:llsd::inArray(result["errors"]))
    {const auto rawText=raw.isMap()?raw["message"].asString():raw.asString();const auto text=fs_content::excerpt(fs_content::utf8(rawText)?rawText:"Compiler returned an invalid UTF-8 diagnostic",1024);if(bytes+text.size()>8192 || value->receipt["diagnostics"].size()>=64){value->receipt["diagnostics_truncated"]=true;break;}value->receipt["diagnostics"].append(llsd::map("message",text));bytes+=text.size();}
    // Remove cached source bytes; the next complete read must fetch the actual
    // saved revision. Never invoke an editor callback with assistant source.
    if(confirmed)LLFileSystem::removeFile(asset,LLAssetType::AT_LSL_TEXT);
    bool localConflict=false;
    if(confirmed && value->source==source() && value->task && fs_assistant::asyncTaskAllowed(value->task) && liveMutation(value))
    {
        std::string error;auto* prim=target(value->request["object_id"].asUUID(),error);
        auto* item=prim?dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(value->request["item_id"].asUUID())):nullptr;
        const auto key=llsd::map("taskid",value->request["object_id"],"itemid",value->request["item_id"]);
        auto* editor=LLFloaterReg::findTypedInstance<LLLiveLSLEditor>("preview_scriptedit",key);
        if(editor && (editor->assistantSourceBusy() || !editor->invalidateAssistantSource(editor->getEditor())))localConflict=true;
        else if(item)prim->updateViewerInventoryAsset(item,asset);
    }
    settle(value,localConflict?"server_confirmed_local_conflict":outcome.state,localConflict?"Native editor changed before cache refresh":reason);
}
void enqueueTaskScript(const std::shared_ptr<Mutation>& value)
{
    const auto origin=value->source;const auto primID=value->request["object_id"].asUUID();std::string error;
    auto* prim=target(primID,error);if(!prim){settle(value,"cancelled_before_submission",error);return;}
    const auto metadata=prim->getRegion()->getCapability("GetMetadata");const auto capability=prim->getRegion()->getCapability("UpdateScriptTask");
    LLCoros::instance().launch("assistantTaskScript",[value,origin,metadata,capability]()
    {
        if(origin!=source() || !fs_assistant::asyncTaskAllowed(value->task) || !liveMutation(value)){settle(value,"cancelled_before_submission","source_or_authority_changed");return;}
        auto adapter=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("AssistantScriptMetadata",LLCore::HttpRequest::DEFAULT_POLICY_ID);
        auto http=std::make_shared<LLCore::HttpRequest>();auto options=std::make_shared<LLCore::HttpOptions>();options->setRetries(0);options->setFollowRedirects(false);
        auto data=llsd::map("object-id",value->request["object_id"],"item-id",value->request["item_id"]);data["fields"]=LLSD::emptyArray();data["fields"].append("experience");
        const auto result=adapter->postAndSuspend(http,metadata,data,options);
        const auto status=LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
        if(!status || (!result["experience"].isUUID() && !result["experience"].isString()) || !LLUUID::validate(result["experience"].asString())){settle(value,"failed","Cannot preserve native Experience association: metadata unavailable");return;}
        if(!value->task || value->finished){settle(value,"cancelled_before_submission","operation_stopped");return;}
        fs_assistant::AsyncScope scope(value->task->guard());
        if(!fs_assistant::asyncAllowed() || origin!=source() || !liveMutation(value)){settle(value,"cancelled_before_submission","source_or_authority_changed");return;}
        bool running=false;std::string error;if(!requestedRunning(value->request,running,error)){settle(value,"cancelled_before_submission",error);return;}
        auto finished=[value](LLUUID,LLUUID,LLUUID,LLSD result){finishTaskUpload(value,result,true,"");};
        auto failed=[value](LLUUID,LLUUID,LLSD result,std::string reason){finishTaskUpload(value,result,false,reason);return true;};
        auto upload=std::make_shared<LLScriptAssetUpload>(value->request["object_id"].asUUID(),value->request["item_id"].asUUID(),LLScriptAssetUpload::MONO,
            running,result["experience"].asUUID(),value->request["text"].asString(),finished,failed);
        value->upload=upload;value->receipt["requested_running"]=running;value->receipt["experience_preserved"]=true;
        upload->setAssistantHooks({[value](const std::string& stage,bool submit)
        {
            const bool allowed=value->progress.before(stage,submit,[&](){return fs_assistant::asyncTaskAllowed(value->task) && liveMutation(value);},[&](){return fs_assistant::asyncTaskSubmit(value->task);});
            if(allowed && stage=="source" && submit){value->submitted=true;value->receipt["state"]="submitted_unknown";value->receipt["source_saved_known"]=false;}
            return allowed;
        },[value](const LLSD& result,const std::string& stage){finishTaskUpload(value,result,false,"Stopped at "+stage);}});
        LLViewerAssetUpload::EnqueueInventoryUpload(capability,upload);
    });
}
LLSD rezCandidates(const std::shared_ptr<Mutation>&);
void dispatchMutation(const LLSD& request)
{
    std::string error;const auto op=request["op"].asString();
    if(op=="scriptStatus")
    {
        auto* prim=target(request["object_id"].asUUID(),error);if(!prim){LLEventAPI::Response errorResponse(LLSD(),request);return errorResponse.error(error);}
        if(service().runningReads.size()>=16){LLEventAPI::Response errorResponse(LLSD(),request);return errorResponse.error("capacity: too many script status readers");}
        auto read=std::make_shared<RunningRead>();read->request=request;read->source=source();read->task=fs_assistant::asyncTask();read->deadline=now()+10.;
        auto* item=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(request["item_id"].asUUID()));if(!scriptReadable(item)){LLEventAPI::Response errorResponse(LLSD(),request);return errorResponse.error("script_not_readable");}
        read->item=itemEvidence(item);service().runningReads.push_back(read);requestRunning(prim,item->getUUID());return;
    }
    if(op=="take" || op=="takeCopy" || op=="rezIdentify"){dispatchLifecycle(request);return;}
    LLEventAPI::Response response(LLSD(),request);
    if(op=="operationStatus")
    {auto value=receiptOperation(request,error);if(!value)return response.error(error);
      if(value->request["op"].asString()=="rezInventory" || value->request["op"].asString()=="rezPrimitive")value->receipt["candidates"]=rezCandidates(value);
      response.setResponse(value->receipt);return;}
    if(auto own=ownMutation(request)){response.setResponse(own->receipt);return;}
    auto* prim=target(request["object_id"].asUUID(),error);if(!prim)return response.error(error);
    if(op=="scriptPreview")
    {auto* value=scriptSnapshot(request,error);if(!value)return response.error(error);const auto diff=fs_content::diff(value->text,request["text"].asString());response.setResponse(llsd::map("before",diff.before,"after",diff.after,"truncated",diff.truncated,"atomic_revision_check",false));return;}
    FSAssistantPreparedOperation prepared;prepared.request=request;if(!prepareMutation(prepared,error))return response.error(error);
    auto value=std::make_shared<Mutation>();value->request=request;value->source=source();value->epoch=request["__assistant_epoch"].asString();value->signature=mutationSignature(request);
    value->key=request["client_request_id"].asString();value->before=objectEvidence(prim);value->item=prepared.validation[op=="put"?"agent_source":"item"];value->task=fs_assistant::asyncTask();value->deadline=now()+25.;
    value->receipt=resultBase();value->receipt["operation_id"]=request["reqid"];value->receipt["client_request_id"]=request["client_request_id"];value->receipt["object_id"]=prim->getID();value->receipt["item_id"]=request["item_id"];value->receipt["state"]="queued";
    auto* entry=service().operations.admit(value->key,value->signature,value);if(!entry)return response.error("capacity_or_request_id_reused: no new operation was submitted");entry->pending=1;
    if(op=="scriptSave"){enqueueTaskScript(value);response.setResponse(value->receipt);return;}
    auto* item=op=="put"?agentSource(request,error):dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(request["item_id"].asUUID()));
    if(!item || !fs_assistant::asyncTaskSubmit(value->task) || !liveMutation(value)){settle(value,"cancelled_before_submission","authority_or_target_changed");response.setResponse(value->receipt);return;}
    if(op=="put" || op=="rename" || op=="remove")
    {
        LLInventoryObject::object_list_t contents;prim->getInventoryContents(contents);for(const auto& old:contents)value->previousItems.insert(old->getUUID().asString());
        value->observe(prim);value->refresh=true;
    }
    value->submitted=true;value->receipt["state"]="submitted_unknown";
    if(op=="put")
    {
        LLPointer<LLViewerInventoryItem> copy=new LLViewerInventoryItem(item);
        if(item->getActualType()==LLAssetType::AT_LSL_TEXT)prim->saveScript(copy,request["running"].asBoolean(),true);
        else prim->updateInventory(copy,TASK_INVENTORY_ITEM_KEY,true);
        value->receipt["source_retained"]=true;
    }
    else if(op=="rename")
    {LLPointer<LLViewerInventoryItem> copy=new LLViewerInventoryItem(item);copy->rename(request["name"].asString());prim->updateInventory(copy,TASK_INVENTORY_ITEM_KEY,false);}
    else if(op=="remove")prim->removeInventory(item->getUUID());
    else
    {
        LLMessageSystem* msg=gMessageSystem;msg->newMessageFast(op=="scriptReset"?_PREHASH_ScriptReset:_PREHASH_SetScriptRunning);
        msg->nextBlockFast(_PREHASH_AgentData);msg->addUUIDFast(_PREHASH_AgentID,gAgent.getID());msg->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_Script);msg->addUUIDFast(_PREHASH_ObjectID,prim->getID());msg->addUUIDFast(_PREHASH_ItemID,item->getUUID());
        if(op=="scriptRunning")msg->addBOOLFast(_PREHASH_Running,request["running"].asBoolean());msg->sendReliable(prim->getRegion()->getHost());
        if(op=="scriptReset")settle(value,"submitted","Native reset has no completion acknowledgment; inspect subsequent output");
        else requestRunning(prim,item->getUUID());
    }
    response.setResponse(value->receipt);
}
void tickMutations()
{
    auto& self=service();
    for(auto it=self.runningReads.begin();it!=self.runningReads.end();)
    {
        auto& read=**it;
        if(!read.done && now()>read.deadline){read.done=true;reply(read.request,llsd::map("error","timeout: running state remains unknown"));}
        if(read.done || read.source!=source() || !fs_assistant::asyncTaskAllowed(read.task) || !fs_assistant_request_allowed(read.request))it=self.runningReads.erase(it);else ++it;
    }
    // Never erase receipts during status polling or while a native result can
    // still arrive. Copy the pointers because callbacks may complete a record.
    std::vector<std::shared_ptr<Mutation>> values;for(const auto& entry:self.operations.entries())values.push_back(entry.second.value);
    for(auto& value:values)
    {
        if(!value || !value->task || value->source!=source())continue;
        if(!fs_assistant::asyncTaskAllowed(value->task))
        {if(!value->submitted)settle(value,"cancelled_before_submission","authority_revoked");else settle(value,"submitted_unknown","authority_revoked_after_submission",true);continue;}
        if(now()>value->deadline)
        {value->task->guard()->cancel("timeout");settle(value,value->submitted?"submitted_unknown":"cancelled_before_submission","timeout: inspect native state; never replay",value->submitted);continue;}
        if(value->refresh && now()>=value->refreshAt)
        {
            std::string error;if(auto* prim=target(value->request["object_id"].asUUID(),error))
            {value->refreshAt=now()+1.;prim->dirtyInventory();prim->requestInventory();}
        }
    }
}
LLSD lifecycleEvidence(LLViewerObject* prim)
{
    auto value=objectEvidence(prim);value["movable"]=prim->permMove();value["copyable"]=prim->permCopy();value["permanent"]=prim->isPermanentEnforced();
    value["position"]=ll_sd_from_vector3(prim->getPositionRegion());value["children"]=LLSD::emptyArray();
    for(auto child:prim->getChildren())value["children"].append(child->getID());
    return value;
}
bool takeEligible(LLViewerObject* prim,const LLSD& request,std::string& error)
{
    if(prim->getRootEdit()!=prim || !prim->permMove() || prim->isPermanentEnforced() || prim->getNVPair("AssetContainer") ||
        (request["op"].asString()=="takeCopy" && !prim->permCopy()) || !rlvCanDeleteOrReturn(prim))
    {error="not_permitted: movable owned root linkset required; take-copy also requires COPY";return false;}
    for(auto child:prim->getChildren())if(child->isAvatar()){error="not_permitted: occupied linksets cannot be taken by the assistant";return false;}
    if(!FSInventoryBulk::ordinaryTaskDestination(request["destination_folder"].asUUID()))
    {error="not_permitted: exact ordinary writable inventory folder outside protected/locked trees required";return false;}
    return true;
}
LLSD rezCandidates(const std::shared_ptr<Mutation>& value)
{
    LLSD result=LLSD::emptyArray();if(value->source!=source())return result;
    const auto position=ll_vector3_from_sd(value->request["position"]);
    for(S32 index=0;index<gObjectList.getNumObjects() && result.size()<64;++index)
    {
        auto* prim=gObjectList.getObject(index);
        if(!prim || prim->isDead() || prim->getRegion()!=gAgent.getRegion() || prim->getPCode()!=LL_PCODE_VOLUME ||
            prim->getRootEdit()!=prim || prim->isAttachment() || !prim->permYouOwner() || value->previousItems.count(prim->getID().asString()) ||
            dist_vec_squared(prim->getPositionRegion(),position)>64.f)continue;
        result.append(llsd::map("object_id",prim->getID(),"position",ll_sd_from_vector3(prim->getPositionRegion()),"association_confirmed",false));
    }
    return result;
}
bool prepareLifecycle(FSAssistantPreparedOperation& operation,std::string& error)
{
    const auto& request=operation.request;const auto op=request["op"].asString();
    auto* prim=target(request["object_id"].asUUID(),error);if(!prim)return false;
    operation.requiredClasses.push_back("read");operation.validation["object"]=objectEvidence(prim);operation.validation["lifecycle"]=lifecycleEvidence(prim);
    if(op=="rezIdentify")
    {
        auto value=receiptOperation(request,error);if(!value)return false;
        if(value->request["op"].asString()!="rezInventory" && value->request["op"].asString()!="rezPrimitive")
        {error="invalid_operation: tracked rez receipt required";return false;}
        if(value->receipt["correlation"].asString()=="explicit_selection")
        {if(value->receipt["object_id"].asUUID()==prim->getID())return true;error="conflict: this rez receipt already has a different selected object";return false;}
        bool found=false;for(const auto& candidate:llsd::inArray(rezCandidates(value)))if(candidate["object_id"].asUUID()==prim->getID())found=true;
        if(!found){error="not_a_candidate: selected owned object was not observed as a new nearby rez candidate";return false;}
    }
    else
    {
        auto* snapshot=contentsSnapshot(request,error);if(!snapshot || snapshot->object["object_id"].asUUID()!=prim->getID() || !takeEligible(prim,request,error))return false;
        if(service().derezTransactions.size()>=4096){error="capacity: native acquisition transaction tombstones full; restart viewer before more takes";return false;}
        operation.validation["destination"]=request["destination_folder"];
    }
    operation.summary=llsd::map("op",op,"subject_count",1,"subjects",LLSD::emptyArray());
    operation.summary["subjects"].append(llsd::map("id",prim->getID(),"name",prim->getID().asString(),"destination",op=="rezIdentify"?"Associate this candidate; server association remains unknown":request["destination_folder"].asString(),
        "after_name",op=="take"?"Take original out of world":op=="takeCopy"?"Take copy; original stays in world":"Select this exact rez candidate"));
    return true;
}
void dispatchLifecycle(const LLSD& request)
{
    LLEventAPI::Response response(LLSD(),request);std::string error;const auto op=request["op"].asString();
    if(auto own=ownMutation(request)){if(own->signature!=mutationSignature(request))return response.error("request_id_reused");response.setResponse(own->receipt);return;}
    FSAssistantPreparedOperation prepared;prepared.request=request;if(!prepareLifecycle(prepared,error))return response.error(error);
    if(op=="rezIdentify")
    {
        auto value=receiptOperation(request,error);if(!value)return response.error(error);
        value->receipt["object_id"]=request["object_id"];value->receipt["object_id_confirmed"]=false;value->receipt["correlation"]="explicit_selection";
        settle(value,"selected_candidate","Explicit association is not simulator proof of which rez created the object");response.setResponse(value->receipt);return;
    }
    auto* prim=target(request["object_id"].asUUID(),error);if(!prim)return response.error(error);
    auto value=std::make_shared<Mutation>();value->request=request;value->source=source();value->epoch=request["__assistant_epoch"].asString();value->key=request["client_request_id"].asString();
    value->signature=mutationSignature(request);value->before=objectEvidence(prim);value->task=fs_assistant::asyncTask();value->deadline=now()+30.;
    value->receipt=resultBase();value->receipt["operation_id"]=request["reqid"];value->receipt["client_request_id"]=request["client_request_id"];
    value->receipt["object_id"]=prim->getID();value->receipt["transaction_id"]=LLUUID::generateNewID();value->receipt["destination_folder"]=request["destination_folder"];
    value->receipt["state"]="queued";value->receipt["source_retained"]=op=="takeCopy";value->receipt["inventory_item_confirmed"]=false;
    auto* entry=service().operations.admit(value->key,value->signature,value);if(!entry)return response.error("capacity_or_request_id_reused: nothing submitted");entry->pending=1;
    if(!fs_assistant::asyncTaskSubmit(value->task) || !takeEligible(prim,request,error))
    {settle(value,"cancelled_before_submission",error);response.setResponse(value->receipt);return;}
    service().derezTransactions[value->receipt["transaction_id"].asUUID()]=prim->getRegion()->getHost();
    LLMessageSystem* msg=gMessageSystem;msg->newMessageFast(_PREHASH_DeRezObject);
    msg->nextBlockFast(_PREHASH_AgentData);msg->addUUIDFast(_PREHASH_AgentID,gAgent.getID());msg->addUUIDFast(_PREHASH_SessionID,gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_AgentBlock);msg->addUUIDFast(_PREHASH_GroupID,gAgent.getGroupID());
    msg->addU8Fast(_PREHASH_Destination,static_cast<U8>(op=="take"?DRD_TAKE_INTO_AGENT_INVENTORY:DRD_ACQUIRE_TO_AGENT_INVENTORY));
    msg->addUUIDFast(_PREHASH_DestinationID,request["destination_folder"].asUUID());msg->addUUIDFast(_PREHASH_TransactionID,value->receipt["transaction_id"].asUUID());
    msg->addU8Fast(_PREHASH_PacketCount,1);msg->addU8Fast(_PREHASH_PacketNumber,0);
    msg->nextBlockFast(_PREHASH_ObjectData);msg->addU32Fast(_PREHASH_ObjectLocalID,prim->getLocalID());
    value->submitted=true;value->receipt["state"]="submitted_unknown";msg->sendReliable(prim->getRegion()->getHost());response.setResponse(value->receipt);
}
}
void fs_initialize_object_tools(){static ObjectListener listener;}
void fs_object_tools_tick()
{
    syncService();tickMutations();auto& reads=service().reads;
    for(auto it=reads.begin();it!=reads.end();)
    {
        auto& read=**it;
        if(!read.done && now()>read.deadline){read.done=true;reply(read.request,llsd::map("error","timeout: simulator Contents unavailable"));}
        if(read.done || read.source!=source() || !fs_assistant::asyncTaskAllowed(read.task) || !fs_assistant_request_allowed(read.request))it=reads.erase(it);else ++it;
    }
}
void fs_object_tools_privacy_changed()
{auto& self=service();self.snapshots.clear();self.scripts.clear();self.diagnostics.clear();self.cursors.clear();self.running.clear();}
void fs_object_tools_debug(const LLChat& chat)
{
    syncService();if(!ready() || FSEventAPIBridge::getPermissionLevel("script_debug_read")!="allow" ||
        gRlvHandler.hasBehaviour(RLV_BHVR_VIEWSCRIPT) || chat.mMuted || chat.mSourceType!=CHAT_SOURCE_OBJECT ||
        (chat.mChatType!=CHAT_TYPE_DEBUG_MSG && chat.mChatType!=CHAT_TYPE_OWNER) || chat.mOwnerID!=gAgent.getID())return;
    std::string error;auto* prim=target(chat.mFromID,error);if(!prim)return;
    auto event=llsd::map("object_id",prim->getID(),"root_id",prim->getRootEdit()->getID(),"owner_id",chat.mOwnerID,
        "object_name",fs_content::excerpt(chat.mFromName,255),"text",fs_content::excerpt(chat.mText,8192),
        "native_type",static_cast<S32>(chat.mChatType),"kind",chat.mChatType==CHAT_TYPE_DEBUG_MSG?"runtime_error":"owner_say",
        "timestamp",chat.mTime,"script_identity_known",false,"truncated",chat.mText.size()>8192);
    auto& journal=service().diagnostics;event["sequence"]=std::to_string(journal.latest()+1);
    const auto bytes=chat.mFromName.size()+std::min<std::size_t>(8192,chat.mText.size())+512;journal.append(std::move(event),bytes);
}
void fs_object_tools_running_reply(LLMessageSystem* msg)
{
    if(!ready() || msg->getSender()!=gAgent.getRegion()->getHost())return;
    LLUUID object,itemID;bool running=false,mono=false;
    msg->getUUIDFast(_PREHASH_Script,_PREHASH_ObjectID,object);msg->getUUIDFast(_PREHASH_Script,_PREHASH_ItemID,itemID);
    msg->getBOOLFast(_PREHASH_Script,_PREHASH_Running,running);msg->getBOOLFast(_PREHASH_Script,"Mono",mono);
    std::string error;auto* prim=target(object,error);if(!prim)return;
    auto* item=dynamic_cast<LLViewerInventoryItem*>(prim->getInventoryObject(itemID));if(!scriptReadable(item))return;
    for(auto& read:service().runningReads)
    {
        if(read->done || read->source!=source() || read->request["object_id"].asUUID()!=object || read->request["item_id"].asUUID()!=itemID)continue;
        read->done=true;fs_assistant::AsyncScope scope(read->task->guard());
        if(!fs_assistant::asyncAllowed() || !fs_assistant_request_allowed(read->request) || !llsd_equals(read->item,itemEvidence(item)))continue;
        Running state{source(),read->request["__assistant_epoch"].asString(),LLUUID::generateNewID(),item->getAssetUUID(),running,mono,now()};
        if(service().running.size()>=128)service().running.erase(service().running.begin());
        service().running[{object,itemID}]=state;
        auto result=resultBase();result["object_id"]=object;result["item_id"]=itemID;result["running"]=running;result["mono"]=mono;
        result["running_snapshot"]=state.token;result["observed"]=true;result["atomic_running_check"]=false;reply(read->request,result);
    }
    for(const auto& entry:service().operations.entries())
    {
        auto value=entry.second.value;
        if(!value || !entry.second.pending || !value->submitted || value->source!=source() || value->request["op"].asString()!="scriptRunning" ||
            value->request["object_id"].asUUID()!=object || value->request["item_id"].asUUID()!=itemID)continue;
        value->receipt["observed_running"]=running;
        settle(value,running==value->request["running"].asBoolean()?"simulator_observed":"simulator_observed_different","Native running reply is an observation, not an execution acknowledgment");
    }
}
void fs_object_tools_inventory_transaction(LLMessageSystem* msg)
{
    if(!ready() || msg->getSender()!=gAgent.getRegion()->getHost())return;
    LLUUID transaction,agent;msg->getUUIDFast(_PREHASH_AgentData,_PREHASH_TransactionID,transaction);
    msg->getUUIDFast(_PREHASH_AgentData,_PREHASH_AgentID,agent);if(agent!=gAgent.getID() || transaction.isNull())return;
    for(const auto& entry:service().operations.entries())
    {
        auto value=entry.second.value;if(!value || value->source!=source() || !value->submitted || value->receipt["transaction_id"].asUUID()!=transaction)continue;
        std::set<LLUUID> matching;
        for(S32 block=0;block<msg->getNumberOfBlocksFast(_PREHASH_ItemData);++block)
        {
            LLUUID itemID;msg->getUUIDFast(_PREHASH_ItemData,_PREHASH_ItemID,itemID,block);
            auto* item=gInventory.getItem(itemID);
            if(item && item->getActualType()==LLAssetType::AT_OBJECT && !item->getIsLinkType() && item->getPermissions().getOwner()==gAgent.getID() &&
                item->getParentUUID()==value->request["destination_folder"].asUUID())matching.insert(itemID);
        }
        for(const auto& item:matching)value->acquisition.observe(item.asString());
        if(value->acquisition.ambiguous())
        {value->receipt["correlation"]="ambiguous_transaction";value->receipt["inventory_item_confirmed"]=false;value->receipt.erase("inventory_item_id");continue;}
        if(matching.empty())continue;
        value->receipt["inventory_item_id"]=*matching.begin();value->receipt["inventory_item_confirmed"]=true;value->receipt["correlation"]="native_transaction";
        if(value->task && value->task->guard())value->task->guard()->acknowledge(matching.begin()->asString());
        if(value->acquisition.confirmed())settle(value,"server_confirmed","Native acquisition transaction and inventory arrival observed");
        else value->receipt["state"]=value->acquisition.rejected()?"server_result_conflict":"inventory_confirmed_ack_pending";
    }
}
bool fs_object_tools_derez_reply(LLMessageSystem* msg)
{
    LLUUID transaction;bool success=false;msg->getUUIDFast(_PREHASH_TransactionData,_PREHASH_TransactionID,transaction);msg->getBOOLFast(_PREHASH_TransactionData,_PREHASH_Success,success);
    const auto known=service().derezTransactions.find(transaction);
    if(known==service().derezTransactions.end() || known->second!=msg->getSender())return false;
    // These exact-target operations do not increment native selection busy
    // counters. Consume their late acknowledgments even after a login changes.
    for(const auto& entry:service().operations.entries())
    {
        auto value=entry.second.value;if(!value || value->source!=source() || value->receipt["transaction_id"].asUUID()!=transaction)continue;
        value->receipt["native_accepted"]=success;value->acquisition.acknowledge(success);
        if(!success)settle(value,"server_rejected","Simulator rejected the acquisition");
        else if(value->acquisition.confirmed())settle(value,"server_confirmed","Native acquisition transaction and inventory arrival observed");
        else value->receipt["state"]="simulator_accepted_inventory_pending";
    }
    return true;
}
bool fs_object_tools_submit_rez(const LLSD& request,const std::function<void()>& submit,LLSD& result,std::string& error)
{
    syncService();
    if(!id(request["client_request_id"]) || !fs_assistant::AsyncScope::current() || !ready())
    {error="invalid_args: tracked rez needs an exact source, request UUID and native authority";return false;}
    if(auto own=ownMutation(request))
    {if(own->signature!=mutationSignature(request)){error="request_id_reused";return false;}result=own->receipt;return true;}
    auto value=std::make_shared<Mutation>();value->request=request;value->source=source();value->epoch=request["__assistant_epoch"].asString();value->key=request["client_request_id"].asString();
    value->signature=mutationSignature(request);value->task=fs_assistant::asyncTask();value->deadline=now()+30.;
    const auto position=ll_vector3_from_sd(request["position"]);
    for(S32 index=0;index<gObjectList.getNumObjects();++index)
    {
        auto* prim=gObjectList.getObject(index);
        if(prim && !prim->isDead() && prim->getRegion()==gAgent.getRegion() && prim->getPCode()==LL_PCODE_VOLUME && prim->getRootEdit()==prim &&
            !prim->isAttachment() && prim->permYouOwner() && dist_vec_squared(prim->getPositionRegion(),position)<=64.f)value->previousItems.insert(prim->getID().asString());
        if(value->previousItems.size()>512){error="capacity: too many nearby existing roots for tracked rez";return false;}
    }
    value->receipt=resultBase();value->receipt["operation_id"]=request["reqid"];value->receipt["client_request_id"]=request["client_request_id"];
    value->receipt["state"]="queued";value->receipt["object_id_confirmed"]=false;value->receipt["correlation"]="unconfirmed";value->receipt["candidates"]=LLSD::emptyArray();
    auto* entry=service().operations.admit(value->key,value->signature,value);if(!entry){error="capacity_or_retired_request_id: no rez submitted";return false;}entry->pending=1;
    if(!fs_assistant::asyncTaskSubmit(value->task)){settle(value,"cancelled_before_submission","authority_revoked");result=value->receipt;return true;}
    value->submitted=true;value->receipt["state"]="submitted_unknown";submit();result=value->receipt;return true;
}
