"""Run production Contents insertion and item-bound asset verification.

Native inventory/HTTP/cache are fixtures; real async authority, receipt storage,
eligibility, submission and verification continuations are compiled unchanged.
No simulator is contacted.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

from test_fscontentsource_native import STUBS
from test_fsobjecttools_native import function

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = r'''
#include "fsobjecttoolsmodel.h"
template<class T>struct LLPointer:std::shared_ptr<T>{using std::shared_ptr<T>::shared_ptr;LLPointer(T* p):std::shared_ptr<T>(p){}operator T*()const{return this->get();}};
using LLNotificationPtr=std::shared_ptr<int>;
struct LLHost{};
struct TaskRegion:Region{LLHost getHost()const{return {};}}taskRegion;
struct LLInventoryObject{using object_list_t=std::vector<std::shared_ptr<InventoryObject>>;};
constexpr int TASK_INVENTORY_ITEM_KEY=0;
int sent=0;bool lastRunning=false;
struct LLViewerObject {
 LLUUID uuid{"prim"};bool dirty=false,complete=true,pending=false;U64 generation=1;LLInventoryObject::object_list_t contents;
 LLUUID getID()const{return uuid;}TaskRegion* getRegion(){return &taskRegion;}
 U64 getInventoryServerGeneration()const{return generation;}
 bool isInventoryDirty()const{return dirty;}bool isInventoryServerComplete()const{return complete;}
 bool isInventoryPending()const{return pending;}
 void getInventoryContents(LLInventoryObject::object_list_t& out){out=contents;}
 InventoryObject* getInventoryObject(const LLUUID& id){for(const auto& p:contents){auto* i=dynamic_cast<LLViewerInventoryItem*>(p.get());if(i&&i->uuid==id)return i;}return nullptr;}
 void saveScript(const LLViewerInventoryItem*,bool run,bool){++sent;lastRunning=run;}
 void updateInventory(LLViewerInventoryItem*,int,bool){++sent;}
 void updateViewerInventoryAsset(LLViewerInventoryItem* i,LLUUID asset){i->asset=asset;}
 void dirtyInventory(){dirty=true;}void requestInventory(){}
}prim;
LLSD objectEvidence(LLViewerObject* p){return llsd::map("object_id",p->uuid,"server_generation",static_cast<int>(p->generation));}
struct LLVOInventoryListener {
 virtual ~LLVOInventoryListener()=default;
 void registerVOInventoryListener(LLViewerObject*,void*){}
 virtual void inventoryChanged(LLViewerObject*,LLInventoryObject::object_list_t*,S32,void*){}
};
std::string currentSource="source";bool editable=true;
std::string source(){return currentSource;}
LLViewerObject* target(const LLUUID& id,std::string&){return editable&&id==prim.uuid?&prim:nullptr;}
LLViewerInventoryItem* agentSource(const LLSD& request,std::string&){return request["source_item"].asUUID()==item.uuid&&item.permissions.owner==gAgent.getID()&&item.permissions.copy?&item:nullptr;}
struct AssetStorage {
 using Callback=void(*)(const LLUUID&,LLAssetType::EType,void*,S32,LLExtStat);
 struct Read{LLUUID task,item;Callback callback;void* data;};
 std::vector<Read> reads;bool immediate=false;LLUUID inlineAsset{"asset"};int total=0;
 void getInvItemAsset(LLHost,const LLUUID&,const LLUUID&,const LLUUID&,LLUUID task,LLUUID item,const LLUUID&,LLAssetType::EType,Callback cb,void* data,bool){
  ++total;if(immediate)cb(inlineAsset,LLAssetType::AT_LSL_TEXT,data,0,0);else reads.push_back({task,item,cb,data});
 }
 void complete(std::size_t index,const LLUUID& asset,int status=0){auto r=reads.at(index);reads.erase(reads.begin()+index);r.callback(asset,LLAssetType::AT_LSL_TEXT,r.data,status,0);}
}storage;
AssetStorage* gAssetStorage=&storage;
struct LLLiveLSLEditor{bool assistantSourceBusy(){return true;}bool invalidateAssistantSource(void*){return false;}void* getEditor(){return nullptr;}};
struct LLFloaterReg{static inline bool busy=false;template<class T>static T* findTypedInstance(const char*,const LLSD&){static T editor;return busy?&editor:nullptr;}};
'''

SERVICE = r'''
struct Snapshot {std::string source,epoch;LLSD object;double expires=0.;std::map<LLUUID,LLPointer<LLViewerInventoryItem>> items;};
struct Service{U64 privacyEpoch=0;fs_object_tools::Receipts<std::shared_ptr<Mutation>> operations{2};std::map<LLUUID,Snapshot> snapshots;std::vector<std::shared_ptr<RunningRead>> runningReads;};
double clockValue=10.;double now(){return clockValue;}
void reply(const LLSD&,const LLSD&){}
Snapshot* runContentsSnapshot(const LLSD&){return nullptr;}

Service& service(){static Service s;return s;}
bool liveMutation(const std::shared_ptr<Mutation>& p){return p->source==source()&&editable&&item.permissions.copy&&llsd_equals(itemEvidence(&item),p->item);}
auto beginPut(LLUUID asset=LLUUID("asset"),LLAssetType::EType type=LLAssetType::AT_LSL_TEXT){
 assert(storage.reads.empty());service()=Service();prim=LLViewerObject();item=LLViewerInventoryItem();item.uuid=LLUUID("source-item");item.asset=asset;item.type=type;
 currentSource="source";editable=true;sent=0;clockValue=10.;storage=AssetStorage();gRlvHandler.script=false;
 auto p=std::make_shared<Mutation>();p->key="operation";p->source=source();p->epoch="epoch";p->before=objectEvidence(&prim);p->deadline=now()+25.;p->request=llsd::map("op","put","object_id",prim.uuid,"source_item",item.uuid,"running",false);
 p->item=itemEvidence(&item);p->task=std::make_shared<fs_assistant::AsyncTask>(guard());p->receipt=llsd::map("state","queued","operation_id",LLUUID("put-operation"));
 service().operations.admit(p->key,"material",p)->pending=1;return p;
}
LLViewerInventoryItem* addItem(const std::string& id,LLUUID asset=LLUUID()){
 auto p=std::make_shared<LLViewerInventoryItem>();p->uuid=LLUUID(id);p->asset=asset;p->type=item.type;prim.contents.push_back(p);return p.get();
}
void observe(const std::shared_ptr<Mutation>& p){++prim.generation;p->inventoryChanged(&prim,&prim.contents,0,nullptr);}
bool confirmed(const std::shared_ptr<Mutation>& p){return p->receipt["destination_present"].asBoolean();}
'''

CASES_SUCCESS = r'''
int main(){
 auto p=beginPut();enqueueContentsPut(p);assert(sent==1&&!lastRunning&&storage.total==0);
 addItem("installed");observe(p);assert(!confirmed(p)&&storage.reads.size()==1&&p->insertionReads==1);
 assert(service().operations.find(p->key)->pending==2&&storage.reads.front().task==prim.uuid);
 storage.complete(0,LLUUID("asset"));assert(confirmed(p)&&p->receipt["task_item_id"].asUUID()==LLUUID("installed"));
 assert(p->finished&&!p->task&&service().operations.find(p->key)->pending==0&&item.asset==LLUUID("asset"));
 // Resolve an undisclosed source UUID BEFORE submitting its copy.
 p=beginPut(LLUUID());enqueueContentsPut(p);assert(sent==0&&storage.reads.size()==1&&storage.reads.front().task.isNull());
 storage.complete(0,LLUUID("asset"));assert(sent==1&&p->insertionAsset==LLUUID("asset")&&item.asset.isNull());
 addItem("installed");observe(p);storage.complete(0,LLUUID("asset"));assert(confirmed(p));
 // Visible object assets retain the ordinary no-download insertion path.
 p=beginPut(LLUUID("object-asset"),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("nested-object",LLUUID("object-asset"));observe(p);
 assert(confirmed(p)&&sent==1&&storage.total==0&&item.asset==LLUUID("object-asset"));
 // Two hidden candidates: never confirm the first match before checking both.
 p=beginPut();enqueueContentsPut(p);addItem("a");addItem("b");observe(p);assert(storage.reads.size()==2);
 storage.complete(0,LLUUID("asset"));assert(!confirmed(p));storage.complete(0,LLUUID("other"));assert(confirmed(p)&&p->receipt["task_item_id"].asUUID()==LLUUID("a"));
 p=beginPut();enqueueContentsPut(p);addItem("a");addItem("b");observe(p);storage.complete(0,LLUUID("asset"));storage.complete(0,LLUUID("asset"));
 assert(!confirmed(p)&&p->receipt["correlation"].asString()=="ambiguous");
 // Cached/inline callbacks do not race probe registration or double-submit.
 p=beginPut(LLUUID());storage.immediate=true;enqueueContentsPut(p);assert(sent==1&&storage.reads.empty());
 addItem("a");addItem("b");observe(p);assert(!confirmed(p)&&p->receipt["correlation"].asString()=="ambiguous"&&p->insertionReads==0);
 // External/native refresh can clear the cache during the lookup. Keep proof
 // privately, but require the next fresh server snapshot before confirming.
 p=beginPut();enqueueContentsPut(p);addItem("a");observe(p);prim.contents.clear();prim.dirty=false;prim.pending=true;
 storage.complete(0,LLUUID("asset"));assert(!confirmed(p)&&p->insertionProbes.at(LLUUID("a")).asset==LLUUID("asset"));
 addItem("a");prim.pending=false;observe(p);assert(confirmed(p)&&storage.total==1);
 // An optimistic native removal cannot erase the second candidate. The server
 // later rejecting that removal must still leave the insertion ambiguous.
 p=beginPut();enqueueContentsPut(p);addItem("a");addItem("b");observe(p);auto removed=prim.contents.back();prim.contents.pop_back();
 storage.complete(0,LLUUID("asset"));assert(!confirmed(p)&&p->insertionReads==1);
 prim.contents.push_back(removed);observe(p);storage.complete(0,LLUUID("asset"));
 assert(!confirmed(p)&&p->receipt["correlation"].asString()=="ambiguous");
 // Locally changing a visible asset is also not a new server observation.
 p=beginPut();enqueueContentsPut(p);auto* a=addItem("a",LLUUID("other"));addItem("b");observe(p);
 a->asset=LLUUID("asset");storage.complete(0,LLUUID("other"));assert(!confirmed(p));
 a->asset=LLUUID("other");observe(p);assert(!confirmed(p));
}
'''

CASES_REVOKE = r'''
int main(){
 for(int changed=0;changed<5;++changed){
  auto p=beginPut(LLUUID());enqueueContentsPut(p);assert(storage.reads.size()==1);
  if(changed==0)p->task->guard()->cancel("revoked");if(changed==1)item.permissions.modify=false;
  if(changed==2)currentSource="different-login";if(changed==3)++service().privacyEpoch;if(changed==4)editable=false;
  storage.complete(0,LLUUID("asset"));assert(sent==0&&p->insertionReads==0&&!confirmed(p));
 }
 for(int changed=0;changed<5;++changed){
  auto p=beginPut();enqueueContentsPut(p);auto* candidate=addItem("a");observe(p);assert(storage.reads.size()==1);
  if(changed==0)p->task->guard()->cancel("revoked");if(changed==1)candidate->permissions.modify=false;
  if(changed==2)currentSource="different-login";if(changed==3)++service().privacyEpoch;if(changed==4)candidate->asset=LLUUID("different-asset");
  storage.complete(0,LLUUID("asset"));assert(sent==1&&p->insertionReads==0&&!confirmed(p));
 }
 auto p=beginPut(LLUUID());enqueueContentsPut(p);storage.complete(0,LLUUID(),1);
 assert(sent==0&&p->finished&&service().operations.find(p->key)->pending==0);
 // Missing COPY/MODIFY/read authority never becomes an arbitrary asset fetch.
 p=beginPut();enqueueContentsPut(p);addItem("a")->permissions.modify=false;observe(p);
 assert(storage.total==0&&!confirmed(p));
 p=beginPut(LLUUID());item.permissions.modify=false;p->item=itemEvidence(&item);enqueueContentsPut(p);addItem("a")->permissions.modify=false;observe(p);
 assert(sent==1&&storage.total==0&&!confirmed(p)&&p->receipt["correlation"].asString()=="source_asset_undisclosed");
}
'''

CASES_BOUNDS = r'''
int main(){
 auto p=beginPut();enqueueContentsPut(p);for(int i=0;i<9;++i)addItem("candidate-"+std::to_string(i));observe(p);
 assert(storage.reads.size()==8&&p->insertionProbes.size()==8&&!confirmed(p));
 for(int i=0;i<8;++i)storage.complete(0,LLUUID("asset"));observe(p);
 assert(storage.total==8&&p->insertionReads==0&&!confirmed(p));
 // Late read callbacks must keep the cancelled receipt from eviction.
 p=beginPut();enqueueContentsPut(p);addItem("a");observe(p);assert(storage.reads.size()==1);
 p->task->guard()->cancel("revoked");settle(p,"submitted_unknown","revoked",true);
 assert(service().operations.find(p->key)->pending==2);
 auto other=std::make_shared<Mutation>();service().operations.admit("other","args",other)->terminal=true;
 assert(service().operations.admit("new","args",other)&&service().operations.find(p->key));
 storage.complete(0,LLUUID("asset"));assert(!confirmed(p)&&p->insertionReads==0&&service().operations.find(p->key)->pending==1);
}
'''

CASES_LOCK = r'''
int main(){
 // A newer authoritative snapshot removes B after A is verified. B's read
 // still owns its receipt, but can no longer affect this completed insertion.
 auto p=beginPut();p->receipt["operation_id"]=LLUUID("put-receipt");enqueueContentsPut(p);
 addItem("a");addItem("b");observe(p);storage.complete(0,LLUUID("asset"));
 std::string error;assert(!primMutationAvailable(prim.uuid,error));
 assert(error.find("operation_id=put-receipt; op=put; state=submitted_unknown; reason=awaiting_native_observation")!=std::string::npos);
 prim.contents.pop_back();observe(p);auto* entry=service().operations.find(p->key);
 assert(confirmed(p)&&p->finished&&!p->task&&p->insertionReads==1&&entry->pending==1);
 assert(!entry->blocksMutation()&&primMutationAvailable(prim.uuid,error));
 auto next=std::make_shared<Mutation>();next->request=llsd::map("object_id",prim.uuid,"op","rename");
 next->receipt=llsd::map("operation_id",LLUUID("next-receipt"),"state","queued");next->key="next";
 auto* nextEntry=service().operations.admit(next->key,"args",next);nextEntry->pending=1;
 assert(!primMutationAvailable(prim.uuid,error)&&error.find("next-receipt")!=std::string::npos);
 // Duplicate inventory delivery and a late obsolete read cannot overwrite the
 // settled receipt, acquire the lock again, or release the newer mutation.
 for(int i=0;i<65;++i)addItem("later-"+std::to_string(i));observe(p);
 assert(p->receipt["correlation"].asString()=="fresh_contents_verified_asset");
 storage.complete(0,LLUUID("asset"));
 assert(confirmed(p)&&p->receipt["task_item_id"].asUUID()==LLUUID("a")&&entry->pending==0);
 assert(!entry->blocksMutation()&&nextEntry->blocksMutation()&&!primMutationAvailable(prim.uuid,error));
 settle(next,"simulator_observed","done");assert(primMutationAvailable(prim.uuid,error));
 // Separate callbacks for duplicate native reads each drain their own lease.
 p=beginPut();enqueueContentsPut(p);auto* a=addItem("a");observe(p);
 assert(readInsertionAsset(p,&prim,a,false)&&p->insertionReads==2);
 storage.complete(0,LLUUID("asset"));assert(confirmed(p)&&p->insertionReads==1);
 storage.complete(0,LLUUID("other"));assert(confirmed(p)&&p->insertionReads==0&&service().operations.find(p->key)->pending==0);
 // Revoke before submission: retain its read, but it cannot send a write.
 p=beginPut(LLUUID());enqueueContentsPut(p);p->task->guard()->cancel("revoked");
 settle(p,"cancelled_before_submission","revoked");assert(primMutationAvailable(prim.uuid,error));
 storage.complete(0,LLUUID("asset"));assert(sent==0&&primMutationAvailable(prim.uuid,error));
 // Revocation/timeout after a write remains exclusive even after reads drain.
 for(const char* op:{"put","scriptSave","scriptRunning","take","takeCopy"}){
  p=beginPut();enqueueContentsPut(p);addItem("a");observe(p);p->request["op"]=op;
  p->task->guard()->cancel("revoked");settle(p,"submitted_unknown","revoked",true);
  assert(!primMutationAvailable(prim.uuid,error)&&error.find("submitted_outcome_unresolved")!=std::string::npos);
  storage.complete(0,LLUUID("asset"));assert(!primMutationAvailable(prim.uuid,error));
 }
 // Account changes clear the journal. An old read with the same request key
 // must neither decrement nor unlock the new login's entry.
 p=beginPut();enqueueContentsPut(p);addItem("a");observe(p);
 currentSource="another-login";service().operations.clear();
 next=std::make_shared<Mutation>();next->request=llsd::map("object_id",prim.uuid);next->key=p->key;
 nextEntry=service().operations.admit(next->key,"args",next);nextEntry->pending=1;
 storage.complete(0,LLUUID("asset"));assert(nextEntry->pending==1&&nextEntry->blocksMutation()&&!confirmed(p));
 // Late authoritative rename/remove evidence can still reconcile a timed-out
 // submitted write, without making another native submission.
 for(const char* op:{"rename","remove"}){
  p=beginPut();p->request["op"]=op;p->request["item_id"]=LLUUID("a");p->request["name"]="02-Gameplay";
  p->observe(&prim);p->submitted=true;p->task->guard()->cancel("timeout");settle(p,"submitted_unknown","timeout",true);
  assert(!primMutationAvailable(prim.uuid,error));
  if(std::string(op)=="rename")addItem("a");observe(p);
  assert(primMutationAvailable(prim.uuid,error)&&service().operations.find(p->key)->pending==0&&sent==0);
 }
}
'''

CASES_SAVE_LOCK = r'''
int main(){
 auto beginSave=[](bool bytes){auto p=beginPut();p->request["op"]="scriptSave";p->request["text"]="private source";
  auto yes=[](){return true;};assert(p->progress.before("capability",true,yes,yes));
  if(bytes)assert(p->progress.before("source",true,yes,yes));p->submitted=true;return p;};
 std::string error;
 auto p=beginSave(false);finishTaskUpload(p,LLSD(),false,"capability failed");
 assert(p->receipt["source_saved_known"].asBoolean()&&!p->receipt["source_saved"].asBoolean()&&primMutationAvailable(prim.uuid,error));
 p=beginSave(true);finishTaskUpload(p,LLSD(),false,"result lost");
 assert(!p->receipt["source_saved_known"].asBoolean()&&!primMutationAvailable(prim.uuid,error));
 assert(service().operations.find(p->key)->unresolved&&!p->task&&!p->request.has("text"));
 const auto completed=llsd::map("state","complete","new_asset",LLUUID("saved"),"compiled",true);
 finishTaskUpload(p,completed,true,"");assert(!primMutationAvailable(prim.uuid,error)); // Duplicate completion cannot rewrite an uncertain receipt.
 for(int changed=0;changed<3;++changed){
  p=beginSave(true);LLFloaterReg::busy=changed==1;
  if(changed==2)p->task->guard()->cancel("revoked");
  finishTaskUpload(p,completed,changed==0,"");
  assert(p->receipt["source_saved_known"].asBoolean()&&p->receipt["source_saved"].asBoolean()&&primMutationAvailable(prim.uuid,error));
  finishTaskUpload(p,LLSD(),false,"late failure");assert(primMutationAvailable(prim.uuid,error));
 }
}
'''

VALIDATION_FIXTURES = r'''
struct FSAssistantPreparedOperation{LLSD request,validation;};
std::shared_ptr<Mutation> ownMutation(const LLSD&){auto* entry=service().operations.find("operation");return entry?entry->value:nullptr;}
std::string mutationSignature(const LLSD& request){return request.has("material")?request["material"].asString():"material";}
std::shared_ptr<Mutation> receiptOperation(const LLSD& request,std::string&){return ownMutation(request);}
bool prepareMutation(FSAssistantPreparedOperation&,std::string&){return false;}
'''
CASES_TERMINAL = r'''
int main(){
 auto p=beginPut();p->request["op"]="scriptSave";p->signature="material";
 FSAssistantPreparedOperation prepared;prepared.request=p->request;prepared.validation=llsd::map("source",source());
 std::string error;assert(validateMutation(prepared,error));
 p->progress.finish();editable=false; // Completion fence/target changes no longer revoke the settled receipt.
 bool permitted=true;auto operation=std::make_shared<fs_assistant::AsyncOperation>([&](){return permitted?std::string():std::string("revoked");});
 operation->checkWith([&](){std::string e;return validateMutation(prepared,e)?std::string():std::string("source_changed");});
 assert(operation->allowed());
 assert(!p->progress.before("capability",true,[](){return true;},[](){return true;}));
 permitted=false;assert(!operation->allowed());permitted=true;assert(!operation->allowed());
 currentSource="another-login";error.clear();assert(!validateMutation(prepared,error));currentSource="source";
 prepared.request["material"]="different";error.clear();assert(!validateMutation(prepared,error));
 // Active saves retain the full live validator.
 p=beginPut();p->request["op"]="scriptSave";p->signature="material";prepared.request=p->request;editable=false;
 error.clear();assert(!validateMutation(prepared,error));
}
'''



RECONCILE_FIXTURES = r'''
struct FSAssistantPreparedOperation{LLSD request,validation,summary;std::vector<std::string> requiredClasses;};
std::string ll_stream_notation_sd(const LLSD& v){
 std::string s=std::to_string(v.value.index())+":";
 if(v.isMap()){for(const auto& e:std::get<LLSD::Map>(v.value))s+=e.first+"="+ll_stream_notation_sd(e.second)+";";}
 else s+=v.asString();return s;
}
std::string mutationSignature(const LLSD&);
std::shared_ptr<Mutation> receiptOperation(const LLSD&,std::string&);
bool preparePutReconciliation(FSAssistantPreparedOperation&,std::string&);
void dispatchPutReconciliation(const LLSD&);
bool prepareMutation(FSAssistantPreparedOperation& op,std::string& error){
 op.validation=llsd::map("source",source(),"material",mutationSignature(op.request));return preparePutReconciliation(op,error);
}
LLSD latestResponse;
namespace LLEventAPI{struct Response{
 Response(LLSD,const LLSD&){}void error(const std::string& s){latestResponse=llsd::map("error",s);}
 void setResponse(const LLSD& s){latestResponse=s;}
};}
LLSD publicReceipt(const std::shared_ptr<Mutation>& p){auto r=p->receipt;const auto* e=service().operations.find(p->key);
 r["mutation_blocking"]=e->blocksMutation();r["retained_pending"]=static_cast<int>(e->pending);return r;}
auto snapshot(){auto key=LLUUID::generateNewID();Snapshot s;s.source=source();s.epoch="epoch";s.object=objectEvidence(&prim);s.expires=now()+120.;
 for(const auto& raw:prim.contents){auto* i=dynamic_cast<LLViewerInventoryItem*>(raw.get());s.items.emplace(i->uuid,new LLViewerInventoryItem(i));}
 service().snapshots.emplace(key,std::move(s));return key;
}
auto choose(const std::shared_ptr<Mutation>& p,const char* chosen="new-object",const char* nonce="reconcile-request"){
 return llsd::map("op","reconcilePut","object_id",prim.uuid,"operation_id",p->receipt["operation_id"],"item_id",LLUUID(chosen),"snapshot_id",snapshot(),"client_request_id",LLUUID(nonce),"__assistant_epoch","epoch");
}
bool prepare(const LLSD& request){std::string error;FSAssistantPreparedOperation op;op.request=request;return prepareMutation(op,error);}
void reconcile(const LLSD& request){latestResponse=LLSD();dispatchPutReconciliation(request);}
'''

CASES_RECONCILE = r'''
int main(){
 // Ordinary nested object: source item, source asset and task item identities
 // are distinct. A sole new hidden-asset item is observed, never auto-matched.
 auto p=beginPut(LLUUID(),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("new-object");observe(p);
 assert(sent==1&&storage.total==0&&!confirmed(p)&&p->receipt["correlation"].asString()=="source_asset_undisclosed");
 std::string error;assert(!primMutationAvailable(prim.uuid,error)&&error.find("object_contents_reconcile")!=std::string::npos);
 auto request=choose(p);FSAssistantPreparedOperation prepared;prepared.request=request;assert(prepareMutation(prepared,error));
 reconcile(request);assert(!latestResponse.has("error"));auto* e=service().operations.find(p->key);
 assert(sent==1&&e->terminal&&!e->unresolved&&!e->blocksMutation()&&e->pending==0);
 assert(p->receipt["operation_id"].asUUID()==LLUUID("put-operation")&&p->receipt["state"].asString()=="associated_unverified");
 assert(p->receipt["state_before_reconciliation"].asString()=="submitted_unknown"&&p->receipt["correlation_before_reconciliation"].asString()=="source_asset_undisclosed");
 assert(p->receipt["copy_outcome"].asString()=="unknown"&&!p->receipt["asset_identity_verified"].asBoolean());
 assert(p->receipt["task_item_id"].asUUID()==LLUUID("new-object")&&primMutationAvailable(prim.uuid,error));
 assert(validateMutation(prepared,error)); // Completion must not revoke its own local receipt.
 const auto receipt=p->receipt;clockValue+=500.;prim.contents.clear();
 for(int i=0;i<50;++i){reconcile(request);assert(!latestResponse.has("error")&&p->receipt==receipt&&sent==1);}
 auto changed=request;changed["item_id"]=LLUUID("other");assert(!prepare(changed));
 changed=request;changed["client_request_id"]=LLUUID("different-request");assert(!prepare(changed));
 assert(!service().operations.admit("reconcile-request","args",std::make_shared<Mutation>()));
 submitContentsPut(p);assert(sent==1); // The original copy cannot be submitted twice.
 // Multiple candidates and manual additions require a deliberate exact choice.
 p=beginPut(LLUUID(),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("manual-object");addItem("new-object");observe(p);
 assert(!confirmed(p)&&!primMutationAvailable(prim.uuid,error));request=choose(p,"new-object");
 reconcile(request);assert(!latestResponse.has("error")&&p->receipt["task_item_id"].asUUID()==LLUUID("new-object")&&sent==1);
 // Predating items, the source inventory UUID, wrong types and known mismatches
 // cannot be used as destination identities.
 p=beginPut(LLUUID(),LLAssetType::AT_OBJECT);addItem("old");enqueueContentsPut(p);addItem("new-object");observe(p);
 assert(!prepare(choose(p,"old"))&&!prepare(choose(p,"source-item")));
 auto* candidate=dynamic_cast<LLViewerInventoryItem*>(prim.contents.back().get());candidate->type=LLAssetType::AT_LSL_TEXT;
 assert(!prepare(choose(p)));candidate->type=LLAssetType::AT_OBJECT;candidate->permissions.owner=LLUUID("someone-else");assert(!prepare(choose(p)));
 p=beginPut(LLUUID("known-asset"),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("new-object",LLUUID("other-asset"));observe(p);assert(!prepare(choose(p)));
 // A hidden script UUID can already be known through an item-bound read.
 // Recovery cannot contradict that evidence, including after its timeout.
 for(int timedOut=0;timedOut<2;++timedOut){
  p=beginPut();enqueueContentsPut(p);addItem("new-object");observe(p);storage.complete(0,LLUUID("other-asset"));
  assert(!confirmed(p)&&p->insertionReads==0&&p->insertionProbes.at(LLUUID("new-object")).asset==LLUUID("other-asset"));
  if(timedOut){clockValue=p->deadline+1.;tickMutations();}
  request=choose(p);assert(!prepare(request));const auto originalReceipt=p->receipt;reconcile(request);
  assert(latestResponse["error"].asString().find("asset_mismatch")!=std::string::npos&&p->receipt==originalReceipt&&sent==1);
  assert(!p->insertionAssociation.isMap()&&service().operations.requestAvailable("reconcile-request")&&!primMutationAvailable(prim.uuid,error));
 }
 // A local optimistic insertion alone is not a newer authoritative snapshot.
 p=beginPut(LLUUID(),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("new-object");request=choose(p);assert(!prepare(request));
 observe(p);request=choose(p);prepared=FSAssistantPreparedOperation();prepared.request=request;assert(prepareMutation(prepared,error));
 ++prim.generation;assert(!validateMutation(prepared,error));assert(!prepare(request));
 request=choose(p);clockValue+=121.;assert(!prepare(request));clockValue-=121.;
 request=choose(p);prim.dirty=true;assert(!prepare(request));prim.dirty=false;prim.pending=true;assert(!prepare(request));prim.pending=false;
 prim.complete=false;assert(!prepare(request));prim.complete=true;
 request=choose(p);dynamic_cast<LLViewerInventoryItem*>(prim.contents.back().get())->asset=LLUUID("local-optimistic-change");assert(!prepare(request));
 // Real timeout/authority handling retains submitted uncertainty. A fresh
 // independent authorized association can still resolve the exclusive lock.
 for(int revoke=0;revoke<2;++revoke){
  p=beginPut(LLUUID(),LLAssetType::AT_OBJECT);enqueueContentsPut(p);
  if(revoke)p->task->guard()->cancel("revoked");else clockValue=p->deadline+1.;tickMutations();
  assert(p->finished&&service().operations.find(p->key)->unresolved&&!primMutationAvailable(prim.uuid,error));
  addItem("new-object");++prim.generation;request=choose(p);reconcile(request);
  assert(!latestResponse.has("error")&&primMutationAvailable(prim.uuid,error)&&sent==1);tickMutations();assert(primMutationAvailable(prim.uuid,error));
 }
 // Authorization can change after preparation. No local association or request
 // reservation may happen under the revoked guard.
 p=beginPut(LLUUID(),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("new-object");observe(p);request=choose(p);
 bool allowed=true;auto authority=std::make_shared<fs_assistant::AsyncOperation>([&](){return allowed?std::string():std::string("revoked");});
 {fs_assistant::AsyncScope scope(authority);allowed=false;reconcile(request);assert(latestResponse.has("error"));}
 assert(!p->insertionAssociation.isMap()&&service().operations.requestAvailable("reconcile-request"));
 editable=false;assert(!prepare(request));editable=true;
 changed=request;changed["__assistant_epoch"]="wrong-epoch";assert(!prepare(changed));
 currentSource="different-login";assert(!prepare(request));currentSource="source";
 auto token=request["snapshot_id"].asUUID();service().snapshots.at(token).epoch="wrong-epoch";assert(!prepare(request));
 // Capacity and delayed verification reads are independent of the original
 // write lock. Recovery needs no new live mutation/receipt slot.
 p=beginPut();enqueueContentsPut(p);addItem("new-object");observe(p);assert(p->insertionReads==1);
 auto other=std::make_shared<Mutation>();other->request=llsd::map("object_id",prim.uuid,"op","rename");other->receipt=llsd::map("operation_id",LLUUID("newer"));
 service().operations.admit("other","args",other)->pending=1;
 request=choose(p);reconcile(request);assert(!latestResponse.has("error")&&service().operations.entries().size()==2);
 e=service().operations.find(p->key);assert(!e->blocksMutation()&&e->pending==1&&!primMutationAvailable(prim.uuid,error));
 const auto selectedReceipt=p->receipt;storage.complete(0,LLUUID("asset"));
 assert(e->pending==0&&p->receipt==selectedReceipt&&service().operations.find("other")->blocksMutation());
 // Account turnover and a reused request key cannot let an old callback alter
 // the new login's receipt or decrement its pending count.
 p=beginPut();enqueueContentsPut(p);addItem("new-object");observe(p);request=choose(p);reconcile(request);
 currentSource="next-login";service().operations.clear();other=std::make_shared<Mutation>();
 auto* replacement=service().operations.admit(p->key,"args",other);replacement->pending=1;
 storage.complete(0,LLUUID("asset"));assert(replacement->pending==1&&replacement->blocksMutation()&&p->receipt["state"].asString()=="associated_unverified");
 // Already verified insertions cannot be downgraded to unverified association.
 p=beginPut(LLUUID("asset"),LLAssetType::AT_OBJECT);enqueueContentsPut(p);addItem("new-object",LLUUID("asset"));observe(p);
 assert(confirmed(p)&&p->receipt["asset_identity_verified"].asBoolean()&&!prepare(choose(p)));
}
'''

class ContentsPutNativeTests(unittest.TestCase):
    def run_native(self, cases, terminal=False, reconcile=False):
        text=(ROOT/'fsobjecttools.cpp').read_text()
        source=STUBS+FIXTURES
        source+=function(text,'const char* scriptReadBlocker(const LLViewerInventoryItem* item)')
        source+=function(text,'bool scriptReadable(const LLViewerInventoryItem* item)')
        source+=function(text,'LLSD itemEvidence(const LLViewerInventoryItem* item)')
        source+=function(text,'struct Mutation:LLVOInventoryListener')+';\n'+function(text,'struct RunningRead')+';\n'+SERVICE
        for name in ['void settle(const std::shared_ptr<Mutation>& value,const std::string& state,const std::string& reason,bool pending=false)',
                     'std::string mutationBlockReason(const fs_object_tools::Receipts<std::shared_ptr<Mutation>>::Entry& entry)',
                     'std::string mutationBusy(const fs_object_tools::Receipts<std::shared_ptr<Mutation>>::Entry& entry)',
                     'bool primMutationAvailable(const LLUUID& object,std::string& error)',
                     'void submitContentsPut(const std::shared_ptr<Mutation>& value)',
                     'struct InsertionRead',
                     'void insertionAssetLoaded(const LLUUID& asset,LLAssetType::EType,void* userdata,S32 status,LLExtStat)',
                     'bool readInsertionAsset(const std::shared_ptr<Mutation>& value,LLViewerObject* prim,LLViewerInventoryItem* item,bool agent)',
                     'void enqueueContentsPut(const std::shared_ptr<Mutation>& value)',
                     'void Mutation::checkInsertion(LLViewerObject* prim)',
                     'void Mutation::inventoryChanged(LLViewerObject* prim,LLInventoryObject::object_list_t* contents,S32,void*)',
                     'void finishTaskUpload(const std::shared_ptr<Mutation>& value,const LLSD& result,bool success,const std::string& reason)']:
            source+=function(text,name)+(';' if name.startswith('struct ') else '')+'\n'
        if reconcile:
            source+=RECONCILE_FIXTURES
            for name in ['LLSD mutationMaterial(const LLSD& request)',
                         'std::string mutationSignature(const LLSD& request)',
                         'std::shared_ptr<Mutation> ownMutation(const LLSD& request)',
                         'std::shared_ptr<Mutation> receiptOperation(const LLSD& request,std::string& error)',
                         'Snapshot* contentsSnapshot(const LLSD& request,std::string& error)',
                         'bool preparePutReconciliation(FSAssistantPreparedOperation& operation,std::string& error)',
                         'void dispatchPutReconciliation(const LLSD& request)',
                         'bool validateMutation(const FSAssistantPreparedOperation& operation,std::string& error)',
                         'void tickMutations()']:
                source+=function(text,name)+'\n'
        if terminal:
            source+=VALIDATION_FIXTURES+function(text,'bool validateMutation(const FSAssistantPreparedOperation& operation,std::string& error)')
        source+=cases
        with tempfile.TemporaryDirectory(prefix='fs-contents-put-') as directory:
            cpp=Path(directory)/'test.cpp';cpp.write_text(source);exe=cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-UNDEBUG','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_native_insertion_visible_hidden_and_ambiguous_assets(self):
        self.run_native(CASES_SUCCESS)

    def test_native_insertion_revocation_stale_sources_and_read_permissions(self):
        self.run_native(CASES_REVOKE)

    def test_native_insertion_bounded_reads_and_receipt_retention(self):
        self.run_native(CASES_BOUNDS)

    def test_completed_insertion_releases_lock_with_retained_callbacks(self):
        self.run_native(CASES_LOCK)

    def test_save_lock_distinguishes_transport_completion_from_known_outcome(self):
        self.run_native(CASES_SAVE_LOCK)

    def test_hidden_object_requires_explicit_fresh_server_association(self):
        self.run_native(CASES_RECONCILE, reconcile=True)

    def test_terminal_save_status_keeps_source_and_permission_gates(self):
        self.run_native(CASES_TERMINAL, terminal=True)


if __name__=='__main__':unittest.main()
