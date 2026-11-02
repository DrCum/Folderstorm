"""Compile building preparation/validation and bridge guard registration.

Real tickets and async authority; LLSD, native scene/admission and transport are
fixtures. No native world write occurs. These checks do not replace SL acceptance.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest

from test_fscontentsource_native import STUBS
from test_fsobjecttools_native import function

ROOT = Path(__file__).resolve().parents[1]
# Keep the fixture item separate from the native building item() helper.
BASE = re.sub(r'\bitem\b', 'inventoryItem', STUBS)
FIXTURES = r'''
#include "fsassistantworldmodel.h"
using fs_assistant_world::Tickets;
std::string currentSource="account:login:grid:region:bridge";
double clockValue=10.;
std::string source(){return currentSource;}double now(){return clockValue;}
LLSD nativeState=llsd::map("agent_position","fixed","rez_group","group");
bool nativeAllowed=true,argumentsAllowed=true;
bool arguments(const LLSD&,std::string& error){if(!argumentsAllowed)error="invalid_args";return argumentsAllowed;}
bool native(const LLSD&,LLSD& state,std::string& error){state=nativeState;if(!nativeAllowed)error="native_permission_or_target_changed";return nativeAllowed;}
bool baseline(const LLSD&,const LLSD&,std::string&){return true;}
// Summary serialization is a fixture; material comparison uses structural LLSD.
std::string ll_stream_notation_sd(const LLSD&){return "summary";}
LLViewerInventoryItem* item(const LLSD& id){return gInventory.getItem(id.asUUID());}
LLSD itemState(LLViewerInventoryItem* p){LLSD value;p->asLLSD(value);return value;}
struct FSAssistantPreparedOperation{LLSD request,validation,summary;std::vector<std::string> requiredClasses;};
struct FSBuildListener {
 bool prepareAssistant(FSAssistantPreparedOperation&,std::string&)const;
 bool validateAssistant(const FSAssistantPreparedOperation&,std::string&)const;
};
'''
GUARD_FIXTURES = r'''
struct State {
 FSBuildListener listener;
 bool validateOperation(const FSAssistantPreparedOperation& p,std::string& error){return listener.validateAssistant(p,error);}
};
struct Posted{fs_assistant::AsyncGuard async;};
'''
CASES = r'''
int main(){
 auto host=std::make_shared<State>();
 auto cube=[](){LLSD position=LLSD::emptyArray(),scale=LLSD::emptyArray(),rotation=LLSD::emptyArray();
  for(int n:{20,25,30})position.append(n);for(int n:{1,1,1})scale.append(n);for(int n:{0,0,0,1})rotation.append(n);
  return llsd::map("api","FSBuild","op","rezPrimitive","shape","cube","position",position,"scale",scale,"rotation",rotation);};
 int submitted=0;
 for(const auto* shape:{"cube","sphere","cylinder","cone"})for(bool tracked:{false,true}){
  FSAssistantPreparedOperation p;p.request=cube();p.request["shape"]=shape;
  if(tracked)p.request["client_request_id"]=LLUUID("11111111-1111-4111-8111-111111111111");
  const auto before=material(p.request);std::string error;
  assert(host->listener.prepareAssistant(p,error));
  // Primitive rez has no existing object id; rendering summary must not add one.
  assert(!p.request.has("id")&&llsd_equals(before,material(p.request)));
  assert(host->listener.validateAssistant(p,error));
  Posted posted{guard()};attach(posted,&p,"FSBuild",LLUUID("execution"),host);
  assert(posted.async->submit());++submitted;
 }
 assert(submitted==8);
 // Inventory rez has an id and keeps the same material through preparation too.
 FSAssistantPreparedOperation inventory;inventory.request=cube();inventory.request["op"]="rezInventory";inventory.request["id"]=inventoryItem.uuid;
 std::string error;const auto before=material(inventory.request);assert(host->listener.prepareAssistant(inventory,error));
 assert(llsd_equals(before,material(inventory.request))&&host->listener.validateAssistant(inventory,error));
 // Ask/Allow share frozen material, including position/shape/request identity.
 for(const auto* changed:{"shape","position","client_request_id"}){
  FSAssistantPreparedOperation p;p.request=cube();error.clear();assert(host->listener.prepareAssistant(p,error));
  p.request[changed]="changed";assert(!host->listener.validateAssistant(p,error));
 }
 // Genuine native changes and expired/login-scoped tickets still stop submission.
 for(int changed=0;changed<4;++changed){
  FSAssistantPreparedOperation p;p.request=cube();error.clear();assert(host->listener.prepareAssistant(p,error));
  Posted posted{guard()};attach(posted,&p,"FSBuild",LLUUID("execution"),host);
  if(changed==0)nativeState["rez_group"]="other";if(changed==1)nativeAllowed=false;
  if(changed==2)currentSource="different-login";if(changed==3)clockValue+=121.;
  assert(!posted.async->submit()&&posted.async->submitted()==0);
  nativeState["rez_group"]="group";nativeAllowed=true;currentSource="account:login:grid:region:bridge";clockValue=10.;service().approvals.clear();
 }
 FSAssistantPreparedOperation p;p.request=cube();error.clear();assert(host->listener.prepareAssistant(p,error));
 Posted posted{guard()};attach(posted,&p,"FSBuild",LLUUID("execution"),host);posted.async->cancel("permission_revoked");
 assert(!posted.async->submit()&&posted.async->submitted()==0);
}
'''


class BuildingNativeAdapterTests(unittest.TestCase):
    def test_native_preparation_keeps_material_and_live_guard(self):
        text=(ROOT/'fsbuildlistener.cpp').read_text()
        source=BASE+FIXTURES
        for name in ['struct Service','Service& service()',
                     'bool inventoryOp(const std::string& op)',
                     'bool readOp(const std::string& op)',
                     'LLSD material(const LLSD& request)',
                     'std::string ticket(Tickets<LLSD>& store, const LLSD& state)',
                     'bool FSBuildListener::prepareAssistant(FSAssistantPreparedOperation& operation,std::string& error) const',
                     'bool FSBuildListener::validateAssistant(const FSAssistantPreparedOperation& operation,std::string& error) const']:
            source+=function(text,name)+(';' if name.startswith('struct ') else '')+'\n'
        source+=GUARD_FIXTURES
        bridge=(ROOT/'fseventapibridge.cpp').read_text();begin=bridge.index('                auto frozen=*prepared;');end=bridge.index('            }\n            if(fs_assistant::needsActiveCharacter',begin)
        source+='void attach(Posted& posted,const FSAssistantPreparedOperation* prepared,const std::string& api,const LLUUID& request_id,std::weak_ptr<State> weak){\n'+bridge[begin:end]+'}\n'
        with tempfile.TemporaryDirectory(prefix='fs-building-native-') as directory:
            cpp=Path(directory)/'test.cpp';cpp.write_text(source+CASES);exe=cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-UNDEBUG','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__=='__main__':unittest.main()
