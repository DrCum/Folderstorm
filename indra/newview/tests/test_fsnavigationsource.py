"""Native ownership/transport contracts plus actual request-guard reproduction."""
import pathlib
import re
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class NavigationNativeTests(unittest.TestCase):
    def test_status_polling_keeps_stop_available_with_actual_preparation(self):
        text=(ROOT/'fsnavigationlistener.cpp').read_text()
        prepare=text[text.index('bool FSNavigationListener::prepareAssistant('):text.index('bool FSNavigationListener::validateAssistant(')]
        # Compile the production preparation function against its real ticket
        # cache. Only viewer/UI dependencies and LLSD storage are stand-ins.
        # Include the production stateless-read helpers when present.
        helpers=""
        if 'bool prepareSourceBoundStatusRead(' in text:
            helpers=text[text.index('bool prepareSourceBoundStatusRead('):text.index('LLSD baseline()')]
        program=r'''#include "fsassistantworldmodel.h"
#include <cassert>
#include <map>
#include <string>
#include <utility>
#include <vector>
struct LLSD {
    std::string value;std::map<std::string,LLSD> fields;
    LLSD()=default;LLSD(const std::string& v):value(v){}LLSD(const char* v):value(v){}LLSD(int v):value(std::to_string(v)){}
    const LLSD& operator[](const std::string& key)const{static const LLSD none;auto found=fields.find(key);return found==fields.end()?none:found->second;}
    LLSD& operator[](const std::string& key){return fields[key];}
    std::string asString()const{return value;}
    bool operator==(const LLSD& other)const{return value==other.value && fields==other.fields;}
};
bool llsd_equals(const LLSD& a,const LLSD& b){return a==b;}
namespace llsd {
inline void append(LLSD&){}
template<class Value,class... Rest>void append(LLSD& out,const char* key,Value value,Rest... rest){out[key]=LLSD(value);append(out,rest...);}
template<class... Args>LLSD map(Args... args){LLSD out;append(out,args...);return out;}
}
struct FSAssistantPreparedOperation{LLSD request,validation,summary;std::vector<std::string> requiredClasses;};
struct FakeID{std::string asString()const{return "account-a";}};
struct FakeAgent{FakeID getID()const{return {};}}gAgent;
struct LLUUID{std::string value;static LLUUID generateNewID(){static int id=0;return {std::to_string(++id)};}std::string asString()const{return value;}};
struct Service{fs_assistant_world::Tickets<LLSD> prepared;std::string preparedSource;};
Service& service(){static Service cache;return cache;}
std::string login="account-a/login-a/grid-a/worker-a";double clockValue=1.;bool nativeAllowed=true,loginReady=true;
std::string source(){return login;}double now(){return clockValue;}bool ready(){return loginReady;}
void fs_navigation_tick(){}
bool arguments(const LLSD&,std::string&){return true;}
bool native(const LLSD&,std::string& error){if(!nativeAllowed){error="restricted";return false;}return true;}
LLSD material(const LLSD& request){return request;}
LLSD baseline(){return LLSD("live-position");}LLSD approvalBaseline(const LLSD&){return baseline();}
std::string ll_stream_notation_sd(const LLSD& v){return v.value;}
class FSNavigationListener{public:bool prepareAssistant(FSAssistantPreparedOperation&,std::string&)const;};
HELPERS
PREPARE
int main(){
    FSNavigationListener listener;std::string error;
    auto prepare=[&](const char* op){FSAssistantPreparedOperation p;p.request["op"]=op;assert(listener.prepareAssistant(p,error));return p;};
    for(int i=0;i<63;++i)prepare("status");
    auto movement=prepare("moveTo");
    const auto key=movement.validation["navigation_prepared"].asString();
    auto* active=service().prepared.get(key,source(),now());assert(active);active->executed=true;
    clockValue=2.;auto stop=prepare("stop");
    assert(service().prepared.get(stop.validation["navigation_prepared"].asString(),source(),now()));
    for(int i=0;i<1024;++i)prepare("status");
    assert(service().prepared.get(key,source(),now())==active && active->executed);
    EXTRA_CHECKS
}
'''
        checks=""
        if helpers:
            checks=r'''auto read=prepare("status");
    assert(validateSourceBoundStatusRead(read,error));
    for(const char* changed:{"account-b/login-a/grid-a/worker-a","account-a/login-b/grid-a/worker-a","account-a/login-a/grid-b/worker-a","account-a/login-a/grid-a/worker-b"}){
        login=changed;assert(!validateSourceBoundStatusRead(read,error));
    }
    login="account-a/login-a/grid-a/worker-a";nativeAllowed=false;assert(!validateSourceBoundStatusRead(read,error));
    nativeAllowed=true;loginReady=false;assert(!validateSourceBoundStatusRead(read,error));loginReady=true;
    read.request["operation_id"]="other";assert(!validateSourceBoundStatusRead(read,error));'''
        program=program.replace('HELPERS',helpers).replace('PREPARE',prepare).replace('EXTRA_CHECKS',checks)
        with tempfile.TemporaryDirectory() as temp:
            cpp=pathlib.Path(temp)/'polling.cpp';cpp.write_text(program);exe=pathlib.Path(temp)/'polling'
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_status_source_validation_and_guarded_dispatch_bypass_only_tickets(self):
        text=(ROOT/'fsnavigationlistener.cpp').read_text()
        prepare=text[text.index('bool FSNavigationListener::prepareAssistant('):text.index('bool FSNavigationListener::validateAssistant(')]
        self.assertLess(prepare.index('prepareSourceBoundStatusRead(operation,error)'),prepare.index('service().prepared.put('))
        validation=text[text.index('bool FSNavigationListener::validateAssistant('):text.index('void FSNavigationListener::dispatch(')]
        self.assertLess(validation.index('validateSourceBoundStatusRead(operation,error)'),validation.index('service().prepared.get('))
        dispatch=text[text.index('void FSNavigationListener::dispatch('):]
        read=dispatch[dispatch.index('if(op=="status")'):dispatch.index('auto* entry=self.prepared.get(')]
        self.assertNotIn('entry->executed',read)
        self.assertNotIn('prepared.',read)
        self.assertIn('getPermissionLevel("world_read")!="allow"',read)
        self.assertIn('!native(request,error)',read)
        for gate in ('fs_assistant_request_allowed(request)','fs_assistant::asyncAllowed()','request["__assistant_epoch"].asString()!=FSEventAPIBridge::instance().sessionReference()'):
            self.assertLess(dispatch.index(gate),dispatch.index('if(op=="status")'))
        # Mutation records and their guarded native submission remain intact.
        self.assertIn('entry->executed',dispatch[dispatch.index('auto* entry=self.prepared.get('):])
        self.assertIn('fs_assistant::asyncSubmit()',dispatch)

    def test_queued_native_teleport_revocation_and_no_replay(self):
        agent=(ROOT/'llagent.cpp').read_text()
        body=re.search(r'bool assistantSubmit\(\)\s*\{(.*?)\n    \}',agent,re.S)[1]
        program='''#include "fsassistantasync.h"
#include <cassert>
struct NativeRequest {fs_assistant::AsyncTaskPtr mAssistantTask;bool mAssistantSubmitted=false;
bool assistantSubmit(){BODY}
};
int main(){bool allowed=true;auto guard=std::make_shared<fs_assistant::AsyncOperation>([&](){return allowed?std::string():"revoked";});
NativeRequest queued;queued.mAssistantTask=std::make_shared<fs_assistant::AsyncTask>(guard);
allowed=false;assert(!queued.assistantSubmit());assert(guard->submitted()==0);
allowed=true;auto current=std::make_shared<fs_assistant::AsyncOperation>([](){return std::string();});NativeRequest one;one.mAssistantTask=std::make_shared<fs_assistant::AsyncTask>(current);
assert(one.assistantSubmit());assert(!one.assistantSubmit());assert(current->submitted()==1);
NativeRequest native;assert(native.assistantSubmit());assert(native.assistantSubmit());}
'''.replace('BODY',body)
        with tempfile.TemporaryDirectory() as temp:
            cpp=pathlib.Path(temp)/'request.cpp';cpp.write_text(program);exe=pathlib.Path(temp)/'request'
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-pedantic','-I'+str(ROOT),str(cpp),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
    def test_native_autopilot_checks_before_controls_and_never_stops_other_pilot(self):
        agent=(ROOT/'llagent.cpp').read_text()
        start=agent[agent.index('void LLAgent::autoPilot(F32 *delta_yaw)'):]
        self.assertLess(start.index('fs_navigation_tick();'),start.index('if (mAutoPilot)'))
        text=(ROOT/'fsnavigationlistener.cpp').read_text()
        self.assertIn('if(stop && !self.teleport && ownsPilot()) gAgent.stopAutoPilot(true)',text)
        self.assertIn('self.region==region',text)
        self.assertIn('fs_assistant::asyncTaskAllowed(self.task)',text)
        self.assertIn('self.flying==gAgent.getFlying()',text)
        self.assertNotIn('resetControlFlags(',text)
        start=text[text.index('LLSD approvalBaseline('):text.index('bool arguments(')]
        self.assertIn('"operation_id",self.token',start)
        self.assertIn('"owned",self.task',start)
        self.assertNotIn('gAgent.getPositionGlobal()',start)
        features=(ROOT/'fsassistantfeatures.h').read_text()
        self.assertIn('{"FSNavigation","stop","navigation_tools","world_read",false,false,"world_read"}',features)
    def test_delayed_teleport_transport_rechecks_original_authority(self):
        agent=(ROOT/'llagent.cpp').read_text()
        delayed=agent[agent.index('void LLAgent::startTeleportRequest()'):agent.index('void LLAgent::handleTeleportFinished()')]
        self.assertIn('guardedRequest->assistantAllowed()',delayed)
        send=agent[agent.index('void LLAgent::doTeleportViaLocation('):agent.index('void LLAgent::teleportViaLocationLookAt(')]
        self.assertLess(send.index('guardedRequest->assistantSubmit()'),send.index('teleportCore(is_local)'))
        self.assertIn('!guardedRequest->mAssistantTask',send)
        cancel=agent[agent.index('void LLAgent::cancelAssistantTeleport('):agent.index('void LLAgent::teleportCancel()')]
        self.assertIn('if(!hasAssistantTeleport(guard)) return',cancel)
        self.assertIn('gRlvHandler.getCanCancelTp()',cancel)
        text=(ROOT/'fsnavigationlistener.cpp').read_text()
        self.assertIn('task->guard()->submitted()==0',text)
        self.assertIn('avatar moved before queued teleport submission',text)
    def test_teleport_ui_exception_preserves_input_owner_checks(self):
        worker=(ROOT/'fssessionworker.cpp').read_text()
        fn=worker[worker.index('bool FSSessionWorker::assistantActiveAllowed(bool'):worker.index('void FSSessionWorker::importProfileSettings()')]
        for gate in ('worker->inputGranted','worker->mode==Mode::Active','!worker->promoting','!worker->demoting','!worker->transition.active()','allowTeleport || !gTeleportDisplay','!LLFloaterReg::instanceVisible("preferences")'):
            self.assertIn(gate,fn)
        bridge=(ROOT/'fseventapibridge.cpp').read_text()
        self.assertIn('assistantActiveAllowed(api=="FSNavigation" && op=="teleport")',bridge)
