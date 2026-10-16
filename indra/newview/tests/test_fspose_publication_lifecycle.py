"""Compile actual manager method bodies against portable viewer boundary stubs.

This checks local queue/publication logic, not native rendering or transport.
"""
from pathlib import Path
import os
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def body(text, signature):
    start = text.index("{", text.index(signature))
    end, depth = start + 1, 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def run_lifecycle(source_ref=None):
    def read(name):
        path = "indra/newview/" + name
        if source_ref:
            return subprocess.check_output(["git", "show", source_ref + ":" + path], cwd=ROOT, text=True)
        return (ROOT / path).read_text(encoding="utf-8")

    source, header = read("fssharedposemanager.cpp"), read("fssharedposemanager.h")
    binding = "struct NativeWorkBinding " + body(header, "struct NativeWorkBinding") + ";"
    work = "struct Work : FSSharedPose::EditGesture, NativeWorkBinding " + body(header, "struct Work :") + ";"
    fixture = r'''
#include "fssharedposeeditqueue.h"
#include "fssharedposepolicy.h"
#include <cassert>
#include <memory>
#include <optional>
using U32 = std::uint32_t;
struct LLUUID {
 std::string id;
 LLUUID()=default; LLUUID(std::string value):id(std::move(value)){}
 bool isNull()const{return id.empty();} bool notNull()const{return !isNull();}
 std::string asString()const{return id;}
 static const LLUUID null;
 static LLUUID generateNewID(){static unsigned n=0;return LLUUID("generation-"+std::to_string(++n));}
 bool operator==(const LLUUID& other)const{return id==other.id;}
 bool operator!=(const LLUUID& other)const{return !(*this==other);}
 bool operator<(const LLUUID& other)const{return id<other.id;}
};
const LLUUID LLUUID::null{};
const LLUUID gAgentID("self");
using Pose=FSSharedPose::Pose;using JointSet=FSSharedPose::JointSet;
Pose subset(const Pose& pose,const JointSet& joints){return fs_pose::observedWorkPose(pose,joints);}
struct Data{std::string session;Data operator[](const char*)const{return *this;}std::string asString()const{return session;}};
double now(){return 1.;}std::string uuid(){return "fresh-native-session";}int avatarFor(const LLUUID&){return 0;}
struct Applying{bool& value;bool old;explicit Applying(bool& v):value(v),old(v){value=true;}~Applying(){value=old;}};
class FSSharedPoseManager {
public:
@BINDING@
@WORK@
 struct Remote{LLUUID session,generation;Pose pose;fs_pose::ReceivedOverlay overlay;};
 struct Approval{LLUUID session,generation,token;};struct AssistantWork{LLUUID session;};
 struct Queued{Data data;std::shared_ptr<AssistantWork> assistant;std::optional<NativeWorkBinding> native;};
 std::map<LLUUID,std::deque<Work>> mWorks;std::map<LLUUID,Remote> mRemotes;std::map<LLUUID,Approval> mApprovals;
 std::deque<Queued> mQueue;std::set<LLUUID> mBlockedEdits;std::shared_ptr<AssistantWork> mAssistantPublishing;
 FSSharedPose::OwnerSession mOwner;LLUUID mPublicGeneration;Pose mPublished,local;std::string mStatus;
 U32 mPublishedRevision=0;double mLastHeartbeat=0.;bool mApplying=false;
 struct Animator{Pose* local;void tryPosingAvatar(int){}Pose captureSharedPose(int)const{return *local;}}mAnimator{&local};
 bool available()const{return true;}bool nearby(const LLUUID&)const{return true;}
 bool canEdit(const LLUUID& id)const{return id==gAgentID&&mOwner.active();}
 bool isShared(const LLUUID& id)const{return canEdit(id);}
 bool nativeWorkCurrent(const NativeWorkBinding&)const;bool nativeQueueCurrent(const std::deque<Work>&)const;
 bool nativePacketCurrent(const NativeWorkBinding&,bool)const;
 bool edit(const LLUUID&,const JointSet&,std::function<void()>);void execute(Work&);void finishEdit(const LLUUID&);
 void apply(const LLUUID&,const Pose& pose){for(const auto& item:pose)local[item.first]=item.second;}
 void restorePreviews(const LLUUID&);void retireNativeOwnerWork();void complete(const LLUUID&,bool);
 void endAssistantPublishing(const std::string&);void ensureSharedSelf(bool);
 void sendNative(const Work&,int){}int workMessage(const Work&,const char*){return 0;}
 void finishAssistant(const std::shared_ptr<AssistantWork>&,const char*,const std::string&){}
 void snapshot(const LLUUID&){}
};
'''.replace("@BINDING@", binding).replace("@WORK@", work)
    signatures = (
        "bool FSSharedPoseManager::nativeWorkCurrent(const NativeWorkBinding& work) const",
        "bool FSSharedPoseManager::nativeQueueCurrent(const std::deque<Work>& queue) const",
        "bool FSSharedPoseManager::nativePacketCurrent(const NativeWorkBinding& binding, bool release) const",
        "bool FSSharedPoseManager::edit(const LLUUID& id, const FSSharedPose::JointSet& joints, std::function<void()> action)",
        "void FSSharedPoseManager::execute(Work& work)",
        "void FSSharedPoseManager::finishEdit(const LLUUID& id)",
        "void FSSharedPoseManager::restorePreviews(const LLUUID& id)",
        "void FSSharedPoseManager::retireNativeOwnerWork()",
        "void FSSharedPoseManager::complete(const LLUUID& id, bool accepted)",
        "void FSSharedPoseManager::endAssistantPublishing(const std::string& reason)",
        "void FSSharedPoseManager::ensureSharedSelf(bool nativeTakeover)",
    )
    for signature in signatures:
        # The old candidate has no retirement method; it never calls that stub.
        if signature in source:
            method = body(source, signature)
        elif source_ref and signature.endswith("::retireNativeOwnerWork()"):
            method = "{}"
        else:
            raise AssertionError("Missing production method: " + signature)
        fixture += "\n" + signature + "\n" + method + "\n"
    fixture += r'''
void seed(FSSharedPoseManager& m){
 m.mOwner.start("self","native-session",{{"mHead",{}}});
 m.mOwner.grant("peer","peer-token",0);
 m.mPublicGeneration=LLUUID::generateNewID();m.local=m.mOwner.pose();
 m.mAssistantPublishing=std::make_shared<FSSharedPoseManager::AssistantWork>();
 m.mAssistantPublishing->session=LLUUID(m.mOwner.session());
 assert(m.edit(gAgentID,{"mHead"},[&m]{m.local.at("mHead").transform[4]=5.f;}));
 auto& front=m.mWorks.at(gAgentID).front();front.asked=front.granted=true;
 assert(m.mOwner.acquire("self","",front.lease.asString(),front.joints,0).granted);
 m.finishEdit(gAgentID);
 assert(m.edit(gAgentID,{"mHead"},[&m]{m.local.at("mHead").transform[4]=9.f;}));
 assert(m.mWorks.at(gAgentID).size()==2 && m.nativeQueueCurrent(m.mWorks.at(gAgentID)));
 m.mQueue.push_back({{m.mOwner.session()},{},static_cast<const FSSharedPoseManager::NativeWorkBinding&>(front)});
 auto unrelated=std::make_shared<FSSharedPoseManager::AssistantWork>();unrelated->session=LLUUID("other-session");
 m.mQueue.push_back({{"other-session"},unrelated,{}});
}
int main(){
 {FSSharedPoseManager m;seed(m);
  const auto old=m.mWorks.at(gAgentID).front();
  m.endAssistantPublishing("assistant_revoked");
  assert(m.local.at("mHead").transform[4]==9.f); // never restore the older canonical zero
  assert(!m.mOwner.active() && !m.mWorks.count(gAgentID));
  assert(m.mQueue.size()==1 && m.mQueue.front().assistant);
  assert(!m.nativePacketCurrent(old,false) && !m.nativePacketCurrent(old,true));}
 {FSSharedPoseManager m;seed(m);
  const auto old=m.mWorks.at(gAgentID).front();const auto previous=m.mPublicGeneration;
  m.ensureSharedSelf(true);
  assert(m.local.at("mHead").transform[4]==9.f);
  assert(m.mWorks.at(gAgentID).size()==2 && m.nativeQueueCurrent(m.mWorks.at(gAgentID)));
  assert(m.nativePacketCurrent(old,false) && m.nativePacketCurrent(old,true));
  assert(m.mPublicGeneration!=previous && !m.mAssistantPublishing);
  assert(m.mOwner.session()=="native-session" && m.mOwner.authorized("peer","peer-token",1));
  assert(m.mOwner.reservationOwner("mHead",1)=="self");
  m.endAssistantPublishing("old_mcp_cleanup");assert(m.local.at("mHead").transform[4]==9.f);
  assert(m.mOwner.active() && m.nativeQueueCurrent(m.mWorks.at(gAgentID)));}
 {FSSharedPoseManager m;seed(m);m.complete(gAgentID,false);
  assert(m.local.at("mHead").transform[4]==0.f); // rejected native drags still roll back
  assert(!m.mWorks.count(gAgentID) && m.mBlockedEdits.count(gAgentID));}
 {FSSharedPoseManager m;seed(m);const auto old=m.mWorks.at(gAgentID).front();
  m.mOwner.start("self","replacement-session",{{"mHead",{}}});
  assert(!m.nativePacketCurrent(old,false));m.complete(gAgentID,false);
  assert(m.local.at("mHead").transform[4]==9.f && m.mOwner.session()=="replacement-session");}
}
'''
    configured = os.environ.get("CXX", "")
    compiler = ([configured] if configured and Path(configured).is_file() else shlex.split(configured)) or [shutil.which("c++") or shutil.which("cl") or "c++"]
    with tempfile.TemporaryDirectory(prefix="fspose-lifecycle-") as folder:
        path = Path(folder)
        (path / "lifecycle.cpp").write_text(fixture, encoding="utf-8")
        msvc = Path(compiler[0]).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe")
        executable = path / ("lifecycle.exe" if os.name == "nt" else "lifecycle")
        flags = (["/std:c++17", "/EHsc", "/W4", "/WX", "/UNDEBUG", "/utf-8", "/I" + str(ROOT / "indra/newview"), str(path / "lifecycle.cpp"), "/Fe" + str(executable)] if msvc else
                 ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-UNDEBUG", "-I", str(ROOT / "indra/newview"), str(path / "lifecycle.cpp"), "-o", str(executable)])
        built = subprocess.run(compiler + flags, cwd=path, capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(built.stdout + built.stderr)
        return subprocess.run([str(executable)], cwd=path, capture_output=True, text=True)


class PublicationLifecycle(unittest.TestCase):
    def test_actual_native_methods_preserve_manual_publication_ownership(self):
        result = run_lifecycle()
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
