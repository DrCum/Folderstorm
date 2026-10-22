"""Exercise the actual native Source/Entry/Service without a viewer or GL build.

Only the viewer API boundary and worker result are replaced. The production
controller body is compiled unchanged, so cancellation/admission tests cover
the same branches used by native Chat.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#include "fschatmediacodec.h"
#include <cassert>
#include <chrono>
#include <future>
#include <map>
#include <set>
#include <functional>
#include <atomic>
#include <thread>
using namespace fs_chat_media;
using Clock = std::chrono::steady_clock;
using U32 = std::uint32_t;
struct LLUUID {
    std::string value;
    LLUUID() = default;
    LLUUID(const char* v):value(v){}
    explicit LLUUID(std::string v):value(std::move(v)){}
    bool notNull() const{return !value.empty();}
    bool isNull() const{return value.empty();}
    std::string asString() const{return value;}
    bool operator!=(const LLUUID& other) const{return value!=other.value;}
    static LLUUID generateNewID(){static unsigned next=0;return LLUUID(std::to_string(++next));}
};
struct Agent {LLUUID account{"account"},login{"login"};LLUUID getID()const{return account;}LLUUID getSessionID()const{return login;}} gAgent;
struct LLApp {static bool isExiting(){return false;}};
bool gDisconnected=false;
constexpr int STATE_STARTED=1;
struct LLStartUp {static int getStartupState(){return STATE_STARTED;}};
struct LLGridManager {static LLGridManager* getInstance(){static LLGridManager v;return &v;}std::string getGrid()const{return "grid";}};
struct LLProxy {bool enabled=false;static bool instanceExists(){return true;}static LLProxy* getInstance(){static LLProxy v;return &v;}bool isHTTPProxyEnabled()const{return enabled;}};
struct LLMute {static constexpr int flagTextChat=1;};
struct LLMuteList {bool muted=false;static LLMuteList* getInstance(){static LLMuteList v;return &v;}bool isMuted(const LLUUID&,int)const{return muted;}};
enum {RLV_BHVR_RECVCHAT,RLV_BHVR_RECVEMOTE,RLV_BHVR_RECVCHATFROM,RLV_BHVR_RECVEMOTEFROM,RLV_BHVR_SHOWNAMES,RLV_BHVR_SHOWLOC};
struct RlvHandler {std::set<int> blocked;std::set<int> exceptions;bool hasBehaviour(int b)const{return blocked.count(b)!=0;}bool isException(int b,const LLUUID&)const{return exceptions.count(b)!=0;}} gRlvHandler;
struct RlvActions {static inline std::set<std::string> blocked;static bool canReceiveIM(const LLUUID& id){return blocked.count(id.value)==0;}};
struct Preferences {int mode=1,size=320;bool animate=false;std::string sites=DefaultSites;};
Preferences savedPreferences(){return {};}
struct Dir {std::string getCAFile()const{return {};}} directory;
Dir* gDirUtilp=&directory;
struct Callbacks {void addFunction(void(*)(void*),void*){}void deleteFunction(void(*)(void*),void*){}} gIdleCallbacks;
struct LLFrameTimer {static U32 getFrameCount(){return 1;}};
std::shared_future<void> workerRelease;
std::shared_ptr<Image> load(const std::string&,const std::string&,const std::string&,const std::atomic<bool>&)
{
    workerRelease.wait(); // Deliberately return a late result even after cancel.
    auto image=std::make_shared<Image>();image->width=image->height=1;image->frames.push_back({{1,2,3,255},100});return image;
}
'''

CASES = r'''
Source source(bool nearby=false,bool group=false)
{return {gAgent.getID(),gAgent.getSessionID(),LLUUID("sender"),LLUUID("conversation"),"grid",nearby,group};}
int main()
{
    {
        std::promise<void> release;workerRelease=release.get_future().share();Service host;
        auto entry=host.request(source(),"https://cdn.example.org/a.png");assert(entry);
        gAgent.login=LLUUID("new-login");host.poll();assert(*entry->cancelled);
        release.set_value();entry->job.wait();host.poll();assert(!entry->image && entry->finished);
        gAgent.login=LLUUID("login");
    }
    {
        std::promise<void> release;workerRelease=release.get_future().share();Service host;
        auto entry=host.request(source(false,true),"https://cdn.example.org/a.png");assert(entry);
        RlvActions::blocked.insert("conversation");host.poll();assert(*entry->cancelled);
        release.set_value();entry->job.wait();host.poll();assert(!entry->image);
        RlvActions::blocked.clear();
    }
    {
        std::promise<void> release;workerRelease=release.get_future().share();Service host;
        auto entry=host.request(source(true),"https://cdn.example.org/a.png");assert(entry);
        gRlvHandler.exceptions.insert(RLV_BHVR_RECVCHATFROM);host.poll();assert(*entry->cancelled);
        release.set_value();entry->job.wait();host.poll();assert(!entry->image);
        gRlvHandler.exceptions.clear();
    }
    {
        std::promise<void> release;workerRelease=release.get_future().share();Service host;
        auto entry=host.request(source(),"https://cdn.example.org/a.png");assert(entry);
        host.clear();assert(*entry->cancelled && entry->job.valid());
        assert(!host.request(source(),"https://cdn.example.org/a.png"));
        release.set_value();entry->job.wait();host.poll();assert(!entry->image);
    }
    {
        std::promise<void> release;workerRelease=release.get_future().share();Service host;
        std::vector<std::shared_ptr<Entry>> held;
        for(unsigned i=0;i<MaxJobs;++i)held.push_back(host.request(source(),"https://cdn.example.org/"+std::to_string(i)+".png"));
        for(const auto& entry:held)assert(entry);
        assert(!host.request(source(),"https://cdn.example.org/extra.png"));
        release.set_value();for(const auto& entry:held)entry->job.wait();host.poll();
        for(const auto& entry:held)assert(entry->image);
    }
    {
        std::promise<void> release;workerRelease=release.get_future().share();release.set_value();Service host;
        std::vector<std::shared_ptr<Entry>> held;unsigned purged=0;
        for(unsigned i=0;i<MaxEntries;++i)
        {
            auto entry=std::make_shared<Entry>();entry->source=source();entry->url="https://cdn.example.org/"+std::to_string(i)+".png";
            entry->finished=true;entry->image=load("","","",*entry->cancelled);
            entry->lastSeen=Clock::now()-std::chrono::seconds(3);entry->releaseViews.push_back([&]{++purged;});
            host.entries[entry->source.key(entry->url)]=entry;held.push_back(entry);
        }
        auto fresh=host.request(source(),"https://cdn.example.org/new.png");assert(fresh);
        assert(host.entries.size()==MaxEntries && purged==1);
        fresh->job.wait();host.poll();assert(fresh->image);
    }
    {
        std::promise<void> release;workerRelease=release.get_future().share();release.set_value();Service host;
        std::vector<std::shared_ptr<Entry>> held;
        for(unsigned i=0;i<MaxEntries;++i)
        {auto entry=std::make_shared<Entry>();entry->source=source();entry->finished=true;host.entries[std::to_string(i)]=entry;held.push_back(entry);}
        assert(!host.request(source(),"https://cdn.example.org/new.png")); // Visible entries stay pinned.
        assert(host.entries.size()==MaxEntries);
    }
    LLProxy::getInstance()->enabled=true;assert(!source().valid());LLProxy::getInstance()->enabled=false;
    LLMuteList::getInstance()->muted=true;assert(!source().valid());LLMuteList::getInstance()->muted=false;
}
'''


class NativeServiceTests(unittest.TestCase):
    def test_production_controller(self):
        text = (ROOT / "fschatmedia.cpp").read_text()
        source = text[text.index("struct Source\n"):text.index("std::mutex decodeMutex;")]
        service = text[text.index("class Service\n"):text.index("Service& service()")]
        with tempfile.TemporaryDirectory(prefix="fs-chat-service-") as tmp:
            cpp = Path(tmp) / "service.cpp"
            exe = Path(tmp) / "service"
            cpp.write_text(STUBS + source + service + CASES)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(ROOT), str(cpp), "-o", str(exe)], check=True, timeout=45)
            subprocess.run([str(exe)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
