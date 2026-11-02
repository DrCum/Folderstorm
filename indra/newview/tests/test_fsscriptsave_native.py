"""Production script-save metadata, price gate and upload-lease regressions.

Transport, native inventory/editors and LLSD are bounded fixtures; production
async guards and upload progress are used. No live viewer/server calls occur.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
from test_fsobjecttools_native import function
from test_fscontentsource_native import STUBS
ROOT=Path(__file__).resolve().parents[1]
BASE=STUBS.replace('struct LLResourceUploadInfo {};','')
UPLOADS=r'''
#include "fsfileuploadmodel.h"
struct LLResourceUploadInfo {
 using ptr_t=std::shared_ptr<LLResourceUploadInfo>;
 struct AssistantHooks {std::function<bool(const std::string&,bool)> before;std::function<void(const LLSD&,const std::string&)> stopped;};
 AssistantHooks hooks;int expected=0;bool quoteRequired=false;LLUUID taskID,itemID,experience;bool running=false;
 virtual ~LLResourceUploadInfo()=default;
 void setAssistantHooks(AssistantHooks h){hooks=std::move(h);}bool assistantOwned()const{return static_cast<bool>(hooks.before);}
 bool requiresQuotedUploadPrice()const{return quoteRequired;}int getExpectedUploadCost()const{return expected;}
 void assistantStopped(const LLSD& r,const std::string& stage){if(hooks.stopped)hooks.stopped(r,stage);}
 bool showUploadDialog()const{return false;}
};
struct LLScriptAssetUpload:LLResourceUploadInfo {
 enum TargetType_t{MONO,LSL2};
 using Finished=std::function<void(LLUUID,LLUUID,LLUUID,LLSD)>;using Failed=std::function<bool(LLUUID,LLUUID,LLSD,std::string)>;
 LLScriptAssetUpload(LLUUID task,LLUUID id,TargetType_t,bool run,LLUUID exp,std::string,Finished,Failed){taskID=task;itemID=id;running=run;experience=exp;}
 LLUUID getTaskId()const{return taskID;}LLUUID getItemId()const{return itemID;}LLUUID getExerienceId()const{return experience;}
 bool getIsRunning()const{return running;}TargetType_t getTargetType()const{return MONO;}
 LLSD generatePostBody();
};
'''
METADATA=r'''
namespace LLCore {
 struct HttpRequest {using ptr_t=std::shared_ptr<HttpRequest>;static constexpr int DEFAULT_POLICY_ID=0;};
 struct HttpOptions{void setRetries(int){}void setFollowRedirects(bool){}};
 struct HttpStatus{int type;explicit operator bool()const{return type>=200&&type<300;}int getType()const{return type;}int getStatus()const{return static_cast<bool>(*this)?0:1;}std::string getMessage()const{return "HTTP fixture failure";}};
}
LLSD serverResult,lastMetadata;std::function<void()> afterMetadata;
namespace LLCoreHttpUtil {
 struct HttpCoroutineAdapter {
  using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;
  static constexpr const char* HTTP_RESULTS="http";
  using Status=LLCore::HttpStatus;
  HttpCoroutineAdapter(const char*,int){}
  LLSD postAndSuspend(const std::shared_ptr<LLCore::HttpRequest>&,const std::string&,const LLSD& data,const std::shared_ptr<LLCore::HttpOptions>& = {}){
   lastMetadata=data;if(afterMetadata)afterMetadata();
   if(!data["object-id"].isUUID()||!data["item-id"].isUUID())return llsd::map("http",400);
   return serverResult;
  }
  static Status getStatusFromLLSD(const LLSD& value){return {value.asInteger()};}
 };
}
struct LLCoros{static LLCoros& instance(){static LLCoros c;return c;}template<class F>void launch(const char*,F f){f();}};
std::string currentSource="source";std::string source(){return currentSource;}bool live=true;
struct Prim {Region* getRegion(){return &region;}}prim;
Prim* target(const LLUUID&,std::string&){return &prim;}
struct Mutation {LLSD request,receipt;std::string source;bool finished=false,submitted=false;unsigned execution=0;fs_content::UploadProgress progress;
 fs_assistant::AsyncTaskPtr task;std::weak_ptr<LLResourceUploadInfo> upload;};
bool liveMutation(const std::shared_ptr<Mutation>& m){return live&&m->source==source()&&item.permissions.copy&&item.permissions.modify;}
void settle(const std::shared_ptr<Mutation>& m,const std::string& state,const std::string& reason){m->finished=true;m->receipt["state"]=state;m->receipt["reason"]=reason;}
bool requestedRunning(const LLSD&,bool& running,std::string&){running=false;return true;}
void finishTaskUpload(const std::shared_ptr<Mutation>&,const LLSD&,bool,const std::string&){}
bool invalidateRunning(const LLUUID&,const LLUUID&,bool){return true;}unsigned executionFor(const LLUUID&,const LLUUID&){return 1;}
void fs_object_tools_native_execution_changed(const LLUUID&,const LLUUID&){}
std::vector<LLResourceUploadInfo::ptr_t> queued;
struct LLViewerAssetUpload {static void EnqueueInventoryUpload(const std::string&,LLResourceUploadInfo::ptr_t p){queued.push_back(std::move(p));}};
'''
METADATA_CASES=r'''
int main(){
 const LLUUID object("11111111-1111-4111-8111-111111111111"),id("22222222-2222-4222-8222-222222222222"),exp("33333333-3333-4333-8333-333333333333");
 auto begin=[&](){queued.clear();auto m=std::make_shared<Mutation>();m->source="source";m->request=llsd::map("object_id",object.asString(),"item_id",id.asString(),"text","source");m->task=std::make_shared<fs_assistant::AsyncTask>(guard());return m;};
 // Null, empty and typed nil are explicit known-none responses, not failures.
 for(const auto& field:{LLSD(),LLSD(""),LLSD(LLUUID::null),LLSD("00000000-0000-0000-0000-000000000000"),LLSD(exp),LLSD(exp.asString())}){
  serverResult=llsd::map("http",200,"experience",field);auto m=begin();enqueueTaskScript(m);assert(queued.size()==1&&!m->finished);
  assert(lastMetadata["object-id"].isUUID()&&lastMetadata["object-id"].asUUID()==object&&lastMetadata["item-id"].isUUID());
  auto upload=std::dynamic_pointer_cast<LLScriptAssetUpload>(queued.back());assert(upload);const auto body=upload->generatePostBody();
  assert(body["experience"].isUUID()&&body["experience"].asUUID()==field.asUUID());assert(body["task_id"].asUUID()==object&&body["item_id"].asUUID()==id);
  assert(m->receipt["experience_preserved"].asBoolean()&&m->task->guard()->submitted()==0);
 }
 // Malformed supplied fields and failed HTTP must never become no association.
 for(int bad=0;bad<3;++bad){serverResult=llsd::map("http",bad==0?403:200);
  if(bad==1)serverResult["experience"]="malformed";if(bad==2)serverResult["experience"]=false;
  auto m=begin();enqueueTaskScript(m);assert(queued.empty()&&m->finished&&m->receipt["metadata_http_type"].asInteger()==(bad==0?403:200));
  assert(m->receipt["experience_metadata"].asString()==(bad==0?"metadata_http_error":"metadata_invalid_experience"));
 }
 serverResult=llsd::map("http",200,"experience",exp);
 for(int changed=0;changed<5;++changed){auto m=begin();afterMetadata=[&,changed](){if(changed==0)currentSource="other";if(changed==1)live=false;if(changed==2)m->task->guard()->cancel("revoked");if(changed==3)m->task.reset();if(changed==4)item.permissions.modify=false;};
  enqueueTaskScript(m);assert(queued.empty());afterMetadata={};currentSource="source";live=true;item.permissions.modify=true;
 }
 auto m=begin();enqueueTaskScript(m);assert(queued.size()==1);auto upload=queued.back();
 m->task->guard()->cancel("revoked after metadata");assert(!upload->hooks.before("capability",true));assert(m->task->guard()->submitted()==0);
}
'''

NATIVE_EXPERIENCE=r'''
#define LL_WARNS(tag) std::ostringstream()
#define LL_ENDL std::endl
struct LLExperienceCache {
 using ExperienceGetFn_t=std::function<void(const LLSD&)>;
 static constexpr const char* EXPERIENCE_ID="public_id";
 std::string mCapability(const char*){return "metadata";}
 void get(const LLUUID& id,const ExperienceGetFn_t& fn){if(fn)fn(llsd::map(EXPERIENCE_ID,id));}
 void fetchAssociatedExperienceCoro(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t&,LLUUID,LLUUID,std::string,ExperienceGetFn_t);
};
template<class T>struct LLHandle{T* value;T* get()const{return value;}};
struct ScriptEd{LLUUID experience;void setAssociatedExperience(const LLUUID& id){experience=id;}};
struct LLLiveLSLEditor {
 ScriptEd core;ScriptEd* mScriptEd=&core;void updateExperiencePanel(){}
 static void setAssociatedExperience(LLHandle<LLLiveLSLEditor>,const LLSD&);
};
'''
MISSING_EXPERIENCE_CASES=r'''
int main(){
 const LLUUID object("11111111-1111-4111-8111-111111111111"),id("22222222-2222-4222-8222-222222222222"),exp("33333333-3333-4333-8333-333333333333");
 LLExperienceCache cache;LLLiveLSLEditor editor;auto http=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("native",0);
 auto begin=[&](){queued.clear();auto m=std::make_shared<Mutation>();m->source="source";m->request=llsd::map("object_id",object.asString(),"item_id",id.asString(),"text","source");m->task=std::make_shared<fs_assistant::AsyncTask>(guard());return m;};
 for(bool associated:{false,true}){
  serverResult=llsd::map("http",200);if(associated)serverResult["experience"]=exp;
  LLSD nativeResult;cache.fetchAssociatedExperienceCoro(http,object,id,"metadata",[&](const LLSD& result){nativeResult=result;LLLiveLSLEditor::setAssociatedExperience({&editor},result);});
  if(!associated)assert(nativeResult["error"].asInteger()==-1&&nativeResult["message"].asString()=="no experience");
  assert(editor.core.experience==(associated?exp:LLUUID::null));
  auto m=begin();enqueueTaskScript(m);assert(queued.size()==1&&!m->finished);
  const auto body=std::dynamic_pointer_cast<LLScriptAssetUpload>(queued.back())->generatePostBody();
  assert(body["experience"].isUUID()&&body["experience"].asUUID()==editor.core.experience);
  assert(m->receipt["experience_metadata"].asString()==(associated?"known_association":"known_none"));
  assert(m->receipt["metadata_http_type"].asInteger()==200&&m->receipt["metadata_http_status"].asInteger()==0);
  assert(m->receipt["experience_preserved"].asBoolean()&&m->task->guard()->submitted()==0);
 }
 // Failed reads cannot authorize saving, even with a valid supplied UUID.
 for(int status:{0,403,500})for(bool associated:{false,true}){
  serverResult=llsd::map("http",status);if(associated)serverResult["experience"]=exp;
  auto m=begin();enqueueTaskScript(m);assert(queued.empty()&&m->finished&&m->task->guard()->submitted()==0);
  assert(m->receipt["experience_metadata"].asString()=="metadata_http_error");
 }
 // A successful no-Experience response does not bypass live source guards.
 serverResult=llsd::map("http",200);
 for(int changed=0;changed<4;++changed){auto m=begin();afterMetadata=[&,changed](){if(changed==0)currentSource="other";if(changed==1)live=false;if(changed==2)m->task->guard()->cancel("revoked");if(changed==3)item.permissions.modify=false;};
  enqueueTaskScript(m);assert(queued.empty()&&m->finished&&m->task->guard()->submitted()==0);
  assert(m->receipt["state"].asString()=="cancelled_before_submission");afterMetadata={};currentSource="source";live=true;item.permissions.modify=true;
 }
 auto m=begin();enqueueTaskScript(m);assert(queued.size()==1);auto upload=queued.back();
 m->task->guard()->cancel("revoked after metadata");assert(!upload->hooks.before("capability",true)&&m->task->guard()->submitted()==0);
}
'''

LEASES=r'''
struct LLGridManager{static LLGridManager& instance(){static LLGridManager g;return g;}std::string getGrid(){return "grid";}};
std::map<const LLResourceUploadInfo*,std::pair<LLUUID,unsigned>> sPendingInventoryUploads;
fs_content::PendingSourceOwners<LLUUID> sPendingLegacyInventoryUploads;
struct LLViewerAssetUpload{static bool hasPendingInventoryUpload(const LLUUID&,const LLResourceUploadInfo*);};
bool nativeBusy=false;
bool editorBusy(const LLUUID& id,bool,const LLResourceUploadInfo* own=nullptr){return nativeBusy||LLViewerAssetUpload::hasPendingInventoryUpload(id,own);}
struct FSAssistantPreparedOperation{LLSD request,validation;};
LLSD receiptFor(const LLSD&,std::string&){return {};}
'''
LEASE_CASES=r'''
int main(){
 const LLUUID first("11111111-1111-4111-8111-111111111111"),second("22222222-2222-4222-8222-222222222222");
 FSAssistantPreparedOperation prepared;prepared.request=llsd::map("op","saveScript","id",item.uuid,"text","same source","compiler","mono","__assistant_epoch","epoch","__assistant_guard",first);prepared.validation=fs_content::evidence(&item);
 auto state=std::make_shared<State>();auto old=std::make_shared<Write>();old->request=prepared.request;old->authority=prepared.validation;old->notecard=false;old->signature=signature(old->request);sWrites[first]=old;
 // Same material, a new execution. The trusted bridge ID replaces even an
 // existing user/request guard in the frozen copy, just as in postEvent.
 Posted posted{guard()};attach(posted,&prepared,"FSContent",second,state);
 LLSD request=prepared.request;request["__assistant_guard"]=second;request["reqid"]=second;
 assert(!ownWrite(request));assert(posted.async->allowed());
 auto current=std::make_shared<Write>();current->request=request;current->authority=prepared.validation;current->notecard=false;current->signature=signature(request);sWrites[second]=current;
 auto upload=std::make_shared<LLResourceUploadInfo>();current->upload=upload;sPendingInventoryUploads[upload.get()]={item.uuid,1};
 assert(ownWrite(request)==current);assert(posted.async->allowed());assert(state->last["__assistant_guard"].asUUID()==second);
 // The original failed write is not permitted to adopt the new upload lease.
 assert(!liveWrite(old));assert(liveWrite(current));
 auto other=std::make_shared<LLResourceUploadInfo>();sPendingInventoryUploads[other.get()]={item.uuid,1};assert(!liveWrite(current));sPendingInventoryUploads.erase(other.get());
 nativeBusy=true;assert(!liveWrite(current));nativeBusy=false;
 auto changed=request;changed["text"]="different";assert(!ownWrite(changed));
 auto expired=request;expired["__assistant_epoch"]="other epoch";assert(!ownWrite(expired));
 // Guards stay revoked even if source permissions later return.
 item.permissions.modify=false;assert(!posted.async->allowed());item.permissions.modify=true;assert(!posted.async->allowed());
 sPendingInventoryUploads.clear();assert(ownWrite(prepared.request)==old);
}
'''

class ScriptSaveNativeTests(unittest.TestCase):
    def run_native(self, source):
        with tempfile.TemporaryDirectory(prefix='fs-script-save-') as directory:
            cpp=Path(directory)/'test.cpp';cpp.write_text(source);exe=cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-UNDEBUG','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def metadata_source(self):
        text=(ROOT/'fsobjecttools.cpp').read_text()
        source=BASE+UPLOADS+METADATA
        for name in ['LLSD taskExperienceRequest(const LLSD& request)',
                     'bool stoppedCompile(const LLSD& request)',
                     'bool taskExperienceAssociation(const LLSD& result,LLUUID& experience,std::string& error)',
                     'void enqueueTaskScript(const std::shared_ptr<Mutation>& value)']:
            source+=function(text,name)
        source+=function((ROOT/'llviewerassetupload.cpp').read_text(),'LLSD LLScriptAssetUpload::generatePostBody()')
        return source

    def test_task_metadata_typed_identity_empty_association_and_live_guards(self):
        self.run_native(self.metadata_source()+METADATA_CASES)

    def test_missing_experience_matches_native_lookup_and_editor(self):
        source=self.metadata_source()+NATIVE_EXPERIENCE
        source+=function((ROOT.parent/'llmessage/llexperiencecache.cpp').read_text(),'void LLExperienceCache::fetchAssociatedExperienceCoro(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t &httpAdapter, LLUUID objectId, LLUUID itemId, std::string url, ExperienceGetFn_t fn)')
        source+=function((ROOT/'llpreviewscript.cpp').read_text(),'void LLLiveLSLEditor::setAssociatedExperience( LLHandle<LLLiveLSLEditor> editor, const LLSD& experience )')
        self.run_native(source+MISSING_EXPERIENCE_CASES)

    def test_optional_update_price_does_not_mutate_response_or_bypass_ceiling(self):
        text=(ROOT/'llviewerassetupload.cpp').read_text()
        begin=text.index('        const LLSD& quoteResult=result;');end=text.index('        std::string uploader =',begin)
        source=BASE+UPLOADS+r'''
struct LLUploadDialog{static void modalUploadFinished(){}};
bool passed=false;std::string stopped;
void check(LLSD& result,const LLResourceUploadInfo::ptr_t& uploadInfo){
'''+text[begin:end]+r'''
passed=true;}
int main(){auto u=std::make_shared<LLResourceUploadInfo>();u->setAssistantHooks({[](const std::string&,bool){return true;},[](const LLSD&,const std::string& stage){stopped=stage;}});
 auto checkPrice=[&](LLSD result,bool required,bool accepted){u->quoteRequired=required;passed=false;stopped.clear();const bool present=result.has("upload_price");check(result,u);assert(passed==accepted);assert(result.has("upload_price")==present);if(!accepted)assert(stopped=="price_increased");};
 checkPrice(LLSD(),false,true);checkPrice(LLSD(),true,false);
 checkPrice(llsd::map("upload_price",0),false,true);checkPrice(llsd::map("upload_price",0),true,true);
 checkPrice(llsd::map("upload_price",10),false,false);checkPrice(llsd::map("upload_price",10),true,false);
 checkPrice(llsd::map("upload_price",0.),false,false);checkPrice(llsd::map("upload_price","0"),false,false);checkPrice(llsd::map("upload_price",-1),false,false);
 u->expected=10;checkPrice(llsd::map("upload_price",5),true,true);
 // Reproduce the old lookup effect independently of compiler argument order.
 LLSD old;const auto& inserted=old["upload_price"];assert(old.has("upload_price")&&!inserted.isInteger());assert(!fs_file_upload::serverQuoteAccepted(false,old.has("upload_price"),inserted.isInteger(),0,inserted.asInteger()));
}
'''
        self.run_native(source)

    def test_identical_retry_uses_exact_frozen_execution_lease(self):
        text=(ROOT/'fscontentlistener.cpp').read_text();permissions=(ROOT/'fscontentpermissions.h').read_text()
        source=BASE+UPLOADS+'namespace fs_content {\n'
        for name in ['inline bool readable(const LLViewerInventoryItem* item,bool notecard)',
                     'inline bool writable(const LLViewerInventoryItem* item,bool notecard)']:
            source+=function(permissions,name)
        source+='}\nusing namespace fs_content;\n'+LEASES
        source+=function(text,'struct Write')+';\nstd::map<LLUUID,std::shared_ptr<Write>> sWrites;\n'
        for name in ['std::string signature(const LLSD& request)',
                     'std::shared_ptr<Write> ownWrite(const LLSD& request)',
                     'bool liveWrite(const std::shared_ptr<Write>& write)']:
            source+=function(text,name)
        source+=function((ROOT/'llviewerassetupload.cpp').read_text(),'bool LLViewerAssetUpload::hasPendingInventoryUpload(const LLUUID& itemId,const LLResourceUploadInfo* except)')
        validate=function(text,'bool validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error) const override').replace(' const override',' const')
        source+='struct Listener {bool prepareAssistant(FSAssistantPreparedOperation& p,std::string&)const {p.validation=evidence(&item);return !editorBusy(item.uuid,false);}\n'+validate+'};\n'
        source+='struct State {Listener listener;LLSD last;bool validateOperation(const FSAssistantPreparedOperation& p,std::string& error){last=p.request;return listener.validateAssistant(p,error);}};\nstruct Posted{fs_assistant::AsyncGuard async;};\n'
        bridge=(ROOT/'fseventapibridge.cpp').read_text();begin=bridge.index('                auto frozen=*prepared;');end=bridge.index('            }\n            if(fs_assistant::needsActiveCharacter',begin)
        source+='void attach(Posted& posted,const FSAssistantPreparedOperation* prepared,const std::string& api,const LLUUID& request_id,std::weak_ptr<State> weak){\n'+bridge[begin:end]+'}\n'
        self.run_native(source+LEASE_CASES)

if __name__=='__main__':unittest.main()
