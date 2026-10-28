"""Compile the production upload controller with fake native transport boundaries."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

class UploadSourceTests(unittest.TestCase):
    def test_actual_controller_revocation_outcomes_and_one_shot(self):
        text = (ROOT / 'fsfileuploadlistener.cpp').read_text()
        plan = text[text.index('struct Plan\n'):text.index('std::map<LLUUID,std::shared_ptr<Plan>> plans;')]
        storage = text[text.index('std::map<LLUUID,std::shared_ptr<Plan>> plans;'):text.index('void releaseBytes(')]
        retention = text[text.index('void prune()'):text.index('bool live(')]
        cleanup = text[text.index('void releaseBytes('):text.index('void prune()')]
        live = text[text.index('bool live('):text.index('class AssistantUploadInfo')]
        advance = text[text.index('void advance(const std::shared_ptr<Plan>& plan)\n{'):text.index('LLSD material(')]
        program = r'''
#include "fsfileuploadmodel.h"
#include "fsassistantasync.h"
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <utility>
namespace model=fs_file_upload;
using S32=int;using U32=unsigned;
struct LLUUID {
 std::string value;LLUUID()=default;LLUUID(std::string v):value(std::move(v)){}static LLUUID generateNewID(){static int n=0;return LLUUID("id"+std::to_string(++n));}
 bool isNull()const{return value.empty();}bool notNull()const{return !isNull();}std::string asString()const{return value;}
 bool operator==(const LLUUID& b)const{return value==b.value;}bool operator!=(const LLUUID& b)const{return !(*this==b);}bool operator<(const LLUUID& b)const{return value<b.value;}
};
struct LLSD {
 std::string value;std::map<std::string,LLSD> fields;std::vector<LLSD> array;
 LLSD()=default;LLSD(const char* v):value(v){}LLSD(std::string v):value(std::move(v)){}LLSD(bool v):value(v?"true":"false"){}LLSD(int v):value(std::to_string(v)){}LLSD(unsigned v):value(std::to_string(v)){}LLSD(const LLUUID& v):value(v.value){}
 LLSD& operator[](const std::string& key){return fields[key];}const LLSD& operator[](const std::string& key)const{static LLSD empty;const auto i=fields.find(key);return i==fields.end()?empty:i->second;}
 LLSD& operator[](int i){if(array.size()<=static_cast<unsigned>(i))array.resize(static_cast<unsigned>(i)+1);return array[static_cast<unsigned>(i)];}
 const LLSD& operator[](int i)const{return array.at(static_cast<unsigned>(i));}
 bool operator==(const LLSD& b)const{return value==b.value && fields==b.fields && array==b.array;}bool operator!=(const LLSD& b)const{return !(*this==b);}
 std::string asString()const{return value;}bool asBoolean()const{return value=="true";}LLUUID asUUID()const{return LLUUID(value);}bool has(const char* key)const{return fields.count(key)>0;}void append(const LLSD& v){array.push_back(v);}
};
namespace llsd {
void add(LLSD&){}
template<class Value,class... Args>void add(LLSD& out,const char* key,Value value,Args... args){out[key]=LLSD(value);add(out,args...);}
template<class... Args>LLSD map(Args... args){LLSD out;add(out,args...);return out;}
}
struct File{std::string name="asset",relative,encoded="bytes";int asset=0,inventory=0,folder=0,cost=0;};
PLAN
std::string origin="source-a";
std::string source(){return origin;}
double now(){return 1.;}
STORAGE
bool live(const std::shared_ptr<Plan>&,std::string&,bool);
LLSD destinationEvidence(const LLUUID&){return LLSD("dest-evidence");}
int currentQuote=0;int quote(const File&){return currentQuote;}
bool destination(const LLUUID&,std::string&){return true;}
CLEANUP
RETENTION
std::vector<std::function<void()>> idle;
void advance(const std::shared_ptr<Plan>&);
void schedule(const std::shared_ptr<Plan>& p){idle.push_back([p](){advance(p);});}
void drain(){auto work=std::move(idle);idle.clear();for(auto& fn:work)fn();}
struct LLFolderType{static constexpr int FT_NONE=0;};
struct Category{LLUUID id,parent;std::string name;LLUUID getUUID()const{return id;}LLUUID getParentUUID()const{return parent;}std::string getName()const{return name;}};
struct Inventory {
 std::map<LLUUID,Category> cats;std::function<void()> folderReply;unsigned creates=0;
 Category* getCategory(const LLUUID& id){auto i=cats.find(id);return i==cats.end()?nullptr:&i->second;}
 bool isObjectDescendentOf(const LLUUID&,const LLUUID&){return true;}
 void createNewCategory(LLUUID parent,int,std::string name,std::function<void(const LLUUID&)> callback){
  ++creates;auto outer=fs_assistant::asyncTask(),inner=fs_assistant::asyncTask();assert(fs_assistant::asyncSubmit());auto id=LLUUID::generateNewID();
  // The real AIS and createNewCategory wrappers mask an ID after revocation,
  // but both acknowledge it first through the real asyncDeliver helper.
  folderReply=[=,this](){cats[id]={id,parent,name};fs_assistant::asyncDeliver(inner,id.asString(),[&](bool allowed){auto masked=allowed?id:LLUUID();fs_assistant::asyncDeliver(outer,masked.asString(),[&](bool allowed2){callback(allowed2?masked:LLUUID());});});};
 }
}gInventory;
LIVE
struct LLResourceUploadInfo {
 using ptr_t=std::shared_ptr<LLResourceUploadInfo>;
 struct Hooks{std::function<bool(const std::string&,bool)> before;std::function<void(const LLSD&,const std::string&)> stopped;};Hooks hooks;
 virtual ~LLResourceUploadInfo()=default;void setAssistantHooks(Hooks h){hooks=std::move(h);}
};
struct AssistantUploadInfo:LLResourceUploadInfo {
 std::function<void(LLUUID,LLSD)> finished;std::function<bool(LLUUID,LLSD,const std::string&)> failed;
 AssistantUploadInfo(std::string,LLUUID,std::string,std::string,int,int,int,int,U32,U32,U32,int,LLUUID,bool,decltype(finished) f,decltype(failed) e):finished(std::move(f)),failed(std::move(e)){}
};
std::vector<std::shared_ptr<AssistantUploadInfo>> uploads;
struct LLViewerAssetUpload{static void EnqueueInventoryUpload(const std::string&,const LLResourceUploadInfo::ptr_t& info){uploads.push_back(std::dynamic_pointer_cast<AssistantUploadInfo>(info));}};
struct Agent{std::string getRegionCapability(const std::string&)const{return "cap";}}gAgent;
ADVANCE
bool allowed=true;
std::shared_ptr<Plan> makePlan(int count=1){auto p=std::make_shared<Plan>();p->origin=source();p->destination=LLUUID("dest");p->evidence=destinationEvidence(p->destination);p->files.resize(static_cast<unsigned>(count));p->progress.start();for(int i=0;i<count;++i)p->receipt["files"].append(llsd::map("state","not_submitted"));auto guard=std::make_shared<fs_assistant::AsyncOperation>([weak=std::weak_ptr<Plan>(p)](){std::string error;auto plan=weak.lock();return allowed && plan && live(plan,error,true)?std::string():"revoked_or_changed";});p->task=std::make_shared<fs_assistant::AsyncTask>(guard);return p;}
int main(){
 auto queued=makePlan();advance(queued);assert(uploads.size()==1);allowed=false;
 assert(!uploads.back()->hooks.before("capability",true));uploads.back()->hooks.stopped(LLSD(),"capability");assert(!queued->progress.running());assert(!queued->receipt["uncertain"].asBoolean());assert(queued->receipt["files"][0]["state"].asString()=="not_submitted");advance(queued);assert(uploads.size()==1);
 allowed=true;auto lost=makePlan();advance(lost);auto u=uploads.back();assert(u->hooks.before("capability",true));u->failed(LLUUID(),LLSD(),"response lost");assert(lost->receipt["uncertain"].asBoolean());assert(lost->receipt["files"][0]["state"].asString()=="submitted_unknown");
 auto late=makePlan();advance(late);u=uploads.back();assert(u->hooks.before("capability",true));assert(u->hooks.before("source",true));allowed=false;u->hooks.stopped(llsd::map("state","complete","new_asset",LLUUID("asset"),"new_inventory_item",LLUUID("item"),"upload_price",0),"source_result");assert(late->receipt["files"][0]["state"].asString()=="server_confirmed");assert(late->receipt["files"][0]["charged_cost"].asString()=="0");assert(!late->receipt["uncertain"].asBoolean());
 allowed=true;auto folder=makePlan();folder->files[0].relative="newfolder";advance(folder);assert(gInventory.creates==1);allowed=false;stop(folder,"cancelled");auto count=uploads.size();gInventory.folderReply();assert(folder->receipt["created_folders"].array.size()==1);assert(!folder->receipt["folder_in_flight"].asBoolean());drain();assert(uploads.size()==count);
 allowed=true;auto complete=makePlan();advance(complete);u=uploads.back();u->finished(LLUUID("asset"),llsd::map("new_inventory_item",LLUUID("item")));allowed=false;drain();assert(complete->progress.state==model::State::Complete);assert(complete->receipt["state"].asString()=="complete");
 allowed=true;auto moved=makePlan();moved->files[0].relative="A/B";advance(moved);gInventory.folderReply();auto first=moved->folders.at("A");auto creates=gInventory.creates;gInventory.cats[first].parent=LLUUID("outside");drain();assert(gInventory.creates==creates);assert(!moved->progress.running());
 allowed=true;auto queuedParent=makePlan();queuedParent->folders["A"]=LLUUID("a");gInventory.cats[LLUUID("a")]={LLUUID("a"),queuedParent->destination,"A"};auto guard=queuedParent->task->guard();assert(guard->allowed());gInventory.cats[LLUUID("a")].parent=LLUUID("outside");assert(!guard->submit());assert(guard->submitted()==0);
 allowed=true;plans.clear();plansSource=source();auto pending=makePlan();pending->id=LLUUID("00-pending");plans[pending->id]=pending;advance(pending);auto pendingUpload=uploads.back();assert(pendingUpload->hooks.before("capability",true));assert(pendingUpload->hooks.before("source",true));stop(pending,"cancelled in flight");
 for(int i=1;i<32;++i){auto other=makePlan();other->id=LLUUID("retained-"+std::to_string(i));plans[other->id]=other;}
 auto request=llsd::map("plan_id",pending->id);std::string error;assert(plans.size()==32);assert(pending->nativePending==1);
 for(int poll=0;poll<64;++poll){assert(findPlan(request,error)==pending);assert(plans.size()==32);}
 assert(!admitPlan());assert(plans.size()==32);assert(findPlan(request,error)==pending);
 pendingUpload->hooks.stopped(llsd::map("state","complete","new_asset",LLUUID("late-asset"),"new_inventory_item",LLUUID("late-item"),"upload_price",0),"source_result");auto receipt=findPlan(request,error);assert(receipt==pending);assert(receipt->receipt["files"][0]["asset_id"].asString()=="late-asset");assert(receipt->receipt["files"][0]["item_id"].asString()=="late-item");assert(!admitPlan());
 auto retainedReply=pendingUpload->hooks.stopped;
 uploads.erase(std::remove(uploads.begin(),uploads.end(),pendingUpload),uploads.end());pendingUpload.reset();
 assert(pending->nativePending==1);assert(!admitPlan());assert(findPlan(request,error)==pending);
 retainedReply={};assert(pending->nativePending==0);assert(findPlan(request,error)==pending);assert(plans.size()==32);
 assert(admitPlan());assert(plans.size()==31);assert(!findPlan(request,error));
 plans.clear();
 auto pendingFolder=makePlan();pendingFolder->id=LLUUID("00-folder");pendingFolder->files[0].relative="retained-folder";plans[pendingFolder->id]=pendingFolder;advance(pendingFolder);stop(pendingFolder,"cancelled folder in flight");
 for(int i=1;i<32;++i){auto other=makePlan();plans[LLUUID("retained-folder-"+std::to_string(i))]=other;}
 auto folderRequest=llsd::map("plan_id",pendingFolder->id);assert(pendingFolder->nativePending==1);assert(findPlan(folderRequest,error)==pendingFolder);assert(!admitPlan());
 const auto uploadCount=uploads.size();gInventory.folderReply();assert(findPlan(folderRequest,error)==pendingFolder);assert(pendingFolder->receipt["created_folders"].array.size()==1);assert(!admitPlan());assert(uploads.size()==uploadCount);
 gInventory.folderReply={};assert(pendingFolder->nativePending==0);assert(findPlan(folderRequest,error)==pendingFolder);assert(admitPlan());assert(!findPlan(folderRequest,error));
 plans.clear();
 allowed=true;auto batch=makePlan(2);advance(batch);u=uploads.back();assert(u->hooks.before("source",true));u->failed(LLUUID(),LLSD(),"failed");count=uploads.size();drain();advance(batch);assert(uploads.size()==count);assert(batch->receipt["files"][1]["state"].asString()=="not_submitted");
}
'''.replace('PLAN', plan).replace('STORAGE', storage).replace('CLEANUP', cleanup).replace('RETENTION', retention).replace('LIVE', live).replace('ADVANCE', advance)
        with tempfile.TemporaryDirectory() as temp:
            cpp = pathlib.Path(temp) / 'controller.cpp'
            cpp.write_text(program)
            exe = pathlib.Path(temp) / 'controller'
            subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I'+str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_native_boundaries_and_price_authority(self):
        listener = (ROOT / 'fsfileuploadlistener.cpp').read_text()
        transport = (ROOT / 'llviewerassetupload.cpp').read_text()
        bridge = (ROOT / 'fseventapibridge.cpp').read_text()
        self.assertIn('api=="FSUpload" && op=="execute" ? prepared.summary["cost"].asInteger() : 0', bridge)
        self.assertLess(transport.index('fs_file_upload::serverQuoteAccepted('), transport.index('postFileAndSuspend(', transport.index('fs_file_upload::serverQuoteAccepted(')))
        self.assertIn('bool requiresQuotedUploadPrice()const override{return true;}', listener)
        self.assertIn('~ValidationCache(){LLKeyframeDataCache::removeKeyframeData(id);}', listener)
        self.assertLess(listener.index('ValidationCache\n'), listener.index('LLKeyframeMotion motion(validationCache.id)'))
        for guard in ('plan->origin!=source()', '!fs_assistant::asyncTaskAllowed(task)', 'O_RDONLY|O_NONBLOCK|O_NOFOLLOW|O_CLOEXEC', '!S_ISREG(info.st_mode)', 'FILE_FLAG_OPEN_REPARSE_POINT', 'GetFileType(handle)!=FILE_TYPE_DISK'):
            self.assertIn(guard, listener)
        # Assistant retries remain zero at native upload transport, not a Go promise.
        self.assertIn('setRetries(0)', transport)
        self.assertIn('uploadRetryLimit(uploadInfo->assistantOwned())', transport)

if __name__ == '__main__':
    unittest.main()
