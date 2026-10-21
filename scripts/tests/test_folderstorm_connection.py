"""Execute affected production controller/worker methods with small UI stubs.

These focused harnesses exercise real orchestration, not Win32 focus or native
viewer execution. No viewer/host build, login, network or process suspension.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class ConnectionIntegration(unittest.TestCase):
    def run_cpp(self, source):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not shutil.which(compiler[0]):
            self.skipTest("C++ compiler required")
        with tempfile.TemporaryDirectory(prefix="folderstorm-connection-") as temporary:
            path = Path(temporary)
            (path / "test.cpp").write_text(source)
            executable = path / "check"
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wshadow", "-pedantic",
                                      "-I" + str(ROOT / "indra/newview"), str(path / "test.cpp"),
                                      "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)

    def test_actual_worker_pause_reset_and_command_guards(self):
        worker = (ROOT / "indra/newview/fssessionworker.cpp").read_text()
        dispatch = function(worker, "    void command(const Message& request)")
        # Run the production pre-dispatch guards; count fallthrough as a native
        # action. Paused stale chat/actions must never reach that point.
        prefix = dispatch.split("        switch (request.kind)", 1)[0] + "++nativeActions;\n}"
        methods = function(worker, "    void pauseControl()") + "\n" + function(worker, "    void recoverControl(") + "\n" + prefix
        self.run_cpp(r'''
#include "fssessionconnection.h"
#include <cassert>
#include <sstream>
using namespace fs_session;
bool gDisconnected=false;
constexpr bool TRUE=true,FALSE=false;
constexpr int SW_RESTORE=9,SW_HIDE=0;
int windowEnabled=-1,windowShown=-1;
void EnableWindow(int,bool value){windowEnabled=value;}
void ShowWindow(int,int value){windowShown=value;}
unsigned long long GetTickCount64(){return 100000;}
std::ostringstream diagnostic;
#define LL_WARNS(tag) diagnostic
#define LL_INFOS(tag) diagnostic
#define LL_ENDL '\n'
struct Worker {
    WorkerId id{};std::uint64_t generation=3,sequence=10,lastCommand=0;
    bool controlPaused=false,recoveryReplyPending=false,promoting=true,demoting=false;
    bool readyApplied=true,input=true,voice=true,detached=false,canUnembed=true,restored=false;
    int window=1,nativeActions=0,grants=0,releases=0;
    Mode mode=Mode::Active;
    Message pendingPromotion,pendingDemotion,recoveryReply,lastReply;
    struct Transition {bool cancelled=false;void cancel(){cancelled=true;}} transition;
    struct Pipe {bool closed=false;void close(){closed=true;}} pipe;
    void releaseInput(){++releases;input=voice=false;}
    bool unembed(bool restore){restored=restore;return canUnembed;}
    bool detach(){detached=true;return true;}
    Message status(const Message& request)const {
        auto out=request;out.kind=Kind::Status;out.worker=id;out.generation=generation;
        out.account="11111111-1111-1111-1111-111111111111";out.grid="grid";out.mode=mode;
        out.flags=0;if(controlPaused)out.flags|=ControlPaused;return out;
    }
    void reply(const Message& request,const std::string& error={}){lastReply=status(request);if(!error.empty())lastReply.flags|=Error;}
''' + methods + r'''
};
int main(){
    Worker w;w.id[0]=1;w.pendingPromotion.sequence=8;
    w.pauseControl();assert(w.controlPaused && !w.input && !w.voice && w.mode==Mode::Warm);
    assert(!w.detached && !w.pipe.closed && !w.promoting && w.transition.cancelled && windowEnabled==0);
    assert(w.recoveryReplyPending && w.recoveryReply.sequence==8);
    w.pauseControl();assert(w.releases==1); // No repeated revocation or response creation.
    Message send;send.worker=w.id;send.generation=w.generation;send.sequence=11;send.kind=Kind::SendChat;
    w.command(send);assert(w.nativeActions==0 && (w.lastReply.flags&ControlPaused) && (w.lastReply.flags&Error));
    auto reset=w.status(send);reset.kind=Kind::Recover;reset.sequence=12;
    auto wrong=reset;wrong.grid="other";w.command(wrong);
    assert(w.controlPaused && (w.lastReply.flags&Error) && w.grants==0);
    reset.sequence=13;w.command(reset);
    assert(!w.controlPaused && !w.input && !w.voice && w.mode==Mode::Warm && !w.restored && windowShown==SW_HIDE);
    assert(!(w.lastReply.flags&(Error|ControlPaused)) && w.grants==0 && w.nativeActions==0);
    auto stale=reset;stale.sequence=14;--stale.generation;w.controlPaused=true;w.command(stale);
    assert(w.controlPaused && (w.lastReply.flags&Error));
    w.canUnembed=false;reset.sequence=15;w.command(reset);assert(w.controlPaused && (w.lastReply.flags&Error));
    w.canUnembed=true;gDisconnected=true;reset.sequence=16;w.command(reset);
    assert(!w.controlPaused && !w.input && !w.voice && w.restored && windowShown==SW_RESTORE && windowEnabled==1);
    // Authentication/sequence failures still fail closed; recovery never adopts a foreign peer.
    auto foreign=reset;foreign.sequence=17;++foreign.worker[0];w.command(foreign);
    assert(w.detached && w.pipe.closed && w.nativeActions==0);
}
''')

    def test_actual_host_quarantine_and_refused_promotion(self):
        host = (ROOT / "tools/session-host/main.cpp").read_text()
        methods = "\n".join(function(host, signature) for signature in [
            "    void beginConnectionRecovery(int index)", "    void stopConnectionRecovery(bool keepLoggedIn)",
            "    bool waitingForLostOwner(int target) const", "    void failHandoff(int index,"])
        self.run_cpp(r'''
#include "fssessionconnection.h"
#include "fssessionregistry.h"
#include <cassert>
#include <memory>
#include <map>
using namespace fs_session;
using HANDLE=void*;
HANDLE GetPropW(void*,const wchar_t*){return nullptr;}
unsigned int GetCurrentProcessId(){return 123;}
const wchar_t* InputLeaseProperty=L"test";
struct Slot {
    LinkRecovery recovery;Message snapshot,request;bool waiting=true,detached=false,lossRecorded=false;
    unsigned long long recoveryAt=0;
    bool hasPendingSend=true,hasPendingTyping=true,hasPendingAction=true,hasPendingReview=true,hasPendingContact=true,hasPendingSkip=true;
    struct Restart {int cancellations=0;void cancel(){++cancellations;}} restart;
    struct Pipe {bool connected=true;int closes=0;bool alive()const{return connected;}void close(){connected=false;++closes;}} pipe;
    void* surface()const{return nullptr;}
};
struct Receipt {std::string state,error;bool submitted=false;};
struct Host {
    std::array<std::unique_ptr<Slot>,MaxCharacters> slots;
    int active=0,selected=-1,logs=0,messages=0;
    bool detaching=false,closing=false,logoutOnClose=false;
    Handoff handoff;
    struct SetOpen {int cancellations=0;void cancel(){++cancellations;}} setOpen;
    std::array<std::string,MaxCharacters> controllerNativeRequests;
    std::string controllerHandoffRequest;
    struct Operations {
        std::map<std::string,Receipt> values;
        Receipt* find(const std::string& id){const auto i=values.find(id);return i==values.end()?nullptr:&i->second;}
    } controllerOperations;
    Host(){for(int i=0;i<MaxCharacters;++i){slots[i]=std::make_unique<Slot>();auto& m=slots[i]->snapshot;m.worker[0]=static_cast<std::uint8_t>(i+1);m.generation=3;m.mode=Mode::Warm;m.state=State::Ready;}}
    void message(const std::wstring&){++messages;}
    void connectionLog(int,ConnectionEvent){++logs;}
    void finishMonitorExchange(bool){}
    void switchTo(int index){selected=index;}
''' + methods + r'''
};
int main(){
    Host h;h.slots[0]->request.kind=Kind::SendChat;
    h.controllerNativeRequests[0]="receipt";h.controllerOperations.values["receipt"].submitted=true;
    h.beginConnectionRecovery(0);
    auto& first=*h.slots[0];
    assert(first.recovery.active() && !first.recovery.resetReady() && h.active==-1 && first.pipe.alive());
    assert(!first.hasPendingSend && !first.hasPendingTyping && !first.hasPendingAction && !first.hasPendingSkip && !first.hasPendingContact);
    assert(first.restart.cancellations==1 && h.controllerOperations.values["receipt"].state=="uncertain");
    assert(h.waitingForLostOwner(1));h.beginConnectionRecovery(0);assert(h.logs==1);
    // An unresponsive target during old-owner demotion quarantines both involved
    // requests. Neither a delayed revoke nor a delayed grant is replayed.
    Host pair;assert(pair.handoff.begin(0,1,3,3));pair.beginConnectionRecovery(1);
    assert(pair.handoff.step()==Handoff::Step::Idle && pair.active==-1);
    assert(pair.slots[0]->recovery.active() && pair.slots[1]->recovery.active() && pair.waitingForLostOwner(2));
    // A known native refusal (e.g. Preferences open) used to close the pipe even
    // after its reply. The production failure path now resets it and restores A.
    Host refused;assert(refused.handoff.begin(0,1,3,3));auto warm=refused.slots[0]->snapshot;
    warm.kind=Kind::Status;assert(refused.handoff.accept(0,warm));refused.active=-1;refused.slots[1]->waiting=false;
    refused.failHandoff(1,L"Preferences open");
    assert(refused.slots[1]->pipe.alive() && refused.slots[1]->pipe.closes==0 && refused.slots[1]->recovery.resetReady());
    assert(refused.selected==0 && refused.waitingForLostOwner(0));
    Host changed;assert(changed.handoff.begin(0,1,3,3));assert(changed.handoff.accept(0,warm));
    ++changed.slots[0]->snapshot.generation;changed.failHandoff(1,L"changed");assert(changed.selected==-1);
    // Recovery cannot replay an old whole-host logout/detach or restart intent.
    Host closing;closing.detaching=closing.closing=closing.logoutOnClose=true;
    closing.slots[0]->waiting=false;closing.beginConnectionRecovery(0);
    assert(!closing.detaching && !closing.closing && !closing.logoutOnClose && closing.slots[0]->recovery.resetReady());
    Host keep;keep.beginConnectionRecovery(0);keep.stopConnectionRecovery(true);
    assert(!keep.slots[0]->pipe.alive() && !keep.slots[0]->waiting && keep.slots[0]->detached);
    Host logout;logout.beginConnectionRecovery(0);logout.stopConnectionRecovery(false);
    assert(logout.slots[0]->pipe.alive()); // Keep native close confirmation; no forced termination.
}
''')


if __name__ == "__main__":
    unittest.main()
