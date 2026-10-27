"""Exercise native validators and pending-Contents scope checks unchanged.

Only LLSD/viewer dependencies are replaced. This covers raw native callers and
recipe children, which must not rely on the Go wrapper's validation.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(text, signature):
    start = re.search(re.escape(signature) + r'\s*\{', text).start()
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


STUBS = r'''
#include "fscontentpolicy.h"
#include "fsassistantnearbychatpolicy.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <variant>
#include <vector>
#include <cstdio>
using S32=int;
namespace fs_content { constexpr std::size_t textLimit=65536; }
struct LLUUID {
 std::string value; LLUUID()=default; explicit LLUUID(std::string s):value(std::move(s)){}
 bool notNull()const{return validate(value)&&value!="00000000-0000-0000-0000-000000000000";}
 static bool validate(const std::string& s){if(s.size()!=36)return false;for(std::size_t i=0;i<s.size();++i){
  if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
  else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;}return true;}
 bool operator<(const LLUUID& b)const{return value<b.value;}
 bool operator!=(const LLUUID& b)const{return value!=b.value;}
 bool operator==(const LLUUID& b)const{return value==b.value;}
 static LLUUID generateNewID(){static unsigned next=0;char buffer[37];std::snprintf(buffer,sizeof(buffer),"11111111-1111-4111-8111-%012u",++next);return LLUUID(buffer);}
 std::string asString()const{return value;}
};
struct LLSD {
 using Map=std::map<std::string,LLSD>;using Array=std::vector<LLSD>;
 std::variant<std::monostate,bool,int,double,std::string,LLUUID,Map,Array> value;
 LLSD()=default;LLSD(bool v):value(v){} LLSD(int v):value(v){} LLSD(double v):value(v){}
 LLSD(const char* v):value(std::string(v)){} LLSD(std::string v):value(std::move(v)){} LLSD(LLUUID v):value(std::move(v)){}
 LLSD(Array v):value(std::move(v)){}
 bool isString()const{return std::holds_alternative<std::string>(value);}bool isUUID()const{return std::holds_alternative<LLUUID>(value);}
 bool isInteger()const{return std::holds_alternative<int>(value);}bool isReal()const{return std::holds_alternative<double>(value);}
 bool isBoolean()const{return std::holds_alternative<bool>(value);}bool isMap()const{return std::holds_alternative<Map>(value);}
 bool isArray()const{return std::holds_alternative<Array>(value);}
 std::string asString()const{return isString()?std::get<std::string>(value):isUUID()?std::get<LLUUID>(value).value:std::string();}
 LLUUID asUUID()const{return LLUUID(asString());}int asInteger()const{return isInteger()?std::get<int>(value):0;}
 double asReal()const{return isReal()?std::get<double>(value):asInteger();}bool asBoolean()const{return isBoolean()&&std::get<bool>(value);}
 int size()const{return static_cast<int>(isArray()?std::get<Array>(value).size():isMap()?std::get<Map>(value).size():0);}
 bool has(const std::string& k)const{return isMap()&&std::get<Map>(value).count(k);}
 LLSD& operator[](const std::string& k){if(!isMap())value=Map();return std::get<Map>(value)[k];}
 const LLSD& operator[](const std::string& k)const{static LLSD empty;return has(k)?std::get<Map>(value).at(k):empty;}
 LLSD& operator[](int i){return std::get<Array>(value).at(i);}const LLSD& operator[](int i)const{return std::get<Array>(value).at(i);}
 auto beginMap()const{return std::get<Map>(value).begin();}auto endMap()const{return std::get<Map>(value).end();}
};
namespace llsd {const LLSD::Array& inArray(const LLSD& v){static LLSD::Array empty;return v.isArray()?std::get<LLSD::Array>(v.value):empty;}
 LLSD map(){LLSD v;v.value=LLSD::Map();return v;}
 template<class K,class V,class... Rest> LLSD map(K key,V value,Rest... rest){auto result=map(rest...);result[key]=value;return result;}}
std::string ll_stream_notation_sd(const LLSD& v){std::string s=v.asString();if(v.isArray())for(const auto& x:llsd::inArray(v))s+=ll_stream_notation_sd(x);
 if(v.isMap())for(auto i=v.beginMap();i!=v.endMap();++i)s+=i->first+ll_stream_notation_sd(i->second);
 if(v.isInteger())s+=std::to_string(v.asInteger());if(v.isBoolean())s+=v.asBoolean()?"true":"false";return s;}
bool llsd_equals(const LLSD& a,const LLSD& b){return ll_stream_notation_sd(a)==ll_stream_notation_sd(b);}
struct Inventory {virtual ~Inventory()=default;};
struct LLViewerInventoryItem:Inventory {LLUUID asset,uuid;bool modify=true;LLViewerInventoryItem()=default;
 explicit LLViewerInventoryItem(const LLViewerInventoryItem* p):asset(p->asset),uuid(p->uuid),modify(p->modify){}
 LLUUID getAssetUUID()const{return asset;}LLUUID getUUID()const{return uuid;}};
LLSD itemEvidence(const LLViewerInventoryItem* p){return llsd::map("asset_id",p->asset,"item_id",p->uuid,"modify",p->modify);}
struct LLHost {int value=1;bool operator<(const LLHost& b)const{return value<b.value;}bool operator!=(const LLHost& b)const{return value!=b.value;}bool operator==(const LLHost& b)const{return value==b.value;}};
struct Region {LLHost host;LLHost getHost()const{return host;}}region;
struct LLViewerObject {
 bool pending=false,dirty=false,root=true,complete=true;LLSD evidence,identity;LLViewerInventoryItem* item=nullptr;LLUUID uuid;
 Inventory* getInventoryObject(const LLUUID&){return item;}
 bool isInventoryPending()const{return pending;}bool isInventoryDirty()const{return dirty;}bool getInventoryRoot()const{return root;}
 bool isInventoryServerComplete()const{return complete;}LLUUID getID()const{return uuid;}Region* getRegion()const{return &region;}
 bool isDead()const{return false;}
} prim;
LLViewerObject* target(const LLUUID&,std::string&){return &prim;}
LLSD scopeIdentity(LLViewerObject* p){return p->identity.isMap()?p->identity:p->evidence;}
bool scriptReadable(LLViewerInventoryItem* p){return p&&p->modify;}
enum {CHAT_TYPE_OWNER=1,CHAT_TYPE_DEBUG_MSG=2,RLV_BHVR_RECVCHAT,RLV_BHVR_RECVCHATFROM,RLV_BHVR_RECVEMOTE,RLV_BHVR_RECVEMOTEFROM};
struct rlv_handler_t {static bool isEnabled(){return true;}};
struct RlvHandler {std::set<int> blocked,exceptions;bool hasBehaviour(int b)const{return blocked.count(b);}
 bool isException(int b,const LLUUID&)const{return exceptions.count(b);}}gRlvHandler;
'''

ADAPTER_STUBS = r'''
#include "fsobjecttoolsmodel.h"
#include "fsscripttestmodel.h"
#include <tuple>
using U64=std::uint64_t;
template<class T>using LLPointer=std::shared_ptr<T>;
struct ObjectList {LLViewerObject* findObject(const LLUUID& id){return id==prim.uuid?&prim:nullptr;}}gObjectList;
struct Agent {LLUUID account{"account"},login{"login"};LLUUID getID()const{return account;}LLUUID getSessionID()const{return login;}Region* getRegion()const{return &region;}}gAgent;
bool bridgeReady=true;
bool ready(){return bridgeReady;}std::string source(){return bridgeReady?"source":"";}
double clockValue=10.;double now(){return clockValue;}
namespace fs_assistant {
 using AsyncGuard=std::shared_ptr<int>;
 struct AsyncScope {AsyncGuard before;explicit AsyncScope(AsyncGuard v):before(current()){current()=std::move(v);}~AsyncScope(){current()=before;}
  static AsyncGuard& current(){static AsyncGuard v;return v;}};
 struct Task {AsyncGuard value=std::make_shared<int>(1);AsyncGuard guard()const{return value;}};
 using AsyncTaskPtr=std::shared_ptr<Task>;
 bool asyncAllowed(){return true;}
}
struct LLResourceUploadInfo {using ptr_t=std::shared_ptr<LLResourceUploadInfo>;virtual ~LLResourceUploadInfo()=default;LLUUID item;bool owned=false;
 LLUUID getItemId()const{return item;}bool assistantOwned()const{return owned;}};
struct LLScriptAssetUpload:LLResourceUploadInfo {LLUUID task;LLUUID getTaskId()const{return task;}};
std::map<const LLResourceUploadInfo*,std::pair<LLUUID,unsigned>> sPendingInventoryUploads;
struct LLViewerAssetUpload {static inline bool pending=false;static bool hasPendingInventoryUpload(const LLUUID&){return pending||!sPendingInventoryUploads.empty();}};
struct Snapshot {std::string source,epoch;LLSD object;double expires=0.;std::map<LLUUID,LLPointer<LLViewerInventoryItem>> items;};
struct Running {std::string source,epoch;LLUUID token,asset;bool running=false,mono=false;double at=0.;};
struct RunningRead {LLSD request,item;std::string source;fs_assistant::AsyncTaskPtr task;U64 privacyEpoch=0,execution=0;bool done=false;};
struct Mutation {LLSD request,receipt;std::string source;U64 execution=0;bool submitted=false;};
struct TestSession {
 std::string source,epoch;LLSD scope;fs_assistant::AsyncTaskPtr task;fs_script_test::Sequence sequence;
 struct InternalSnapshot {LLUUID token;Snapshot value;};std::map<LLUUID,InternalSnapshot> mutationSnapshots;
};
struct Service {
 std::map<LLUUID,Snapshot> snapshots;std::shared_ptr<TestSession> test;
 std::map<std::pair<LLUUID,LLUUID>,Running> running;std::vector<std::shared_ptr<RunningRead>> runningReads;
 U64 privacyEpoch=0;fs_object_tools::Receipts<std::shared_ptr<Mutation>> operations;
};
Service& service(){static Service v;return v;}
LLSD objectEvidence(LLViewerObject* p){return p->evidence;}
LLSD resultBase(){return {};}
std::vector<LLSD> delivered;
void reply(const LLSD&,const LLSD& result){delivered.push_back(result);}
bool fs_assistant_request_allowed(const LLSD&){return true;}
void settle(const std::shared_ptr<Mutation>& value,const std::string& state,const std::string&){value->receipt["state"]=state;}
constexpr const char* _PREHASH_GetScriptRunning="GetScriptRunning";
constexpr const char* _PREHASH_Script="Script";
constexpr const char* _PREHASH_ObjectID="ObjectID";
constexpr const char* _PREHASH_ItemID="ItemID";
constexpr const char* _PREHASH_Running="Running";
struct LLMessageSystem {
 LLUUID object,item;LLHost sender;bool running=true,mono=true;int sent=0;
 void newMessageFast(const char*){}void nextBlockFast(const char*){}
 void addUUIDFast(const char*,const LLUUID&){}void sendReliable(const LLHost&){++sent;}
 LLHost getSender()const{return sender;}
 void getUUIDFast(const char*,const char* key,LLUUID& out)const{out=std::string(key)=="ObjectID"?object:item;}
 void getBOOLFast(const char*,const char* key,bool& out)const{out=std::string(key)=="Running"?running:mono;}
}message;
LLMessageSystem* gMessageSystem=&message;
using ExecutionKey=std::tuple<LLHost,LLUUID,LLUUID>;
'''

RUNNING_CASES = r'''
int main(){
 prim.uuid=LLUUID("11111111-1111-4111-8111-111111111111");
 LLViewerInventoryItem item;item.uuid=LLUUID("22222222-2222-4222-8222-222222222222");item.asset=LLUUID("asset-A");prim.item=&item;
 message.object=prim.uuid;message.item=item.uuid;
 // This is the production native-editor request adapter, with MCP disabled.
 bridgeReady=false;assert(fs_object_tools_request_running(&prim,item.uuid));assert(message.sent==1);
 bridgeReady=true;assert(!invalidateRunning(prim.uuid,item.uuid)); // assistant compile must wait for the editor's old wire reply
 // Native editing remains available. It changes execution but keeps the old
 // query's version fence after the editor closes or MCP is enabled again.
 fs_object_tools_native_execution_changed(prim.uuid,item.uuid);item.asset=LLUUID("asset-B");
 auto read=std::make_shared<RunningRead>();read->source=source();read->request["object_id"]=prim.uuid;read->request["item_id"]=item.uuid;
 read->request["__assistant_epoch"]="epoch";read->item=itemEvidence(&item);read->execution=executionFor(prim.uuid,item.uuid);read->task=std::make_shared<fs_assistant::Task>();
 service().runningReads.push_back(read);
 assert(!requestRunning(&prim,item.uuid));assert(message.sent==1); // cannot join an old-version wire query
 message.running=true;assert(!fs_object_tools_running_reply(&message));assert(read->done);
 assert(service().running.empty()&&!delivered.empty()&&delivered.back().has("error"));
 // Only a fresh wire query can report the newly compiled stopped revision.
 auto fresh=std::make_shared<RunningRead>(*read);fresh->done=false;fresh->item=itemEvidence(&item);
 service().runningReads.push_back(fresh);assert(requestRunning(&prim,item.uuid));assert(message.sent==2);
 message.running=false;assert(fs_object_tools_running_reply(&message));
 const auto observed=service().running.at({prim.uuid,item.uuid});assert(!observed.running&&observed.asset==item.asset);
 // Native and assistant readers at the same execution coalesce once and both
 // receive the current result. A native action invalidates that cached token.
 assert(fs_object_tools_request_running(&prim,item.uuid));assert(requestRunning(&prim,item.uuid));assert(message.sent==3);
 fs_object_tools_native_execution_changed(prim.uuid,item.uuid);assert(service().running.empty());
 assert(!fs_object_tools_running_reply(&message));
 // Ordinary native callers can refresh after draining an obsolete response.
 assert(fs_object_tools_request_running(&prim,item.uuid));assert(message.sent==4);assert(fs_object_tools_running_reply(&message));
 // Consent toggles never erase wire fences; a wrong-region packet cannot drain one.
 assert(fs_object_tools_request_running(&prim,item.uuid));bridgeReady=false;service()=Service();bridgeReady=true;
 message.sender.value=2;assert(!fs_object_tools_running_reply(&message));assert(!invalidateRunning(prim.uuid,item.uuid));
 message.sender.value=1;assert(fs_object_tools_running_reply(&message));assert(invalidateRunning(prim.uuid,item.uuid));
 // Native Contents installation must also invalidate under an assistant scope.
 const auto prior=executionFor(prim.uuid,item.uuid);
 {fs_assistant::AsyncScope assistant(std::make_shared<int>(1));fs_object_tools_native_execution_changed(prim.uuid,item.uuid);}
 assert(executionFor(prim.uuid,item.uuid)>prior);
 // Server Contents may observe an external source replacement without any
 // local native execution hook. The wire request's original asset is binding.
 assert(fs_object_tools_request_running(&prim,item.uuid));const auto beforeExternalQuery=message.sent;
 item.asset=LLUUID("asset-C");assert(!requestRunning(&prim,item.uuid));assert(message.sent==beforeExternalQuery);
 auto external=std::make_shared<RunningRead>(*fresh);external->done=false;external->item=itemEvidence(&item);external->execution=executionFor(prim.uuid,item.uuid);
 service().runningReads.push_back(external);message.running=true;
 assert(!fs_object_tools_running_reply(&message));assert(external->done&&service().running.empty());
 auto externalFresh=std::make_shared<RunningRead>(*external);externalFresh->done=false;service().runningReads.push_back(externalFresh);
 assert(requestRunning(&prim,item.uuid));message.running=false;assert(fs_object_tools_running_reply(&message));
 assert(!service().running.at({prim.uuid,item.uuid}).running&&service().running.at({prim.uuid,item.uuid}).asset==item.asset);
 // Unknown native compile completion is a terminal fence even if no success
 // callback arrived. A native query started during upload cannot become fresh
 // evidence after the pending-upload marker vanishes.
 auto upload=std::make_shared<LLScriptAssetUpload>();upload->task=prim.uuid;upload->item=item.uuid;
 {
  PendingInventoryUpload pending(upload);fs_object_tools_native_execution_changed(prim.uuid,item.uuid);
  assert(fs_object_tools_request_running(&prim,item.uuid));
 } // failure/unknown outcome: the production lease destructor invalidates
 assert(!LLViewerAssetUpload::hasPendingInventoryUpload(item.uuid));
 assert(!fs_object_tools_running_reply(&message));
 // A late reply from an object's previous simulator drains its old fence but
 // cannot update the native editor now displaying the object in another region.
 assert(fs_object_tools_request_running(&prim,item.uuid));region.host.value=2;
 assert(!fs_object_tools_running_reply(&message));
 assert(fs_object_tools_request_running(&prim,item.uuid));message.sender.value=2;assert(fs_object_tools_running_reply(&message));
 region.host.value=1;message.sender.value=1;
 auto pendingOldHost=std::make_unique<PendingInventoryUpload>(upload);region.host.value=2;
 const auto newHostVersion=executionFor(prim.uuid,item.uuid);pendingOldHost.reset();
 assert(executionFor(prim.uuid,item.uuid)==newHostVersion);region.host.value=1;
 // Explicit native reopen after a lost reply remains usable. This target's
 // assistant evidence is now uncertain; no new indistinguishable reply can pass.
 assert(fs_object_tools_request_running(&prim,item.uuid));const auto sentBeforeRetry=message.sent;
 clockValue+=11.;assert(fs_object_tools_request_running(&prim,item.uuid));assert(message.sent==sentBeforeRetry+1);
 assert(!requestRunning(&prim,item.uuid)&&!invalidateRunning(prim.uuid,item.uuid));
 assert(fs_object_tools_running_reply(&message)); // native UI still receives its result
 assert(service().running.empty());
}
'''

SNAPSHOT_CASES = r'''
int main(){
 const LLUUID object("11111111-1111-4111-8111-111111111111"),itemID("22222222-2222-4222-8222-222222222222"),oldToken("33333333-3333-4333-8333-333333333333");
 prim.uuid=object;prim.identity=llsd::map("object_id",object,"root_id",object);prim.evidence=prim.identity;prim.evidence["generation"]=1;
 LLViewerInventoryItem item;item.uuid=itemID;item.asset=LLUUID("asset-A");prim.item=&item;
 auto run=std::make_shared<TestSession>();run->source=source();run->epoch="epoch";run->task=std::make_shared<fs_assistant::Task>();run->sequence.start(3,now(),600.);
 run->scope["objects"]=LLSD::Array{prim.identity};
 run->scope["scripts"]=LLSD::Array{llsd::map("object_id",object,"item_id",itemID,"asset_id",item.asset,"item_evidence",itemEvidence(&item))};service().test=run;
 Snapshot old;old.source=source();old.epoch="epoch";old.object=prim.evidence;old.expires=now()+120.;
 // Public snapshot capacity is full: internal assertions/actions must not
 // consume it or relax the standard public snapshot's generation checks.
 service().snapshots[oldToken]=old;for(int i=0;i<15;++i)service().snapshots[LLUUID::generateNewID()]=old;
 LLSD child=llsd::map("op","scriptRunning","object_id",object,"item_id",itemID,"snapshot_id",oldToken,"__assistant_epoch","epoch");
 std::string error;assert(contentsSnapshot(child,error));
 prim.evidence["generation"]=2;assert(!contentsSnapshot(child,error)); // assert_contents refreshed inventory
 {
  fs_assistant::AsyncScope scope(run->task->guard());
  assert(refreshTestMutationSnapshot(run,child,error));assert(child["snapshot_id"].asUUID()!=oldToken);
  auto* fresh=contentsSnapshot(child,error);assert(fresh&&fresh->object["generation"].asInteger()==2);
  assert(service().snapshots.size()==16&&run->mutationSnapshots.size()==1);
  // Repeat the refreshed Contents -> next runtime mutation sequence.
  prim.evidence["generation"]=3;child["op"]="scriptReset";
  assert(refreshTestMutationSnapshot(run,child,error)&&contentsSnapshot(child,error));
  assert(run->mutationSnapshots.size()==1);
  item.asset=LLUUID("asset-B");assert(!refreshTestMutationSnapshot(run,child,error));item.asset=LLUUID("asset-A");
  item.modify=false;assert(!refreshTestMutationSnapshot(run,child,error));item.modify=true;
  prim.dirty=true;assert(!refreshTestMutationSnapshot(run,child,error));prim.dirty=false;
  prim.complete=false;assert(!refreshTestMutationSnapshot(run,child,error));prim.complete=true;
  assert(refreshTestMutationSnapshot(run,child,error));
  clockValue+=121.;assert(!contentsSnapshot(child,error));clockValue-=121.;
 }
 assert(!contentsSnapshot(child,error)); // an external scope cannot use a run-private token
 {
  fs_assistant::AsyncScope wrong(std::make_shared<int>(9));assert(!contentsSnapshot(child,error));
 }
}
'''

CASES = r'''
int main(){
 const LLSD uuid="11111111-1111-4111-8111-111111111111";std::string error;
 LLSD request;request["op"]="testBegin";request["client_request_id"]=uuid;
 request["object_ids"]=LLSD::Array{uuid};request["timeout_seconds"]=600;
 request["__test_step_ids"]=LLSD::Array{uuid};
 LLSD step;step["object_id"]=uuid;step["action"]="send_channel";step["channel"]=-900123;step["text"]="start";
 const auto valid=[&](const LLSD& s){request["steps"]=LLSD::Array{s};return testArguments(request,error)&&mutationArguments(testChild(request,0),error);};
 assert(valid(step));step["channel"]=0;assert(!valid(step));step["channel"]=std::numeric_limits<S32>::max();assert(!valid(step));
 step["channel"]=-1;step["text"]=std::string(1024,'x');assert(!valid(step));
 step["text"]=std::string("a\0b",3);assert(!valid(step));step["text"]=std::string("\xc0\xaf",2);assert(!valid(step));
 step=LLSD();step["object_id"]=uuid;step["action"]="running";step["item_id"]=uuid;step["snapshot_id"]=uuid;
 assert(!valid(step));step["running"]=false;assert(valid(step));step["running"]="false";assert(!valid(step));
 step=LLSD();step["object_id"]=uuid;step["action"]="touch";assert(valid(step));
 step["face"]=0;assert(!valid(step));step["uv"]=LLSD::Array{.5,.5};step["local_point"]=LLSD::Array{0.,0.,0.};assert(valid(step));
 step["uv"]=LLSD::Array{2.,0.};assert(!valid(step));step["uv"]=LLSD::Array{.5,.5};
 step["local_point"]=LLSD::Array{std::numeric_limits<double>::quiet_NaN(),0.,0.};assert(!valid(step));
 step["local_point"]=LLSD::Array{0.,0.,0.};step["face"]=1.5;assert(!valid(step));
 step["face"]=0;step["text"]="field for another action";assert(!valid(step));
 // Native running/Contents assertions are typed reads, not recipe mutations.
 step=LLSD();step["object_id"]=uuid;step["action"]="assert_contents";step["item_id"]=uuid;step["timeout_seconds"]=5;
 request["steps"]=LLSD::Array{step};assert(!testArguments(request,error));step["expected"]=false;
 request["steps"]=LLSD::Array{step};assert(testArguments(request,error));
 // Internal Contents requests temporarily clear the native cache. Keep the
 // source guard alive, but mark it pending until authoritative data returns.
 LLSD scope,identity,script;identity["object_id"]=uuid;prim.evidence=identity;
 script["object_id"]=uuid;script["item_id"]=uuid;script["asset_id"]=uuid;
 scope["objects"]=LLSD::Array{identity};scope["scripts"]=LLSD::Array{script};
 bool pending=false;prim.pending=true;assert(testScopeCurrent(scope,error,&pending)&&pending);
 prim.pending=false;prim.dirty=true;pending=false;assert(testScopeCurrent(scope,error,&pending)&&pending);
 prim.dirty=false;assert(!testScopeCurrent(scope,error));LLViewerInventoryItem item;item.asset=uuid.asUUID();prim.item=&item;
 pending=false;assert(testScopeCurrent(scope,error,&pending)&&!pending);
 item.asset=LLUUID("22222222-2222-4222-8222-222222222222");assert(!testScopeCurrent(scope,error));
 // Native debug output follows receive policy, including per-prim exceptions;
 // native owner-say keeps its existing exemption.
 assert(diagnosticReceiveAllowed(uuid.asUUID(),CHAT_TYPE_DEBUG_MSG,false));
 gRlvHandler.blocked.insert(RLV_BHVR_RECVCHAT);
 assert(!diagnosticReceiveAllowed(uuid.asUUID(),CHAT_TYPE_DEBUG_MSG,false));
 assert(diagnosticReceiveAllowed(uuid.asUUID(),CHAT_TYPE_OWNER,false));
 gRlvHandler.exceptions.insert(RLV_BHVR_RECVCHAT);
 assert(diagnosticReceiveAllowed(uuid.asUUID(),CHAT_TYPE_DEBUG_MSG,false));
 gRlvHandler.blocked.insert(RLV_BHVR_RECVCHATFROM);gRlvHandler.exceptions.insert(RLV_BHVR_RECVCHATFROM);
 assert(!diagnosticReceiveAllowed(uuid.asUUID(),CHAT_TYPE_DEBUG_MSG,false));
 gRlvHandler.blocked.insert(RLV_BHVR_RECVEMOTE);
 assert(!diagnosticReceiveAllowed(uuid.asUUID(),CHAT_TYPE_DEBUG_MSG,true));
 // Private child IDs cannot be missing or shared between recipe steps.
 request["steps"]=LLSD::Array{step,step};assert(!testArguments(request,error));
 request["__test_step_ids"]=LLSD::Array{uuid,uuid};assert(!testArguments(request,error));
}
'''


class NativeObjectToolsTests(unittest.TestCase):
    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory(prefix='fs-object-tools-') as directory:
            cpp = Path(directory) / 'test.cpp'
            cpp.write_text(source)
            exe = cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                '-UNDEBUG', '-I' + str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_native_editor_and_assistant_running_replies(self):
        text = (ROOT / 'fsobjecttools.cpp').read_text()
        helpers = [
            'ExecutionState& executionState()',
            'ExecutionKey executionKey(LLViewerObject* prim,const LLUUID& item)',
            'U64 executionFor(const LLUUID& prim,const LLUUID& item)',
            'bool runningQueryPending(LLViewerObject* prim,const LLUUID& item)',
            'bool invalidateRunning(const LLUUID& prim,const LLUUID& item)',
            'bool requestRunning(LLViewerObject* prim,const LLUUID& item,bool nativeObserver=false)',
            'bool fs_object_tools_request_running(LLViewerObject* prim,const LLUUID& item)',
            'void fs_object_tools_native_execution_changed(const LLUUID& object,const LLUUID& item)',
            'bool fs_object_tools_running_reply(LLMessageSystem* msg)',
        ]
        source = STUBS + ADAPTER_STUBS + function(text, 'struct ExecutionState') + ';\n'
        source += ''.join(function(text, name) for name in helpers)
        source += function((ROOT / 'llviewerassetupload.cpp').read_text(), 'struct PendingInventoryUpload') + ';\n'
        self.compile_and_run(source + RUNNING_CASES)

    def test_refreshed_contents_then_runtime_mutation(self):
        text = (ROOT / 'fsobjecttools.cpp').read_text()
        helpers = [
            'Snapshot* runContentsSnapshot(const LLSD& request)',
            'Snapshot* contentsSnapshot(const LLSD& request,std::string& error)',
            'bool testScopeCurrent(const LLSD& scope,std::string& error,bool* pending=nullptr)',
            'bool refreshTestMutationSnapshot(const std::shared_ptr<TestSession>& run,LLSD& child,std::string& error)',
        ]
        source = STUBS + ADAPTER_STUBS + ''.join(function(text, name) for name in helpers)
        self.compile_and_run(source + SNAPSHOT_CASES)

    def test_raw_native_and_recipe_validation(self):
        text = (ROOT / 'fsobjecttools.cpp').read_text()
        functions = [
            'bool id(const LLSD& value)',
            'bool interactionOp(const std::string& op)',
            'bool testOp(const std::string& op)',
            'bool mutationRead(const std::string& op)',
            'bool interactionArguments(const LLSD& request,std::string& error)',
            'bool testArguments(const LLSD& request,std::string& error)',
            'bool mutationArguments(const LLSD& request,std::string& error)',
            'LLSD testChild(const LLSD& request,S32 index)',
            'bool testScopeCurrent(const LLSD& scope,std::string& error,bool* pending=nullptr)',
            'bool diagnosticReceiveAllowed(const LLUUID& sender,S32 type,bool emote)',
        ]
        source = STUBS + ''.join(function(text, name) for name in functions) + CASES
        self.compile_and_run(source)

    def test_native_boundary_contracts(self):
        text = (ROOT / 'fsobjecttools.cpp').read_text()
        for signature in ('bool prepareMutation(FSAssistantPreparedOperation& operation,std::string& error)',
                          'bool prepareInteraction(FSAssistantPreparedOperation& operation,std::string& error)',
                          'bool prepareTest(FSAssistantPreparedOperation& operation,std::string& error)'):
            body = function(text, signature)
            self.assertIn('Arguments(', body)
        prepare = function(text, 'bool prepareTest(FSAssistantPreparedOperation& operation,std::string& error)')
        self.assertLess(prepare.index('mutationArguments(child,error)'), prepare.index('prepareInteraction(prepared,error)'))
        tick = function(text, 'void tickTests()')
        self.assertLess(tick.index('run->observation.action('), tick.index('dispatchInteraction(child)'))
        self.assertIn('ContentsRead>(child,prim,false)', tick)
        self.assertIn('run->observer && !run->observer->done', tick)
        self.assertLess(tick.index('if(scopePending)return;'), tick.index('if(action=="wait_output")'))
        viewer = (ROOT / 'llviewerobject.cpp').read_text()
        self.assertIn('loadTaskInvLLSD(result, !potentially_stale)', viewer)
        bridge = (ROOT / 'fseventapibridge.cpp').read_text()
        self.assertIn('durableOperationSeconds(api,op,prepared->request["timeout_seconds"]', bridge)
        self.assertIn('input.has("__test_step_ids")', bridge)
        self.assertIn('!fs_assistant::sourceBoundCancellation(api,op)', bridge)
        self.assertTrue('runningQueryPending(prim,item->getUUID())' in text, 'execution changes must check the shared wire fence')
        self.assertLess(tick.index('refreshTestMutationSnapshot(run,child,error)'), tick.index('prepareInteraction(prepared,error)'))
        editor = (ROOT / 'llpreviewscript.cpp').read_text()
        self.assertTrue('fs_object_tools_request_running(object, mItemUUID)' in editor)
        self.assertTrue('_PREHASH_GetScriptRunning' not in editor, 'native queries must share the no-nonce tracker')
        for action in ('onRunningCheckboxClicked', 'onReset'):
            body = function(editor, 'void LLLiveLSLEditor::' + action + ('( LLUICtrl*, void* userdata )' if action == 'onRunningCheckboxClicked' else '(void *userdata)'))
            self.assertIn('fs_object_tools_native_execution_changed', body)
        upload = (ROOT / 'llviewerassetupload.cpp').read_text()
        self.assertIn('fs_object_tools_native_execution_changed', function(upload, 'struct PendingInventoryUpload'))


if __name__ == '__main__':
    unittest.main()
