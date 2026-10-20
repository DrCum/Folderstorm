"""Native ownership/transport contracts plus actual request-guard reproduction."""
import pathlib
import re
import subprocess
import tempfile
import unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class NavigationNativeTests(unittest.TestCase):
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
