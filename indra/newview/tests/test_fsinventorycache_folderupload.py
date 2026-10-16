"""Compile production parser/callback bodies against portable viewer boundaries.

This exercises worker isolation and main-thread folder preparation, not native
inventory transport, filesystem scanning, asset encoding or upload charging.
Run: python3 -m unittest discover -s indra/newview/tests -p test_fsinventorycache_folderupload.py -v
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def read(name, ref=None):
    path = "indra/" + name
    if ref:
        return subprocess.check_output(["git", "show", ref + ":" + path], cwd=ROOT, text=True)
    return (ROOT / path).read_text(encoding="utf-8")


def body(source, signature):
    start = source.index("{", source.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def compile_run(source):
    compiler = shlex.split(os.environ.get("CXX", "c++"))
    if not shutil.which(compiler[0]):
        raise unittest.SkipTest("A C++17 compiler is required for the boundary harness")
    with tempfile.TemporaryDirectory(prefix="folderstorm-upload-") as directory:
        cpp, executable = Path(directory) / "test.cpp", Path(directory) / "test"
        cpp.write_text(source, encoding="utf-8")
        subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                                  str(cpp), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


BOUNDARY = r'''
#include <algorithm>
#include <cassert>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
struct LLUUID {
 std::string value;
 LLUUID()=default; explicit LLUUID(std::string v):value(std::move(v)){}
 bool isNull()const{return value.empty();} bool notNull()const{return !isNull();}
 bool operator==(const LLUUID& r)const{return value==r.value;}
 bool operator<(const LLUUID& r)const{return value<r.value;}
};
struct LLSD {
 using array_const_iterator=std::vector<LLSD>::const_iterator;
 std::string value; std::map<std::string,LLSD> fields; std::vector<LLSD> array; bool is_array=false;
 LLSD()=default; LLSD(const char* v):value(v){} LLSD(std::string v):value(std::move(v)){}
 LLSD(LLUUID v):value(std::move(v.value)){}
 LLSD& operator[](const std::string& key){return fields[key];}
 const LLSD& operator[](const std::string& key)const{
  static const LLSD empty;auto it=fields.find(key);return it==fields.end()?empty:it->second;
 }
 std::string asString()const{return value;} LLUUID asUUID()const{return LLUUID(value);}
 static LLSD emptyArray(){LLSD r;r.is_array=true;return r;}
 void append(const LLSD& r){is_array=true;array.push_back(r);}
 bool isArray()const{return is_array;}
 array_const_iterator beginArray()const{return array.begin();}
 array_const_iterator endArray()const{return array.end();}
};
namespace llsd{const std::vector<LLSD>& inArray(const LLSD& data){return data.array;}}
'''


class InventoryCacheAndFolderUpload(unittest.TestCase):
    def test_worker_uses_only_stored_item_fields(self):
        # The null-ID diagnostic is also exercised with logging enabled: getName
        # on a malformed link must not consult the live inventory either.
        model = read("newview/llinventorymodel.cpp", os.environ.get("FS_CACHE_SOURCE_REF"))
        parser = body(model, "bool LLInventoryModel::loadFromFile(")
        items = body(parser, "if (llsd_items.isArray())")
        viewer = read("newview/llviewerinventory.cpp")
        base = read("llinventory/llinventory.cpp")
        source = BOUNDARY + r'''
namespace LLAssetType{enum EType{AT_UNKNOWN,AT_TEXTURE,AT_LINK,AT_LINK_FOLDER};}
unsigned liveLookups=0;
struct LLInventoryObject{
 std::string mName;LLAssetType::EType mType=LLAssetType::AT_UNKNOWN;
 virtual ~LLInventoryObject()=default;
 virtual const std::string& getName()const;
 virtual LLAssetType::EType getType()const{return mType;}
 LLAssetType::EType getActualType()const;
};
struct LLInventoryItem:LLInventoryObject{};
struct LLViewerInventoryCategory:LLInventoryObject{};
struct LLViewerInventoryItem:LLInventoryItem{
 LLUUID id,parent;
 bool fromLLSD(const LLSD& data){
  id=data["id"].asUUID();parent=data["parent"].asUUID();mName=data["name"].asString();
  mType=static_cast<LLAssetType::EType>(std::stoi(data["type"].asString()));return true;
 }
 const LLUUID& getUUID()const{return id;}const LLUUID& getParentUUID()const{return parent;}
 const LLViewerInventoryItem* getLinkedItem()const{
  if(mType==LLAssetType::AT_LINK)++liveLookups;
  return nullptr;
 }
 const LLViewerInventoryCategory* getLinkedCategory()const{
  if(mType==LLAssetType::AT_LINK_FOLDER)++liveLookups;
  return nullptr;
 }
 const std::string& getName()const override;LLAssetType::EType getType()const override;
};
template<class T>struct LLPointer:std::shared_ptr<T>{LLPointer(T* p):std::shared_ptr<T>(p){}};
#define LL_DEBUGS(tag) std::clog
#define LL_ENDL '\n'
'''
        for signature, text in (
            ("const std::string& LLInventoryObject::getName() const", base),
            ("LLAssetType::EType LLInventoryObject::getActualType() const", base),
            ("LLAssetType::EType LLViewerInventoryItem::getType() const", viewer),
            ("const std::string& LLViewerInventoryItem::getName() const", viewer),
        ):
            source += signature + body(text, signature) + "\n"
        source += r'''
void parse(const LLSD& inventory,std::vector<LLPointer<LLViewerInventoryItem>>& items,
           std::set<LLUUID>& cats_to_update){
 const LLSD& llsd_items=inventory["items"];
 if(llsd_items.isArray()) @ITEMS@
}
int main(){
 LLSD inventory;
 for(int type:{LLAssetType::AT_TEXTURE,LLAssetType::AT_LINK,LLAssetType::AT_LINK_FOLDER,LLAssetType::AT_UNKNOWN}){
  LLSD item;item["id"]=std::to_string(type+1);item["parent"]="parent";
  item["type"]=std::to_string(type);item["name"]="stored name";inventory["items"].append(item);
 }
 for(int type:{LLAssetType::AT_LINK,LLAssetType::AT_LINK_FOLDER}){
  LLSD item;item["type"]=std::to_string(type);item["name"]="malformed link";
  inventory["items"].append(item);
 }
 std::vector<LLPointer<LLViewerInventoryItem>> items;std::set<LLUUID> dirty;
 parse(inventory,items,dirty);
 assert(liveLookups==0 && items.size()==3 && dirty.size()==1);
 assert(items[1]->getActualType()==LLAssetType::AT_LINK);
 assert(items[2]->getActualType()==LLAssetType::AT_LINK_FOLDER);
 // Resolution still works when the main-thread merge chooses to request it.
 items[1]->getType();items[2]->getName();assert(liveLookups==2);
}
'''.replace("@ITEMS@", items)
        compile_run(source)

    def test_folder_callbacks_prepare_all_destinations_before_upload(self):
        menu = read("newview/llviewermenufile.cpp")
        source = BOUNDARY + r'''
struct Agent{
 LLUUID id{"agent"},session{"login"};LLUUID getID()const{return id;}LLUUID getSessionID()const{return session;}
}gAgent;
bool gDisconnected=false,exiting=false;
struct LLApp{static bool isExiting(){return exiting;}};
std::vector<std::string> errors;
namespace LLNotificationsUtil{void add(const char*,const LLSD& args){errors.push_back(args["MESSAGE"].asString());}}
std::deque<std::function<void()>> idle;
void doOnIdleOneTime(std::function<void()> fn){idle.push_back(std::move(fn));}
namespace LLFolderType{enum EType{FT_NONE};}
struct Inventory{
 struct Request{LLUUID parent;std::string name;std::function<void(const LLUUID&)> callback;};
 std::vector<Request> requests;bool destination=true,synchronous=false;
 int callbackDepth=0,maxDepth=0;
 int* getCategory(const LLUUID&){return destination?&callbackDepth:nullptr;}
 LLUUID getRootFolderID()const{return LLUUID("inventory-root");}
 void createNewCategory(const LLUUID& parent,LLFolderType::EType,const std::string& name,
                        std::function<void(const LLUUID&)> callback){
  requests.push_back({parent,name,callback});
  if(synchronous){++callbackDepth;maxDepth=std::max(maxDepth,callbackDepth);
   callback(LLUUID("created-"+name));--callbackDepth;}
 }
}gInventory;
struct Directory{
 std::string getExtension(const std::string& file)const{return file.substr(file.find_last_of('.')+1);}
}directory;
Directory* gDirUtilp=&directory;
struct Upload{std::vector<std::string> files;bool allow2k;LLUUID dest,material,texture;};
std::vector<Upload> uploads;bool changeSessionOnUpload=false;
void do_bulk_upload(std::vector<std::string> files,bool allow,const LLUUID& dest,
                    const LLUUID& material={},const LLUUID& texture={}){
 uploads.push_back({std::move(files),allow,dest,material,texture});
 if(changeSessionOnUpload)gAgent.session=LLUUID("next-login");
}
bool validationFails=false,queueAvailable=true,postAccepted=true;
void validate_folder_upload(const LLSD&){if(validationFails)throw std::runtime_error("File changed");}
namespace LL{
struct WorkQueue{
 static std::shared_ptr<WorkQueue> getInstance(const char*){
  return queueAvailable?std::make_shared<WorkQueue>():nullptr;
 }
 template<class Work,class Callback>bool postTo(const std::shared_ptr<WorkQueue>&,Work work,Callback callback){
  if(!postAccepted)return false;
  callback(work());return true;
 }
};
}
'''
        for signature in ("bool folder_upload_session_valid(const LLSD& plan)",
                          "void folder_upload_error(const std::string& message)"):
            source += signature + body(menu, signature) + "\n"
        source += "struct FolderUploadOperation : std::enable_shared_from_this<FolderUploadOperation> "
        source += body(menu, "struct FolderUploadOperation :") + ";\n"
        signature = "void start_folder_recursive_upload(const LLSD& plan, const LLUUID& dest, bool allow_2k)"
        source += signature + body(menu, signature)
        source += r'''
void tick(){assert(!idle.empty());auto work=std::move(idle.front());idle.pop_front();work();}
void flush(){while(!idle.empty())tick();}
LLSD node(const std::string& name,std::initializer_list<const char*> files){
 LLSD result;result["name"]=name;result["folders"]=LLSD::emptyArray();result["files"]=LLSD::emptyArray();
 for(const auto* path:files){LLSD file;file["path"]=path;result["files"].append(file);}return result;
}
LLSD plan(){
 LLSD result;result["agent_id"]=gAgent.id;result["session_id"]=gAgent.session;
 result["tree"]=node("Root",{"image.png","model.gltf"});
 result["tree"]["folders"].append(node("Child",{"sound.wav"}));return result;
}
void reset(){
 idle.clear();gInventory.requests.clear();uploads.clear();errors.clear();gInventory=Inventory{};gAgent=Agent{};
 gDisconnected=exiting=validationFails=changeSessionOnUpload=false;queueAvailable=postAccepted=true;
}
void complete(size_t index,const LLUUID& id){auto callback=gInventory.requests.at(index).callback;callback(id);}
void start(){start_folder_recursive_upload(plan(),LLUUID("destination"),false);}
void finishAll(){
 size_t handled=0;
 while(handled<gInventory.requests.size()){
  // Only one creation is in flight, and assets stay staged until every folder exists.
  assert(gInventory.requests.size()==handled+1 && uploads.empty());
  const auto& request=gInventory.requests[handled];
  complete(handled++,LLUUID("created-"+request.name));flush();
 }
}
int main(){
 // A null ID is a failure for each of the four category callback paths.
 for(const auto* failed: {"Root","Child","Materials","Textures"}){
  reset();start();size_t handled=0;
  while(errors.empty()){
   assert(handled<gInventory.requests.size() && uploads.empty());
   const auto name=gInventory.requests[handled].name;
   complete(handled++,name==failed?LLUUID{}:LLUUID("created-"+name));flush();
  }
  assert(errors.size()==1 && uploads.empty());
  const auto expected=std::string(failed)=="Root"?"Root":"Root/"+std::string(failed);
  assert(errors[0].find(expected)!=std::string::npos);
  assert(errors[0].find(std::to_string(handled-1)+" folder(s)")!=std::string::npos);
  assert(errors[0].find("No files were uploaded")!=std::string::npos);
  assert(gInventory.requests.size()==handled); // no siblings or whole-import retry after failure
  complete(handled-1,LLUUID("late-duplicate"));flush();assert(uploads.empty() && errors.size()==1);
 }
 for(bool synchronous:{false,true}){
  reset();gInventory.synchronous=synchronous;start();
  if(synchronous)flush();else finishAll();
  assert(gInventory.requests.size()==4 && uploads.size()==3 && errors.empty());
  assert(gInventory.maxDepth<=1);
  for(const auto& request:gInventory.requests)
   assert(request.parent==LLUUID(request.name=="Root"?"destination":"created-Root"));
  for(const auto& upload:uploads){
   assert(!upload.allow2k && upload.files.size()==1);
   if(upload.files[0]=="model.gltf"){
    assert(upload.dest==LLUUID("created-Materials") && upload.material==upload.dest);
    assert(upload.texture==LLUUID("created-Textures"));
   }else{
    assert(upload.dest==LLUUID(upload.files[0]=="image.png"?"created-Root":"created-Child"));
    assert(upload.material.isNull() && upload.texture.isNull());
   }
  }
  for(size_t i=0;i<gInventory.requests.size();++i){complete(i,LLUUID{});complete(i,LLUUID("duplicate"));}
  flush();assert(uploads.size()==3 && errors.empty());
 }
 // Cancellation/stale account/login is silent even when transport returns a null ID.
 for(int change=0;change<4;++change)for(bool failed:{false,true}){
  reset();start();
  if(change==0)gAgent.id=LLUUID("other-account");
  if(change==1)gAgent.session=LLUUID("next-login");
  if(change==2)gDisconnected=true;
  if(change==3)exiting=true;
  complete(0,failed?LLUUID{}:LLUUID("late-root"));flush();
  assert(uploads.empty() && errors.empty() && gInventory.requests.size()==1);
 }
 reset();start();complete(0,LLUUID("created-Root"));gAgent.session=LLUUID("next-login");flush();
 assert(gInventory.requests.size()==1 && uploads.empty() && errors.empty());
 // Cancellation after preparation but before submission must still suppress every batch.
 reset();start();
 for(size_t i=0;i<4;++i){complete(i,LLUUID("created-"+gInventory.requests[i].name));if(i<3)flush();}
 gAgent.session=LLUUID("next-login");flush();assert(uploads.empty() && errors.empty());
 reset();changeSessionOnUpload=true;start();finishAll();assert(uploads.size()==1 && errors.empty());
 reset();gInventory.destination=false;start();assert(errors.size()==1 && gInventory.requests.empty());
 reset();validationFails=true;start();assert(errors.size()==1 && gInventory.requests.empty());
 reset();queueAvailable=false;start();assert(errors.size()==1 && gInventory.requests.empty());
 reset();postAccepted=false;start();assert(errors.size()==1 && gInventory.requests.empty());
 reset();gDisconnected=true;start();assert(errors.empty() && gInventory.requests.empty());
 // Many synchronous category callbacks still yield between requests, with no recursive growth.
 reset();gInventory.synchronous=true;auto large=plan();large["tree"]=node("Root",{});
 for(int i=0;i<1000;++i)large["tree"]["folders"].append(node("child-"+std::to_string(i),{}));
 start_folder_recursive_upload(large,{},true);flush();
 assert(gInventory.requests.size()==1001 && gInventory.maxDepth==1 && uploads.empty() && errors.empty());
}
'''
        compile_run(source)

    def test_folder_scan_prices_only_through_existing_bulk_helper(self):
        menu = read("newview/llviewermenufile.cpp")
        scan = body(menu, "void start_bulk_folder_upload(const LLUUID& dest)")
        self.assertNotIn("get_bulk_upload_expected_cost(", scan)
        self.assertEqual(scan.count("upload_bulk(files, true, dest, result)"), 1)
        bulk = body(menu, "void upload_bulk(")
        self.assertEqual(bulk.count("get_bulk_upload_expected_cost("), 1)
        self.assertIn("BulkUploadNoCompatibleFiles", bulk)


if __name__ == "__main__":
    unittest.main()
