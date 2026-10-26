/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsobjecttools.h"
#include "fsobjecttoolsmodel.h"
#include "fsassistantfeature.h"
#include "fscontentpermissions.h"
#include "fscontentpolicy.h"
#include "fseventapibridge.h"
#include "fssessionworker.h"
#include "llassetstorage.h"
#include "llapp.h"
#include "llchat.h"
#include "lleventapi.h"
#include "llfilesystem.h"
#include "llsdserialize.h"
#include "llstartup.h"
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
    if(!RlvActions::canEdit(object))
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
{LLSD value;item->asLLSD(value);return value;}
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
struct Service
{
    std::string source;std::map<LLUUID,Snapshot> snapshots;std::map<LLUUID,SourceSnapshot> scripts;
    std::vector<std::shared_ptr<ContentsRead>> reads;
    fs_object_tools::Journal<LLSD> diagnostics;
    std::map<std::string,std::uint64_t> cursors;
};
Service& service(){static Service value;return value;}
void syncService()
{
    auto& self=service();const auto current=source();
    if(self.source==current)return;
    self.source=current;self.snapshots.clear();self.scripts.clear();self.cursors.clear();self.diagnostics.clear();
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
    return true;
}
class ObjectListener:public LLEventAPI,public FSAssistantFeature
{
public:
    ObjectListener():LLEventAPI("FSObjects","Exact-prim script development and diagnostics")
    {for(const char* op:{"contents","scriptGet","debugPoll"})add(op,"Source-bound object tool",&ObjectListener::dispatch);}
    const char* assistantCapability()const override{return "task_inventory";}
    LLSD assistantCapabilities()const override{return llsd::map("task_inventory",1,"script_diagnostics",1);}
    LLSD assistantLimits()const override{return llsd::map("contents_items",4096,"source_bytes",65536,"diagnostic_events",2048,"diagnostic_bytes",2097152,"atomic_revision_check",false);}
    bool prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error)const override
    {
        syncService();const auto& request=operation.request;if(!ready() || !arguments(request,error))return false;
        operation.validation=llsd::map("source",source());
        const auto op=request["op"].asString();
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
}
void fs_initialize_object_tools(){static ObjectListener listener;}
void fs_object_tools_tick()
{
    syncService();auto& reads=service().reads;
    for(auto it=reads.begin();it!=reads.end();)
    {
        auto& read=**it;
        if(!read.done && now()>read.deadline){read.done=true;reply(read.request,llsd::map("error","timeout: simulator Contents unavailable"));}
        if(read.done || read.source!=source() || !fs_assistant::asyncTaskAllowed(read.task) || !fs_assistant_request_allowed(read.request))it=reads.erase(it);else ++it;
    }
}
void fs_object_tools_privacy_changed()
{auto& self=service();self.snapshots.clear();self.scripts.clear();self.diagnostics.clear();self.cursors.clear();}
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
