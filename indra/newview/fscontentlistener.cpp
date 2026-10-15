/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fscontentlistener.h"
#include "fsassistantfeature.h"
#include "fscontentpermissions.h"
#include "fscontentpolicy.h"
#include "fscontentupload.h"
#include "llassetstorage.h"
#include "lleventapi.h"
#include "llfilesystem.h"
#include "llfloaterreg.h"
#include "llnotecard.h"
#include "llpreviewnotecard.h"
#include "llpreviewscript.h"
#include "llscripteditor.h"
#include "lltexteditor.h"
#include "llviewerassetupload.h"
#include "llviewerregion.h"
#include <map>
#include <memory>
#include <sstream>

namespace {
using namespace fs_content;
struct Snapshot
{
    LLSD authority,epoch;
    std::string text,raw,prefix,suffix,hash;
    bool notecard=false,editable=true;
};
struct Write
{
    LLSD request,authority,receipt;
    fs_assistant::AsyncTaskPtr task;
    std::string signature;
    std::weak_ptr<LLResourceUploadInfo> upload;
    UploadProgress progress;
    bool notecard=true;
};
std::map<LLUUID,Snapshot> sSnapshots;
std::map<LLUUID,std::shared_ptr<Write>> sWrites;
LLSD sIdentity;
void resetSession()
{
    const auto now=identity();
    if(sIdentity!=now){sSnapshots.clear();sWrites.clear();sIdentity=now;}
}
LLSD diffValue(const std::string& before,const std::string& after)
{
    const auto value=diff(before,after);
    return llsd::map("offset_bytes",static_cast<S32>(value.prefix),"removed_bytes",static_cast<S32>(value.removed),
        "added_bytes",static_cast<S32>(value.added),"before",value.before,"after",value.after,"truncated",value.truncated);
}
std::string signature(const LLSD& request)
{
    return hash(request["id"].asString()+"\n"+request["expected_asset_id"].asString()+"\n"+
        request["content_hash"].asString()+"\n"+hash(request["text"].asString())+"\n"+request["op"].asString()+"\n"+request["compiler"].asString());
}
bool editorBusy(const LLUUID& id,bool notecard,const LLResourceUploadInfo* own=nullptr)
{
    if(LLViewerAssetUpload::hasPendingInventoryUpload(id,own))return true;
    LLPreview* preview=nullptr;
    LLTextEditor* editor=nullptr;
    if(notecard)
    {
        auto* card=LLFloaterReg::findTypedInstance<LLPreviewNotecard>("preview_notecard",id);
        preview=card;if(card)editor=card->getEditor();
    }
    else
    {
        auto* script=LLFloaterReg::findTypedInstance<LLPreviewLSL>("preview_script",id);
        preview=script;if(script)editor=script->getEditor();
    }
    // Visible editors can have pending external/preprocessor save state that
    // pristine text alone does not expose. Never invoke their save callbacks.
    return preview && (preview->getVisible() || preview->getAssetStatus()==LLPreview::PREVIEW_ASSET_LOADING ||
        !editor || !editor->isPristine());
}
bool invalidateHiddenEditor(const std::shared_ptr<Write>& write)
{
    const LLUUID id=write->request["id"].asUUID();
    if(write->notecard)
    {
        auto* preview=LLFloaterReg::findTypedInstance<LLPreviewNotecard>("preview_notecard",id);
        return !preview || preview->invalidateAssistantSource(preview->getEditor());
    }
    auto* preview=LLFloaterReg::findTypedInstance<LLPreviewLSL>("preview_script",id);
    return !preview || preview->invalidateAssistantSource(preview->getEditor());
}
bool target(const LLSD& request,bool& notecard,LLViewerInventoryItem*& item,std::string& error)
{
    resetSession();item=gInventory.getItem(request["id"].asUUID());
    notecard=item && item->getType()==LLAssetType::AT_NOTECARD;
    if(!readable(item,notecard)){error="Exact native-readable notecard or script item is required; resolve inventory links first";return false;}
    return true;
}
bool input(const LLSD& request,std::string& error)
{
    if(!request["text"].isString() || request["text"].asString().size()>textLimit || !utf8(request["text"].asString()))
    {error="text must be complete UTF-8 without NUL, at most 65536 bytes";return false;}
    if(request["expected_asset_id"].asUUID().isNull() || !request["content_hash"].isString() || request["content_hash"].asString().size()!=64)
    {error="expected_asset_id and the complete read's content_hash are required";return false;}
    return true;
}
bool baseSnapshot(const LLSD& request,Snapshot*& snapshot,LLViewerInventoryItem*& item,std::string& error,bool write)
{
    bool notecard=false;if(!target(request,notecard,item,error) || !input(request,error))return false;
    const auto found=sSnapshots.find(item->getUUID());
    if(found==sSnapshots.end() || !sameSourceEpoch(found->second.epoch.asString(),request["__assistant_epoch"].asString()) || found->second.authority!=evidence(item) ||
       found->second.hash!=request["content_hash"].asString() || item->getAssetUUID()!=request["expected_asset_id"].asUUID())
    {error="Revision conflict or snapshot expired; obtain a fresh complete inventory_content_get";return false;}
    snapshot=&found->second;
    if(!snapshot->editable){error="Version 1 notecard reading is supported; lossless legacy editing is unavailable";return false;}
    if(notecard && !preserveEmbedded(snapshot->text,request["text"].asString()))
    {error="Embedded inventory markers must be preserved in their original order";return false;}
    if(notecard && replaceNotecard(snapshot->prefix,snapshot->suffix,request["text"].asString()).size()>assetLimit)
    {error="Serialized notecard exceeds the 262144 byte asset limit";return false;}
    if(!notecard && request["compiler"].asString()!="mono")
    {error="Script source uses the explicit native mono compiler; set compiler to mono";return false;}
    if(write && (!writable(item,notecard) || (notecard!=(request["op"].asString()=="saveNotecard"))))
    {error="Write requires an existing owned agent-inventory item with native COPY and MODIFY permissions of the requested type";return false;}
    if(write && (!gAgent.getRegion() || gAgent.getRegion()->getCapability(notecard?"UpdateNotecardAgentInventory":"UpdateScriptAgent").empty()))
    {error="This region does not provide the required native inventory update capability";return false;}
    if(write && editorBusy(item->getUUID(),notecard))
    {error="Busy: close the native editor and wait for its pending inventory upload to finish";return false;}
    return true;
}
std::shared_ptr<Write> ownWrite(const LLSD& request)
{
    const auto key=signature(request);
    for(const auto& entry:sWrites)
        if(entry.second->signature==key && sameSourceEpoch(entry.second->request["__assistant_epoch"].asString(),request["__assistant_epoch"].asString()))
            return entry.second;
    return {};
}
bool liveWrite(const std::shared_ptr<Write>& write)
{
    if(write->authority["account"]!=gAgent.getID() || write->authority["session"]!=gAgent.getSessionID() ||
       write->authority["grid"].asString()!=LLGridManager::instance().getGrid())return false;
    auto* item=gInventory.getItem(write->request["id"].asUUID());
    const auto upload=write->upload.lock();
    if(!writable(item,write->notecard) || editorBusy(item->getUUID(),write->notecard,upload.get()))return false;
    auto expected=write->authority;
    if(write->receipt["source_saved"].asBoolean())expected["asset_id"]=write->receipt["asset_id"];
    return evidence(item)==expected;
}
LLSD receiptFor(const LLSD& request,std::string& error)
{
    resetSession();const auto found=sWrites.find(request["operation_id"].asUUID());
    if(found==sWrites.end() || !sameSourceEpoch(found->second->request["__assistant_epoch"].asString(),request["__assistant_epoch"].asString()) ||
       found->second->authority["session"]!=gAgent.getSessionID())
    {error="Content operation belongs to an expired connection or different source";return LLSD();}
    const auto& write=found->second;
    auto* item=gInventory.getItem(write->request["id"].asUUID());
    if(!readable(item,write->notecard) || !agentInventory(item->getUUID()) ||
       item->getPermissions().getOwner()!=write->authority["owner"].asUUID())
    {error="Native content read authority changed; private operation details are unavailable";return LLSD();}
    LLSD result=write->receipt;
    result["current_asset_id"]=item->getAssetUUID();
    result["post_submit_conflict"]=write->progress.sourceSubmitted() &&
        item->getAssetUUID()!=(result["source_saved"].asBoolean()?result["asset_id"].asUUID():result["previous_asset_id"].asUUID());
    return result;
}
void complete(const std::shared_ptr<Write>& write,const LLSD& result,bool success,const std::string& reason)
{
    if(!write->progress.finish())return;
    const LLUUID asset=result["new_asset"].asUUID();
    const bool confirmed=write->progress.sourceSubmitted() && asset.notNull() && result["state"].asString()=="complete";
    auto& receipt=write->receipt;
    const auto outcome=write->progress.outcome(confirmed,success);
    receipt["state"]=outcome.state;receipt["source_saved"]=outcome.sourceSaved;
    receipt["source_saved_known"]=outcome.sourceSavedKnown;
    receipt["uncertain"]=outcome.uncertain;receipt["reason"]=excerpt(reason,1024);
    if(asset.notNull()){receipt["asset_id"]=asset;if(write->task->guard())write->task->guard()->acknowledge(asset.asString());}
    if(!write->notecard)
    {
        receipt["compiled_known"]=confirmed && result["compiled"].isBoolean();
        receipt["compiled"]=confirmed && result["compiled"].asBoolean();
        receipt["diagnostics"]=LLSD::emptyArray();
        receipt["diagnostics_unavailable"]=confirmed && !result["compiled"].asBoolean() && !result["errors"].isArray();
        std::size_t remaining=8192;
        for(const auto& entry:llsd::inArray(result["errors"]))
        {
            if(!remaining || receipt["diagnostics"].size()>=64){receipt["diagnostics_truncated"]=true;break;}
            const auto raw=entry.isMap()?entry["message"].asString():entry.asString();
            const auto sanitized=utf8(raw)?raw:std::string("Compiler returned an invalid UTF-8 diagnostic");
            const auto message=excerpt(sanitized,std::min<std::size_t>(1024,remaining));
            LLSD row;row["message"]=message;
            auto location=diagnosticLocation(message);
            if(entry.isMap() && entry["line"].isInteger() && entry["column"].isInteger() &&
                entry["line"].asInteger()>=0 && entry["column"].asInteger()>=0)
                location={entry["line"].asInteger(),entry["column"].asInteger(),true};
            if(location.known){row["line"]=location.line;row["column"]=location.column;row["coordinates"]="native_compiler";}
            receipt["diagnostics"].append(row);remaining-=std::min(remaining,message.size());
            if(message.size()<raw.size())receipt["diagnostics_truncated"]=true;
            if(!utf8(raw))receipt["diagnostics_unavailable"]=true;
        }
    }
    // Late outcomes remain in source-bound memory even if HTTP delivery expired.
    // No private source text is included in the receipt.
    const bool deliver=fs_assistant::asyncTaskAllowed(write->task) && fs_assistant_request_allowed(write->request);
    if(deliver && confirmed && success && !invalidateHiddenEditor(write))
    {receipt["state"]="server_confirmed_local_conflict";receipt["uncertain"]=true;receipt["reason"]="Native editor ownership changed before cache invalidation";}
    if(deliver)sendReply(receipt,write->request);
    write->task.reset();
}
struct Read
{
    LLSD request,authority;bool notecard=false;fs_assistant::AsyncTaskPtr task;
};
void loaded(const LLUUID&,LLAssetType::EType,void* userdata,S32 status,LLExtStat)
{
    std::unique_ptr<Read> read(static_cast<Read*>(userdata));
    fs_assistant::AsyncScope scope(read->task->guard());
    if(!read->task->claim("read"))return;
    auto* item=gInventory.getItem(read->request["id"].asUUID());
    if(!fs_assistant::asyncAllowed() || !fs_assistant_request_allowed(read->request) || !readable(item,read->notecard) || evidence(item)!=read->authority)
    {sendReply(llsd::map("error","Content authority or item revision changed; read again"),read->request);return;}
    if(status){sendReply(llsd::map("error","Native item-bound asset download failed"),read->request);return;}
    LLFileSystem file(item->getAssetUUID(),item->getType());const auto size=file.getSize();
    if(size<0 || static_cast<std::size_t>(size)>assetLimit)
    {sendReply(llsd::map("error","Asset exceeds the bounded content read limit"),read->request);return;}
    Snapshot snapshot;snapshot.authority=read->authority;snapshot.epoch=read->request["__assistant_epoch"];snapshot.notecard=read->notecard;snapshot.raw.resize(size);
    if(size && !file.read(reinterpret_cast<U8*>(&snapshot.raw[0]),size))
    {sendReply(llsd::map("error","Incomplete asset read"),read->request);return;}
    if(!snapshot.raw.empty() && snapshot.raw.back()=='\0')snapshot.raw.pop_back();
    if(read->notecard)
    {
        LLNotecard card;std::stringstream stream(snapshot.raw);
        if(!card.importStream(stream) || stream.tellg()<0 ||
           !notecardLayout(snapshot.raw,card.getText(),static_cast<std::size_t>(stream.tellg()),snapshot.prefix,snapshot.suffix,card.getVersion()) ||
           !notecardReadText(card.getText(),card.getVersion(),card.getItems().size(),snapshot.text))
        {sendReply(llsd::map("error","Malformed or truncated notecard; no editable snapshot was retained"),read->request);return;}
        snapshot.editable=card.getVersion()==2;
    }
    else snapshot.text=snapshot.raw;
    if(!utf8(snapshot.text))
    {sendReply(llsd::map("error","Source is not valid UTF-8 text"),read->request);return;}
    const bool full=snapshot.text.size()<=textLimit;snapshot.hash=hash(snapshot.raw);
    LLSD reply=llsd::map("ok",true,"id",item->getUUID(),"asset_id",item->getAssetUUID(),
        "kind",read->notecard?"notecard":"script","text",excerpt(snapshot.text,textLimit),"complete",full,"truncated",!full,
        "content_hash",full?snapshot.hash:std::string(),"native_readable",true,"native_writable",writable(item,read->notecard));
    reply["editable"]=full && snapshot.editable && writable(item,read->notecard);
    if(!snapshot.editable)reply["edit_unavailable_reason"]="Version 1 notecard reading is supported; lossless legacy editing is unavailable";
    reply["embedded_inventory"]=read->notecard && snapshot.raw.find("count 0\n")==std::string::npos;
    if(full)
    {
        resetSession();if(sSnapshots.size()>=8)sSnapshots.erase(sSnapshots.begin());
        sSnapshots[item->getUUID()]=std::move(snapshot);
    }
    sendReply(reply,read->request);
}
class FSContentListener : public LLEventAPI,public FSAssistantFeature
{
public:
    FSContentListener():LLEventAPI("FSContent","Revision-checked agent inventory source access")
    {for(const char* op:{"get","preview","saveNotecard","saveScript"})add(op,"Inventory content service",&FSContentListener::dispatch);}
    const char* assistantCapability() const override{return "content_editing";}
    LLSD assistantLimits() const override
    {return llsd::map("text_bytes",static_cast<S32>(textLimit),"asset_bytes",static_cast<S32>(assetLimit),"diff_bytes",4096,"diagnostic_bytes",8192,"snapshots",8,"receipts",32,"atomic_revision_check",false,"compiler","mono","preprocessing",false);}
    bool prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error) const override
    {
        const auto& request=operation.request;const auto op=request["op"].asString();
        if(op=="get" && request.has("operation_id"))
        {const auto receipt=receiptFor(request,error);if(!error.empty())return false;operation.validation=identity();operation.validation["receipt_id"]=request["operation_id"];return true;}
        bool notecard=false;LLViewerInventoryItem* item=nullptr;
        if(!target(request,notecard,item,error))return false;
        operation.validation=evidence(item);
        if(op=="get")return true;
        Snapshot* snapshot=nullptr;
        if(!baseSnapshot(request,snapshot,item,error,op!="preview"))return false;
        operation.validation["content_hash"]=snapshot->hash;operation.validation["after_hash"]=hash(request["text"].asString());
        operation.validation["compiler"]=request["compiler"];
        operation.summary=llsd::map("op",op,"subject_count",1,"subjects",LLSD::emptyArray(),"diff",diffValue(snapshot->text,request["text"].asString()),
            "atomic_revision_check",false,"source_bytes",static_cast<S32>(request["text"].asString().size()));
        operation.summary["subjects"].append(llsd::map("id",item->getUUID(),"name",item->getName(),"after_name","Replace inventory source"));
        operation.summary["consequences"]=LLSD::emptyArray();operation.summary["consequences"].append("content_replace_source");
        if(op!="preview" && std::find(operation.requiredClasses.begin(),operation.requiredClasses.end(),"read")==operation.requiredClasses.end())operation.requiredClasses.push_back("read");
        return true;
    }
    bool validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error) const override
    {
        const auto& request=operation.request;
        if(request["op"].asString()=="get" && request.has("operation_id"))
        {receiptFor(request,error);return error.empty() && operation.validation["session"]==gAgent.getSessionID();}
        const auto own=ownWrite(request);
        if(own && own->authority["asset_id"]==operation.validation["asset_id"])
        {if(liveWrite(own))return true;error="Content source, editor ownership or permissions changed";return false;}
        FSAssistantPreparedOperation current;current.request=request;
        if(!prepareAssistant(current,error))return false;
        if(current.validation!=operation.validation){error="Content revision changed; request a fresh preview and approval";return false;}
        return true;
    }
    void dispatch(const LLSD& request)
    {
        if(!fs_assistant_request_allowed(request))
        {Response response(LLSD(),request);return response.error("Assistant request expired or its session/permission changed");}
        fs_assistant::AsyncScope scope(fs_assistant_async_context(request));
        if(!fs_assistant::AsyncScope::current())
        {Response response(LLSD(),request);return response.error("Content access requires a durable assistant operation");}
        const auto op=request["op"].asString();std::string error;
        if(op=="get" && request.has("operation_id"))
        {const auto receipt=receiptFor(request,error);Response response(LLSD(),request);if(!error.empty())return response.error(error);response.setResponse(receipt);return;}
        bool notecard=false;LLViewerInventoryItem* item=nullptr;
        if(!target(request,notecard,item,error))
        {Response response(LLSD(),request);return response.error(error);}
        if(op=="get")
        {
            if(!gAssetStorage){Response response(LLSD(),request);return response.error("Asset storage is unavailable");}
            auto* read=new Read{request,evidence(item),notecard,fs_assistant::asyncTask()};
            gAssetStorage->getInvItemAsset(LLHost(),gAgent.getID(),gAgent.getSessionID(),item->getPermissions().getOwner(),
                LLUUID::null,item->getUUID(),item->getAssetUUID(),item->getType(),loaded,read,true);return;
        }
        Snapshot* snapshot=nullptr;
        if(!baseSnapshot(request,snapshot,item,error,op!="preview"))
        {Response response(LLSD(),request);return response.error(error);}
        if(op=="preview")
        {Response response(LLSD(),request);response.setResponse(llsd::map("id",item->getUUID(),"asset_id",item->getAssetUUID(),"content_hash",snapshot->hash,"diff",diffValue(snapshot->text,request["text"].asString()),"atomic_revision_check",false));return;}
        if(op!="saveNotecard" && op!="saveScript")
        {Response response(LLSD(),request);return response.error("Unsupported content mutation");}
        save(request,*snapshot,item);
    }
    void save(const LLSD& request,const Snapshot& snapshot,LLViewerInventoryItem* item)
    {
        for(auto it=sWrites.begin();it!=sWrites.end() && sWrites.size()>=32;)
            if(it->second->progress.finished())it=sWrites.erase(it);else ++it;
        if(sWrites.size()>=32){Response response(LLSD(),request);return response.error("Too many pending content operations");}
        auto write=std::make_shared<Write>();write->request=request;write->authority=evidence(item);write->notecard=snapshot.notecard;
        write->task=fs_assistant::asyncTask();write->signature=signature(request);
        const auto buffer=snapshot.notecard?replaceNotecard(snapshot.prefix,snapshot.suffix,request["text"].asString()):request["text"].asString();
        write->receipt=llsd::map("operation_id",request["reqid"],"id",item->getUUID(),"previous_asset_id",item->getAssetUUID(),"asset_id",item->getAssetUUID(),
            "state","queued","source_saved",false,"source_saved_known",true,"uncertain",false,"atomic_revision_check",false,
            "content_hash",hash(buffer));
        sWrites[request["reqid"].asUUID()]=write;
        auto finished=[write](LLUUID,LLUUID,LLUUID,LLSD result){complete(write,result,true,"");};
        auto failed=[write](LLUUID,LLUUID,LLSD result,std::string reason){complete(write,result,false,reason);return true;};
        LLResourceUploadInfo::ptr_t upload;
        if(snapshot.notecard)
            upload=std::make_shared<LLBufferedAssetUploadInfo>(item->getUUID(),LLAssetType::AT_NOTECARD,buffer,finished,failed);
        else
        {
            // Inventory LLScriptAssetUpload selects native mono, accepts raw
            // source, and never reads editor/preprocessor or global settings.
            upload=std::make_shared<LLScriptAssetUpload>(item->getUUID(),buffer,finished,failed);
            write->receipt["compiler"]="mono";write->receipt["compiled"]=false;write->receipt["compiled_known"]=false;
        }
        write->upload=upload;
        upload->setAssistantHooks({[write](const std::string& stage,bool submit)
        {
            if(!write->progress.before(stage,submit,[&](){return fs_assistant::asyncTaskAllowed(write->task) && liveWrite(write);},
                [&](){return fs_assistant::asyncTaskSubmit(write->task);}))return false;
            if(stage=="source" && submit){write->receipt["state"]="submitted_unknown";write->receipt["source_saved_known"]=false;write->receipt["uncertain"]=true;}
            return true;
        },[write](const LLSD& result,const std::string& stage){complete(write,result,false,"Authority, item revision or editor ownership changed at "+stage);}});
        const auto url=gAgent.getRegion()->getCapability(snapshot.notecard?"UpdateNotecardAgentInventory":"UpdateScriptAgent");
        LLViewerAssetUpload::EnqueueInventoryUpload(url,upload);
    }
};
}
void fs_initialize_content_api(){static FSContentListener listener;}
