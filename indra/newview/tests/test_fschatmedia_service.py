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
#include <condition_variable>
#include <queue>
using namespace fs_chat_media;
namespace fs_chat_media {
class AnimationSource { public: unsigned loops=0; };
class AnimationDecoder { unsigned count=0; public: explicit AnimationDecoder(std::shared_ptr<const AnimationSource>){} std::shared_ptr<Frame> next(const std::atomic<bool>& cancelled){if(cancelled||count++>=3)return {};auto frame=std::make_shared<Frame>();frame->rgba={1,2,3,255};return frame;}bool ended()const{return count>=3;} };
}

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
std::shared_ptr<Image> load(const std::string&,const std::string&,const std::string&,const std::atomic<bool>&,int=1024)
{
    workerRelease.wait(); // Deliberately return a late result even after cancel.
    auto image=std::make_shared<Image>();image->width=image->height=1;image->frames.push_back({{1,2,3,255},100,{}});return image;
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
            entry->lastSeen=Clock::now()-std::chrono::seconds(3);entry->releaseViews.push_back([&](bool discard){if(discard)++purged;return true;});
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
    {
        Service host;std::vector<std::shared_ptr<Entry>> held;
        for(int i=0;i<6;++i){auto entry=std::make_shared<Entry>();entry->source=source();entry->finished=true;
          entry->image=std::make_shared<Image>();entry->image->width=entry->image->height=1;entry->image->frames.resize(1);
          entry->image->animation=std::make_shared<AnimationSource>();entry->animationDemand=Clock::now();
          held.push_back(entry);host.entries[std::to_string(i)]=entry;}
        for(int pass=0;pass<10;++pass){host.poll();unsigned pending=0;for(auto& entry:held)if(entry->animationJob.valid()){++pending;entry->animationJob.wait();}
          assert(pending<=2);}
        for(const auto& entry:held)assert(entry->animationSerial>0); // Fair admission, not the first two only.
        host.clear();host.poll();for(const auto& entry:held)assert(*entry->cancelled&&!entry->animationFrame);
    }
    LLProxy::getInstance()->enabled=true;assert(!source().valid());LLProxy::getInstance()->enabled=false;
    LLMuteList::getInstance()->muted=true;assert(!source().valid());LLMuteList::getInstance()->muted=false;
}
'''



CACHED_STUBS = r"""
struct Rect {
 int left=0,top=100,right=100,bottom=0;
 void intersectWith(const Rect& b){left=(std::max)(left,b.left);top=(std::min)(top,b.top);right=(std::min)(right,b.right);bottom=(std::max)(bottom,b.bottom);}
 bool isEmpty()const{return left>=right||bottom>=top;}
};
struct LLView;
struct Handle{std::weak_ptr<LLView*> ref;LLView* get()const{const auto p=ref.lock();return p?*p:nullptr;}};
struct LLView{
 std::shared_ptr<LLView*> life=std::make_shared<LLView*>(this);bool visible=true;Rect rect;LLView* parent=nullptr;
 virtual ~LLView(){*life=nullptr;}Handle getHandle(){return {life};}
 bool isInVisibleChain()const{return visible&&(!parent||parent->isInVisibleChain());}
 Rect calcScreenRect()const{return rect;}LLView* getParent()const{return parent;}
};
struct LLScrollContainer:LLView{};
struct Texture{static inline unsigned live=0;Texture(){++live;}~Texture(){--live;}};
"""
CACHED_CASES = r"""
int main(){
 Service host;auto entry=std::make_shared<Entry>();entry->source.account=gAgent.getID();entry->source.login=gAgent.getSessionID();entry->source.grid="grid";
 entry->finished=true;entry->image=std::make_shared<Image>();entry->image->width=1024;entry->image->height=1024;entry->image->frames.resize(1);host.entries["same-url"]=entry;
 LLScrollContainer viewport;std::vector<std::unique_ptr<ImageCard>> cards;
 for(unsigned i=0;i<100;++i){auto card=std::make_unique<ImageCard>();card->parent=&viewport;card->attach(entry);cards.push_back(std::move(card));}
 assert(Texture::live==100&&host.entries.size()==1&&host.memory()==4u*1024*1024);
 for(unsigned i=1;i<100;++i)cards[i]->rect={0,1100,100,1000};
 host.poll();assert(Texture::live==1);for(unsigned i=1;i<100;++i)assert(!cards[i]->mTexture&&cards[i]->mFrame==-1&&cards[i]->mRequested);
 cards[0]->visible=false;host.poll();assert(Texture::live==0);
 // A returning card has its frame invalidated for the normal draw upload.
 cards[0]->visible=true;cards[0]->mTexture=std::make_shared<Texture>();cards[0]->mFrame=0;host.poll();assert(Texture::live==1);
 cards.resize(1);host.poll();assert(entry->releaseViews.size()==1);host.clear();assert(Texture::live==0&&!cards[0]->mRequested);
}
"""

class NativeServiceTests(unittest.TestCase):
    def test_repeated_url_offscreen_textures(self):
        text = (ROOT / "fschatmedia.cpp").read_text()
        source = text[text.index("struct Source\n"):text.index("std::mutex decodeMutex;")]
        service = text[text.index("class FrameWorkers\n"):text.index("Service& service()")]
        attach = text[text.index("    void attach("):text.index("    void setLinkHidden(")]
        visible = text[text.index("    bool visibleInChat()"):text.index("    void activate()")]
        card = "class ImageCard:public LLView {public:std::shared_ptr<Entry> mEntry;std::shared_ptr<Texture> mTexture=std::make_shared<Texture>();std::shared_ptr<void> mTextureReservation;int mFrame=0;bool mRequested=true;void syncLinkVisibility(){}\n" + attach + visible + "};\n"
        with tempfile.TemporaryDirectory(prefix="fs-chat-textures-") as tmp:
            cpp = Path(tmp) / "textures.cpp"
            exe = Path(tmp) / "textures"
            cpp.write_text(STUBS + source + service + CACHED_STUBS + card + CACHED_CASES)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(ROOT), str(cpp), "-o", str(exe)], check=True, timeout=45)
            subprocess.run([str(exe)], check=True, timeout=20)

    def test_production_controller(self):
        text = (ROOT / "fschatmedia.cpp").read_text()
        source = text[text.index("struct Source\n"):text.index("std::mutex decodeMutex;")]
        service = text[text.index("class FrameWorkers\n"):text.index("Service& service()")]
        with tempfile.TemporaryDirectory(prefix="fs-chat-service-") as tmp:
            cpp = Path(tmp) / "service.cpp"
            exe = Path(tmp) / "service"
            cpp.write_text(STUBS + source + service + CASES)
            subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread", "-I", str(ROOT), str(cpp), "-o", str(exe)], check=True, timeout=45)
            subprocess.run([str(exe)], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
