"""Run production source eligibility, download callbacks and edit validators.

Native transport, inventory storage and LLSD are bounded fixtures. Real source
policy and async authority are used; neither fixture invents an empty source for
an unknown asset. These checks do not replace simulator/Windows acceptance.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

from test_fsobjecttools_native import function

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include "fscontentpolicy.h"
#include "fscontentreadpolicy.h"
#include "fsassistantasync.h"
#include "fscontentupload.h"
#include <cassert>
#include <cstring>
#include <map>
#include <sstream>
#include <variant>
using S32=int;using U64=std::uint64_t;using U8=unsigned char;using LLExtStat=int;
struct LLUUID {
 std::string value;LLUUID()=default;explicit LLUUID(std::string s):value(std::move(s)){}
 bool isNull()const{return value.empty()||value=="00000000-0000-0000-0000-000000000000";}bool notNull()const{return !isNull();}
 bool operator==(const LLUUID& b)const{return (isNull()&&b.isNull())||value==b.value;}
 bool operator!=(const LLUUID& b)const{return !(*this==b);}bool operator<(const LLUUID& b)const{return value<b.value;}
 std::string asString()const{return value;}
 static LLUUID generateNewID(){static int next=0;return LLUUID("snapshot-"+std::to_string(++next));}
 static const LLUUID null;
 static bool validate(const std::string& s){if(s.size()!=36)return false;for(std::size_t i=0;i<s.size();++i){
  if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
  else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')||(s[i]>='A'&&s[i]<='F')))return false;}return true;}
};
const LLUUID LLUUID::null;
struct LLSD {
 using Integer=int;
 using Map=std::map<std::string,LLSD>;using Array=std::vector<LLSD>;
 std::variant<std::monostate,bool,int,double,std::string,LLUUID,Map,Array> value;
 LLSD()=default;LLSD(bool v):value(v){}LLSD(int v):value(v){}LLSD(double v):value(v){}
 LLSD(const char* v):value(std::string(v)){}LLSD(std::string v):value(std::move(v)){}LLSD(LLUUID v):value(std::move(v)){}
 bool isString()const{return std::holds_alternative<std::string>(value);}
 bool isUUID()const{return std::holds_alternative<LLUUID>(value);}bool isUndefined()const{return std::holds_alternative<std::monostate>(value);}
 bool isInteger()const{return std::holds_alternative<int>(value);}int asInteger()const{return isInteger()?std::get<int>(value):0;}
 bool isMap()const{return std::holds_alternative<Map>(value);}bool isArray()const{return std::holds_alternative<Array>(value);}
 std::string asString()const{return isString()?std::get<std::string>(value):std::holds_alternative<LLUUID>(value)?std::get<LLUUID>(value).asString():std::string();}
 LLUUID asUUID()const{return LLUUID(asString());}bool asBoolean()const{return std::holds_alternative<bool>(value)&&std::get<bool>(value);}
 LLSD& operator[](const std::string& k){if(!isMap())value=Map();return std::get<Map>(value)[k];}
 const LLSD& operator[](const std::string& k)const{static LLSD empty;return has(k)?std::get<Map>(value).at(k):empty;}
 bool has(const std::string& k)const{return isMap()&&std::get<Map>(value).count(k);}
 bool operator==(const LLSD& b)const{return value==b.value;}bool operator!=(const LLSD& b)const{return !(*this==b);}
 static LLSD emptyArray(){LLSD v;v.value=Array();return v;}void append(LLSD v){if(!isArray())value=Array();std::get<Array>(value).push_back(std::move(v));}
};
namespace llsd {
 LLSD map(){LLSD v;v.value=LLSD::Map();return v;}
 template<class K,class V,class... Rest> LLSD map(K key,V value,Rest... rest){auto result=map(rest...);result[key]=value;return result;}
}
bool llsd_equals(const LLSD& a,const LLSD& b){return a==b;}
namespace LLAssetType {enum EType {AT_UNKNOWN,AT_LSL_TEXT,AT_NOTECARD};const char* lookup(EType t){return t==AT_LSL_TEXT?"lsltext":"notecard";}}
enum {PERM_COPY,PERM_MODIFY,GP_OBJECT_MANIPULATE,RLV_BHVR_VIEWSCRIPT,RLV_BHVR_VIEWNOTE};
struct Permissions {LLUUID owner{"account"};bool copy=true,modify=true;LLUUID getOwner()const{return owner;}};
struct InventoryObject {virtual ~InventoryObject()=default;};
struct LLViewerInventoryItem:InventoryObject {
 LLUUID uuid{"item"},asset;Permissions permissions;bool link=false,finished=true;LLAssetType::EType type=LLAssetType::AT_LSL_TEXT;
 LLUUID getUUID()const{return uuid;}LLUUID getAssetUUID()const{return asset;}
 LLAssetType::EType getActualType()const{return type;}LLAssetType::EType getType()const{return type;}
 const Permissions& getPermissions()const{return permissions;}bool getIsLinkType()const{return link;}bool isFinished()const{return finished;}
 std::string getName()const{return "02-Gameplay";}
 void asLLSD(LLSD& value)const{value=llsd::map("item_id",uuid,"asset_id",asset,"owner",permissions.owner,"copy",permissions.copy,"modify",permissions.modify);}
}item;
struct Region {std::string getCapability(const char*)const{return "capability";}}region;
struct Agent {
 LLUUID account{"account"},login{"login"};LLUUID getID()const{return account;}LLUUID getSessionID()const{return login;}
 bool allowOperation(int op,const Permissions& p,int)const{return op==PERM_COPY?p.copy:p.modify;}
 Region* getRegion()const{return &region;}
}gAgent;
struct rlv_handler_t {static bool isEnabled(){return true;}};
struct Restrictions {bool script=false,note=false;bool hasBehaviour(int b)const{return b==RLV_BHVR_VIEWSCRIPT?script:note;}}gRlvHandler;
struct Inventory {LLViewerInventoryItem* getItem(const LLUUID& id){return id==item.uuid?&item:nullptr;}}gInventory;
namespace fs_content {
 constexpr std::size_t textLimit=65536,assetLimit=262144;
 bool agentInventory(const LLUUID&){return true;}bool library(const LLUUID&){return false;}
 // Hashing is a fixture boundary; production snapshot validation is unchanged.
 std::string hash(const std::string& text){std::string result(64,'0');auto value=std::hash<std::string>{}(text);
  for(std::size_t i=0;i<sizeof(value)*2;++i){result[i]="0123456789abcdef"[value&15];value>>=4;}return result;}
 LLSD evidence(const LLViewerInventoryItem* p){LLSD v;p->asLLSD(v);v["account"]=gAgent.getID();v["session"]=gAgent.getSessionID();v["grid"]="grid";return v;}
}
struct LLFileSystem {
 static inline std::map<LLUUID,std::string> assets;static inline bool shortRead=false;
 LLUUID asset;int last=0;LLFileSystem(LLUUID a,LLAssetType::EType):asset(std::move(a)){}
 int getSize()const{return assets.count(asset)?static_cast<int>(assets.at(asset).size()):-1;}
 bool read(U8* buffer,int n){last=shortRead?n-1:n;if(last>0)std::memcpy(buffer,assets.at(asset).data(),last);return last>0;}
 int getLastBytesRead()const{return last;}
};
std::vector<LLSD> delivered;bool requestAllowed=true;
bool fs_assistant_request_allowed(const LLSD&){return requestAllowed;}
void sendReply(const LLSD& result,const LLSD&){if(requestAllowed)delivered.push_back(result);}
auto guard(){return std::make_shared<fs_assistant::AsyncOperation>([](){return std::string();});}
LLUUID resolved("resolved-asset"),other("other-asset");
struct LLResourceUploadInfo {};
'''

TASK_STUBS = r'''
std::string currentSource="source";std::string source(){return currentSource;}double clockValue=10.;double now(){return clockValue;}
struct Prim {bool dirty=false;LLSD evidence=llsd::map("object_id",LLUUID("object"),"generation",1);
 InventoryObject* getInventoryObject(const LLUUID& id){return id==item.uuid?&item:nullptr;}
 bool isInventoryDirty()const{return dirty;}LLUUID getID()const{return LLUUID("object");}
}prim;
Prim* target(const LLUUID&,std::string&){return &prim;}LLSD objectEvidence(const Prim* p){return p->evidence;}
LLSD resultBase(){return {};}
void reply(const LLSD& request,const LLSD& result){sendReply(result,request);}
struct ObjectList {Prim* findObject(const LLUUID&){return &prim;}}gObjectList;
bool runningEvidenceUncertain(Prim*,const LLUUID&){return false;}
const char* runningUncertainError="uncertain";
'''

TASK_CASES = r'''
int main(){
 LLFileSystem::assets[resolved]="default { state_entry() {} }";
 LLSD request=llsd::map("__assistant_epoch","epoch","object_id",prim.getID(),"item_id",item.uuid);
 auto read=[&](LLUUID asset=resolved,int status=0,fs_assistant::AsyncGuard authority=guard()){
  delivered.clear();auto* pending=new AssetRead{request,objectEvidence(&prim),itemEvidence(&item),source(),std::make_shared<fs_assistant::AsyncTask>(authority),service().privacyEpoch};
  scriptLoaded(asset,LLAssetType::AT_LSL_TEXT,pending,status,0);
 };
 assert(item.asset.isNull()&&scriptReadable(&item));
 auto row=itemRow(&item);assert(row["native_source_readable"].asBoolean()&&!row["asset_id_known"].asBoolean());
 read();assert(delivered.size()==1&&!delivered.back().has("error"));
 auto result=delivered.back();assert(result["asset_id"].asUUID()==resolved&&item.asset.isNull());
 assert(result["text"].asString()==LLFileSystem::assets[resolved]);
 request["source_snapshot"]=result["source_snapshot"];request["expected_asset_id"]=resolved;request["content_hash"]=result["content_hash"];
 std::string error;auto* snapshot=scriptSnapshot(request,error);assert(snapshot&&snapshot->asset==resolved&&snapshot->item["asset_id"].asUUID().isNull());
 request["expected_asset_id"]=other;assert(!scriptSnapshot(request,error));request["expected_asset_id"]=resolved;
 request["content_hash"]=std::string(64,'x');assert(!scriptSnapshot(request,error));request["content_hash"]=result["content_hash"];
 // Running replies retain native metadata evidence, which can still be null.
 request["running_mode"]="preserve";request["running_snapshot"]=LLUUID("running-token");
 auto& observed=service().running[{prim.getID(),item.uuid}];observed=Running{source(),"epoch",LLUUID("running-token"),LLUUID(),true,true,now()};
 bool running=false;assert(requestedRunning(request,running,error)&&running);
 observed.asset=other;assert(!requestedRunning(request,running,error));observed.asset=LLUUID();
 observed.at-=11.;assert(!requestedRunning(request,running,error));observed.at=now();
 auto approved=llsd::map("object_id",prim.getID(),"item_id",item.uuid,"asset_id",item.asset,"item_evidence",itemEvidence(&item));
 assert(testSourceSnapshot(approved,"epoch"));
 assert(!testSourceSnapshot(approved,"another-epoch"));
 prim.evidence["generation"]=2;assert(!testSourceSnapshot(approved,"epoch"));prim.evidence["generation"]=1;
 clockValue+=121.;assert(!testSourceSnapshot(approved,"epoch"));clockValue-=121.;
 auto changedApproval=approved;changedApproval["item_evidence"]["copy"]=false;assert(!testSourceSnapshot(changedApproval,"epoch"));
 prim.dirty=true;assert(!scriptSnapshot(request,error));prim.dirty=false;
 prim.evidence["generation"]=2;assert(!scriptSnapshot(request,error));prim.evidence["generation"]=1;
 item.asset=other;assert(!scriptSnapshot(request,error));read();assert(delivered.back().has("error"));item.asset=LLUUID();
 item.permissions.modify=false;assert(!scriptReadable(&item));assert(itemRow(&item)["source_read_blocker"].asString()=="native_modify_required");item.permissions.modify=true;
 item.permissions.copy=false;assert(!scriptReadable(&item));assert(std::string(scriptReadBlocker(&item))=="native_copy_required");item.permissions.copy=true;
 item.permissions.owner=LLUUID("another-owner");assert(!scriptReadable(&item));assert(std::string(scriptReadBlocker(&item))=="script_owner_required");item.permissions.owner=gAgent.getID();
 gRlvHandler.script=true;assert(!scriptReadable(&item));assert(std::string(scriptReadBlocker(&item))=="rlva_viewscript_restricted");gRlvHandler.script=false;
 item.link=true;assert(!scriptReadable(&item));item.link=false;
 request["__assistant_epoch"]="new-epoch";assert(!scriptSnapshot(request,error));request["__assistant_epoch"]="epoch";
 currentSource="new-source";assert(!scriptSnapshot(request,error));currentSource="source";
 clockValue+=121.;assert(!scriptSnapshot(request,error));clockValue-=121.;
 for(int changed=0;changed<7;++changed){
  auto authority=guard();auto* pending=new AssetRead{request,objectEvidence(&prim),itemEvidence(&item),source(),std::make_shared<fs_assistant::AsyncTask>(authority),service().privacyEpoch};
  if(changed==0)item.permissions.copy=false;if(changed==1)item.asset=other;if(changed==2)currentSource="changed";
  if(changed==3)gRlvHandler.script=true;if(changed==4)authority->cancel("revoked");if(changed==5)++service().privacyEpoch;if(changed==6)prim.evidence["generation"]=2;
  delivered.clear();const auto count=service().scripts.size();scriptLoaded(resolved,LLAssetType::AT_LSL_TEXT,pending,0,0);
  assert(service().scripts.size()==count&&(delivered.empty()||delivered.back().has("error")));
  item.permissions.copy=true;item.asset=LLUUID();currentSource="source";gRlvHandler.script=false;prim.evidence["generation"]=1;
 }
 for(int failed=0;failed<6;++failed){
  const auto count=service().scripts.size();LLFileSystem::shortRead=failed==2;
  if(failed==3)LLFileSystem::assets[resolved]=std::string("bad\0source",10);
  if(failed==4)LLFileSystem::assets[resolved]="";if(failed==5)LLFileSystem::assets[resolved]=std::string(fs_content::assetLimit+1,'x');
  read(failed==0?LLUUID():resolved,failed==1?1:0);assert(delivered.back().has("error")&&service().scripts.size()==count);
 }
 LLFileSystem::shortRead=false;LLFileSystem::assets[resolved]="source";
 item.asset=resolved;read();assert(!delivered.back().has("error")); // known ID still works
}
'''

INVENTORY_STUBS = r'''
void resetSession(){}
bool editorBusy(const LLUUID&,bool){return false;}
bool target(const LLSD&,bool& notecard,LLViewerInventoryItem*& p,std::string& error){p=&item;notecard=item.type==LLAssetType::AT_NOTECARD;
 if(!fs_content::readable(p,notecard)){error="not readable";return false;}return true;}
// Notecard parsing is not under test; the production script path bypasses it.
struct LLNotecard {bool importStream(std::stringstream&){return false;}std::string getText()const{return {};}
 int getVersion()const{return 2;}std::vector<int> getItems()const{return {};}};
'''

INVENTORY_CASES = r'''
int main(){
 LLFileSystem::assets[resolved]="default { state_entry() {} }";
 LLSD request=llsd::map("__assistant_epoch","epoch","id",item.uuid,"compiler","mono","op","saveScript","text","replacement");
 auto read=[&](LLUUID asset=resolved,int status=0,fs_assistant::AsyncGuard authority=guard()){
  delivered.clear();auto* pending=new Read{request,evidence(&item),false,std::make_shared<fs_assistant::AsyncTask>(authority)};
  loaded(asset,LLAssetType::AT_LSL_TEXT,pending,status,0);
 };
 assert(item.asset.isNull()&&readable(&item,false)&&writable(&item,false));
 read();assert(delivered.size()==1&&!delivered.back().has("error"));auto result=delivered.back();
 assert(result["asset_id"].asUUID()==resolved&&result["editable"].asBoolean()&&item.asset.isNull());
 assert(result["text"].asString()==LLFileSystem::assets[resolved]);
 request["expected_asset_id"]=resolved;request["content_hash"]=result["content_hash"];
 Snapshot* snapshot=nullptr;LLViewerInventoryItem* p=nullptr;std::string error;
 assert(baseSnapshot(request,snapshot,p,error,true)&&snapshot->asset==resolved&&snapshot->authority["asset_id"].asUUID().isNull());
 request["expected_asset_id"]=other;assert(!baseSnapshot(request,snapshot,p,error,true));request["expected_asset_id"]=resolved;
 request["content_hash"]=std::string(64,'x');assert(!baseSnapshot(request,snapshot,p,error,true));request["content_hash"]=result["content_hash"];
 // Receipt conflict checks use native metadata until a save is confirmed;
 // the resolved previous source ID must not be compared to an unknown cache ID.
 auto write=std::make_shared<Write>();write->authority=evidence(&item);write->request=request;write->notecard=false;
 write->receipt=llsd::map("previous_asset_id",resolved,"asset_id",resolved,"source_saved",false);
 const auto allowed=[](){return true;};assert(write->progress.before("capability",true,allowed,allowed));assert(write->progress.before("source",true,allowed,allowed));
 sWrites[LLUUID("operation")]=write;auto status=request;status["operation_id"]=LLUUID("operation");
 assert(!receiptFor(status,error)["post_submit_conflict"].asBoolean());
 item.asset=other;assert(receiptFor(status,error)["post_submit_conflict"].asBoolean());
 write->receipt["source_saved"]=true;write->receipt["asset_id"]=other;assert(!receiptFor(status,error)["post_submit_conflict"].asBoolean());
 item.asset=LLUUID();assert(receiptFor(status,error)["post_submit_conflict"].asBoolean());write->receipt["source_saved"]=false;
 item.asset=other;assert(!baseSnapshot(request,snapshot,p,error,true));read();assert(delivered.back().has("error"));item.asset=LLUUID();
 for(int changed=0;changed<6;++changed){
  auto authority=guard();auto* pending=new Read{request,evidence(&item),false,std::make_shared<fs_assistant::AsyncTask>(authority)};
  if(changed==0)item.permissions.modify=false;if(changed==1)item.asset=other;if(changed==2)gAgent.login=LLUUID("another-login");
  if(changed==3)gRlvHandler.script=true;if(changed==4)authority->cancel("revoked");if(changed==5)requestAllowed=false;
  delivered.clear();sSnapshots.clear();loaded(resolved,LLAssetType::AT_LSL_TEXT,pending,0,0);
  assert(sSnapshots.empty()&&(delivered.empty()||delivered.back().has("error")));
  item.permissions.modify=true;item.asset=LLUUID();gAgent.login=LLUUID("login");gRlvHandler.script=false;requestAllowed=true;
 }
 for(int failed=0;failed<6;++failed){
  sSnapshots.clear();LLFileSystem::shortRead=failed==2;
  if(failed==3)LLFileSystem::assets[resolved]=std::string("bad\0source",10);
  if(failed==4)LLFileSystem::assets[resolved]="";if(failed==5)LLFileSystem::assets[resolved]=std::string(assetLimit+1,'x');
  read(failed==0?LLUUID():resolved,failed==1?1:0);assert(delivered.back().has("error")&&sSnapshots.empty());
 }
 LLFileSystem::shortRead=false;LLFileSystem::assets[resolved]="source";item.asset=resolved;read();assert(!delivered.back().has("error"));
}
'''

LEGACY_CASES = r'''
int main(){
 LLFileSystem::assets[resolved]="default { state_entry() {} }";
 LLSD request=llsd::map("__assistant_epoch","epoch","id",item.uuid);
 auto read=[&](LLUUID asset=resolved,int status=0,fs_assistant::AsyncGuard authority=guard()){
  delivered.clear();auto* pending=new AssetRead{request,item.uuid,item.asset,false,fs_content::evidence(&item),std::make_shared<fs_assistant::AsyncTask>(authority)};
  on_asset_read(asset,LLAssetType::AT_LSL_TEXT,pending,status,0);
 };
 assert(item.asset.isNull()&&fs_content::readable(&item,false));read();
 assert(delivered.size()==1&&!delivered.back().has("error")&&delivered.back()["complete"].asBoolean());
 assert(delivered.back()["asset_id"].asUUID()==resolved&&delivered.back()["text"].asString()==LLFileSystem::assets[resolved]&&item.asset.isNull());
 item.asset=other;read();assert(delivered.back().has("error"));item.asset=LLUUID();
 read(LLUUID());assert(delivered.back().has("error"));read(resolved,1);assert(delivered.back().has("error"));
 auto revoked=guard();revoked->cancel("revoked");read(resolved,0,revoked);assert(delivered.back().has("error"));
 auto* pending=new AssetRead{request,item.uuid,item.asset,false,fs_content::evidence(&item),std::make_shared<fs_assistant::AsyncTask>(guard())};
 item.permissions.copy=false;delivered.clear();on_asset_read(resolved,LLAssetType::AT_LSL_TEXT,pending,0,0);
 assert(delivered.back().has("error"));item.permissions.copy=true;
 pending=new AssetRead{request,item.uuid,item.asset,false,fs_content::evidence(&item),std::make_shared<fs_assistant::AsyncTask>(guard())};
 item.asset=other;delivered.clear();on_asset_read(resolved,LLAssetType::AT_LSL_TEXT,pending,0,0);assert(delivered.back().has("error"));
 item.asset=resolved;read();assert(!delivered.back().has("error"));
}
'''


class NativeContentSourceTests(unittest.TestCase):
    def run_native(self, source):
        with tempfile.TemporaryDirectory(prefix='fs-content-source-') as directory:
            cpp = Path(directory) / 'test.cpp'
            cpp.write_text(source)
            exe = cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                '-UNDEBUG', '-I' + str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_task_unknown_asset_read_and_edit_snapshot(self):
        text = (ROOT / 'fsobjecttools.cpp').read_text()
        helpers = ['const char* scriptReadBlocker(const LLViewerInventoryItem* item)',
                   'bool scriptReadable(const LLViewerInventoryItem* item)',
                   'LLSD itemEvidence(const LLViewerInventoryItem* item)',
                   'LLSD itemRow(const LLViewerInventoryItem* item)']
        source = STUBS + TASK_STUBS + ''.join(function(text, name) for name in helpers)
        source += function(text, 'struct SourceSnapshot') + ';\n'
        source += function(text, 'struct Running') + ';\n'
        source += 'struct Service {U64 privacyEpoch=0;std::map<LLUUID,SourceSnapshot> scripts;std::map<std::pair<LLUUID,LLUUID>,Running> running;};\nService& service(){static Service v;return v;}\n'
        source += function(text, 'struct AssetRead') + ';\n'
        source += function(text, 'void scriptLoaded(const LLUUID& asset,LLAssetType::EType,void* userdata,S32 status,LLExtStat)')
        source += function(text, 'SourceSnapshot* scriptSnapshot(const LLSD& request,std::string& error)')
        source += function(text, 'bool requestedRunning(const LLSD& request,bool& running,std::string& error)')
        source += function(text, 'const SourceSnapshot* testSourceSnapshot(const LLSD& script,const std::string& epoch)')
        self.run_native(source + TASK_CASES)

    def test_inventory_unknown_asset_read_and_first_save_snapshot(self):
        text = (ROOT / 'fscontentlistener.cpp').read_text()
        permissions = (ROOT / 'fscontentpermissions.h').read_text()
        source = STUBS + 'namespace fs_content {\n'
        source += function(permissions, 'inline bool readable(const LLViewerInventoryItem* item,bool notecard)')
        source += function(permissions, 'inline bool writable(const LLViewerInventoryItem* item,bool notecard)') + '}\n'
        source += 'using namespace fs_content;\n' + INVENTORY_STUBS
        source += function(text, 'struct Snapshot') + ';\nstd::map<LLUUID,Snapshot> sSnapshots;\n'
        source += function(text, 'struct Write') + ';\nstd::map<LLUUID,std::shared_ptr<Write>> sWrites;\n'
        source += function(text, 'struct Read') + ';\n'
        source += function(text, 'bool input(const LLSD& request,std::string& error)')
        source += function(text, 'bool baseSnapshot(const LLSD& request,Snapshot*& snapshot,LLViewerInventoryItem*& item,std::string& error,bool write)')
        source += function(text, 'void loaded(const LLUUID& asset,LLAssetType::EType,void* userdata,S32 status,LLExtStat)')
        source += function(text, 'LLSD receiptFor(const LLSD& request,std::string& error)')
        self.run_native(source + INVENTORY_CASES)

    def test_legacy_inventory_reader_uses_resolved_asset(self):
        text = (ROOT / 'llinventorylistener.cpp').read_text()
        permissions = (ROOT / 'fscontentpermissions.h').read_text()
        source = STUBS + 'namespace fs_content {\n'
        source += function(permissions, 'inline bool readable(const LLViewerInventoryItem* item,bool notecard)')
        source += function(permissions, 'inline bool writable(const LLViewerInventoryItem* item,bool notecard)') + '}\n'
        source += INVENTORY_STUBS + 'const std::size_t kTextCap=65536;\n'
        source += function(text, 'std::string capped_text(const char* data, S32 length, bool& truncated)')
        source += function(text, 'struct AssetRead') + ';\n'
        source += function(text, 'void finish_asset_read(AssetRead* raw, const LLUUID& asset, S32 status)')
        source += function(text, 'void on_asset_read(const LLUUID& asset, LLAssetType::EType, void* user_data, S32 status, LLExtStat)')
        self.run_native(source + LEGACY_CASES)


if __name__ == '__main__':
    unittest.main()
