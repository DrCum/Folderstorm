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
#include <set>
#include <string>
#include <variant>
#include <vector>
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
namespace llsd {const LLSD::Array& inArray(const LLSD& v){static LLSD::Array empty;return v.isArray()?std::get<LLSD::Array>(v.value):empty;}}
std::string ll_stream_notation_sd(const LLSD& v){std::string s=v.asString();if(v.isArray())for(const auto& x:llsd::inArray(v))s+=ll_stream_notation_sd(x);
 if(v.isMap())for(auto i=v.beginMap();i!=v.endMap();++i)s+=i->first+ll_stream_notation_sd(i->second);return s;}
bool llsd_equals(const LLSD& a,const LLSD& b){return ll_stream_notation_sd(a)==ll_stream_notation_sd(b);}
struct Inventory {virtual ~Inventory()=default;};
struct LLViewerInventoryItem:Inventory {LLUUID asset;LLUUID getAssetUUID()const{return asset;}};
struct LLViewerObject {
 bool pending=false,dirty=false,root=true;LLSD evidence;LLViewerInventoryItem* item=nullptr;
 Inventory* getInventoryObject(const LLUUID&){return item;}
 bool isInventoryPending()const{return pending;}bool isInventoryDirty()const{return dirty;}bool getInventoryRoot()const{return root;}
} prim;
LLViewerObject* target(const LLUUID&,std::string&){return &prim;}
LLSD scopeIdentity(LLViewerObject* p){return p->evidence;}
bool scriptReadable(LLViewerInventoryItem* p){return p!=nullptr;}
enum {CHAT_TYPE_OWNER=1,CHAT_TYPE_DEBUG_MSG=2,RLV_BHVR_RECVCHAT,RLV_BHVR_RECVCHATFROM,RLV_BHVR_RECVEMOTE,RLV_BHVR_RECVEMOTEFROM};
struct rlv_handler_t {static bool isEnabled(){return true;}};
struct RlvHandler {std::set<int> blocked,exceptions;bool hasBehaviour(int b)const{return blocked.count(b);}
 bool isException(int b,const LLUUID&)const{return exceptions.count(b);}}gRlvHandler;
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
        with tempfile.TemporaryDirectory(prefix='fs-object-tools-') as directory:
            cpp = Path(directory) / 'test.cpp'
            cpp.write_text(source)
            exe = cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + [
                '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
                '-UNDEBUG', '-I' + str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

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
        self.assertIn('runningQueries.pending({prim->getID(),item->getUUID()})', text)


if __name__ == '__main__':
    unittest.main()
