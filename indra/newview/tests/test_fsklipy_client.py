"""Compile the production KLIPY lifecycle and send tracking at viewer boundaries.

No API requests: held futures simulate late/cancelled responses. These checks use
the real controller/acceptance bodies, not a second model of their transitions.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run(body):
    with tempfile.TemporaryDirectory(prefix="fs-klipy-") as tmp:
        cpp, exe = Path(tmp) / "check.cpp", Path(tmp) / "check"
        cpp.write_text(body)
        subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
            "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread",
            "-I", str(ROOT), str(cpp), "-o", str(exe)], check=True, timeout=45)
        subprocess.run([str(exe)], check=True, timeout=20)


COMMON = r'''
#include "fsklipy.h"
#include <cassert>
#include <chrono>
#include <deque>
#include <future>
#include <optional>
#include <functional>
#include <map>
#include <cctype>
struct LLUUID {
 std::string value;LLUUID()=default;LLUUID(const char* s):value(s){}
 explicit LLUUID(std::string s):value(std::move(s)){}
 std::string asString()const{return value;}
 bool operator==(const LLUUID& b)const{return value==b.value;}
 static const LLUUID null;
};
const LLUUID LLUUID::null;
struct Agent{LLUUID account{"account"},login{"login"};LLUUID getID()const{return account;}LLUUID getSessionID()const{return login;}}gAgent;
bool gDisconnected=false;
struct LLApp{static bool isExiting(){return false;}};
constexpr int STATE_STARTED=1;
struct LLStartUp{static int getStartupState(){return STATE_STARTED;}};
struct LLGridManager{std::string grid="grid";static LLGridManager* getInstance(){static LLGridManager v;return &v;}std::string getGrid()const{return grid;}};
struct LLProxy{bool enabled=false;static bool instanceExists(){return true;}static LLProxy* getInstance(){static LLProxy v;return &v;}bool isHTTPProxyEnabled()const{return enabled;}};
struct Callbacks{void addFunction(void(*)(void*),void*){}void deleteFunction(void(*)(void*),void*){}}gIdleCallbacks;
struct Dir{std::string getCAFile()const{return {};}}directory;Dir* gDirUtilp=&directory;
namespace fs_klipy {
bool keyValid(const std::string& s){return !s.empty();}
std::string requestURL(const Request& r){return "https://api.klipy.com/"+r.query;}
std::string shareURL(const std::string&,const std::string&){return "share";}
std::string shareBody(const std::string&,const std::string&){return "{}";}
std::string reportURL(const std::string&,const std::string&){return "report";}
std::string reportBody(const std::string&,const std::string&){return "{}";}
Result success(){Result r;r.items.push_back({"result","","","",true});return r;}
Result parse(const fs_chat_media::Response&,Kind){return success();}
}
namespace fs_chat_media {Response klipyRequest(const std::string&,const std::string&,const std::string&,const std::atomic<bool>&){return {};}}
'''

CONTROLLER = r'''
Config saved(){return {true,"test-app-key"};}
std::string customer(){return "opaque-client-id";}
'''

CASES = r'''
std::pair<std::unique_ptr<Job>,std::promise<fs_klipy::Result>> held(Client& c,unsigned generation=0)
{
 auto job=std::make_unique<Job>();job->owner=c.owner;job->generation=generation;job->page=1;
 std::promise<fs_klipy::Result> result;job->work=result.get_future();return {std::move(job),std::move(result)};
}
int main()
{
 {Client c;c.generation=2;auto [job,finish]=held(c,1);c.jobs.push_back(std::move(job));
  finish.set_value(fs_klipy::success());c.poll();assert(c.state.items.empty());} // Old search.
 {Client c;auto [job,finish]=held(c);c.jobs.push_back(std::move(job));
  gAgent.login=LLUUID("new-login");c.poll();assert(*c.jobs[0]->cancelled);
  finish.set_value(fs_klipy::success());c.poll();assert(c.state.items.empty());gAgent.login=LLUUID("login");}
 {Client c;auto [a,doneA]=held(c);auto [b,doneB]=held(c);c.jobs.push_back(std::move(a));c.jobs.push_back(std::move(b));
  c.cancel(false);fs_klipy::Request request;request.query="latest";c.intent=Intent{request,c.generation,false};
  c.state.loading=true;c.poll();assert(c.jobs.size()==2&&c.intent); // Cancelling doesn't free transport slots.
  doneA.set_value(fs_klipy::success());doneB.set_value(fs_klipy::success());c.poll();
  assert(c.jobs.size()==1&&!c.intent&&c.state.items.empty());c.jobs[0]->work.wait();c.poll();
  assert(c.state.items.size()==1&&!c.state.loading&&c.state.page==1);}
 {Client c;c.state.items=fs_klipy::success().items;c.state.page=1;c.state.more=true;
  auto [job,finish]=held(c);job->append=true;job->page=2;c.jobs.push_back(std::move(job));
  fs_klipy::Result failure;failure.error="rate limit";failure.retryAfter=120;finish.set_value(failure);c.poll();
  assert(c.state.items.size()==1&&c.state.page==1&&c.state.more&&!c.state.error.empty());
  assert(c.backoff>Clock::now()+std::chrono::seconds(119));
  fs_klipy::Request request;c.intent=Intent{request,c.generation,true};c.poll();assert(c.jobs.empty()&&c.intent);c.intent.reset();
  auto [retry,done]=held(c);retry->append=true;retry->page=2;c.jobs.push_back(std::move(retry));done.set_value(fs_klipy::success());c.poll();
  assert(c.state.page==2&&c.state.items.size()==2&&c.state.error.empty());}
 {Client c;c.state.items.resize(999);auto [job,finish]=held(c);job->append=true;c.jobs.push_back(std::move(job));
  auto r=fs_klipy::success();r.items.resize(24);finish.set_value(r);c.poll();assert(c.state.items.size()==999&&!c.state.more&&!c.state.error.empty());}
 {Client c;c.state.items=fs_klipy::success().items;auto [job,finish]=held(c);c.jobs.push_back(std::move(job));
  c.config.enabled=false;c.poll();assert(*c.jobs[0]->cancelled&&c.state.items.empty());
  auto revision=c.state.revision;c.poll();assert(c.state.revision==revision); // No endless UI rebuild while disabled.
  finish.set_value(fs_klipy::success());c.poll();assert(c.state.items.empty());}
 {Client c;c.state.error="old error";c.state.page=5;c.cancel(false);assert(c.state.error.empty()&&c.state.page==0);}
 {Client c;LLProxy::getInstance()->enabled=true;assert(!c.enabled());LLProxy::getInstance()->enabled=false;
  LLGridManager::getInstance()->grid="other-grid";assert(!c.owner.valid());LLGridManager::getInstance()->grid="grid";}
}
'''

TRACKING_STUBS = r'''
struct LLView;
template<class T>struct LLHandle{T* value=nullptr;LLHandle()=default;explicit LLHandle(T* v):value(v){}T* get()const{return value;}};
struct LLView{virtual ~LLView()=default;LLHandle<LLView> getHandle(){return LLHandle<LLView>(this);}};
struct LLChatEntry:LLView{std::string text;std::string getText()const{return text;}};
struct Value{std::string value;LLUUID asUUID()const{return LLUUID(value);}std::string asString()const{return value;}};
struct LLSD{std::map<std::string,Value> fields;const Value& operator[](const char* key)const{return fields.at(key);}};
struct Binding{LLChatEntry* editor=nullptr;LLUUID conversation{"private"};LLChatEntry* input()const{return editor;}};
LLHandle<LLView> sendingEditor;
struct GifSelection{LLSD target;std::string url,slug,query;};
std::vector<GifSelection> gifSelections;
std::map<std::string,std::shared_ptr<Binding>> bindings;
bool restricted=false;
std::shared_ptr<Binding> gifTarget(const LLSD& target)
{if(restricted||target["login"].asUUID()==LLUUID("old-login"))return {};return bindings[target["editor"].asString()];}
namespace FSKlipy{
unsigned shares=0;
void acceptedShare(const std::string&,const std::string&,const LLUUID&,const LLUUID&,const std::string&){++shares;}
}
namespace FSChatUpload{
class SendScope{LLHandle<LLView> mPrevious;public:explicit SendScope(LLChatEntry*);~SendScope();};
void acceptedNearby(const std::string&);void acceptedIM(const LLUUID&,const std::string&);
}
GifSelection selection(const char* editor="im",const char* login="login")
{GifSelection s;s.target.fields={{"editor",{editor}},{"login",{login}},{"account",{"account"}},{"grid",{"grid"}}};
 s.url="https://static.klipy.com/cat.gif?ID=Exact";s.slug="cat";return s;}
'''

TRACKING_CASES = r'''
int main()
{
 LLChatEntry im,nearby;auto b=std::make_shared<Binding>();b->editor=&im;bindings["im"]=b;
 auto n=std::make_shared<Binding>();n->editor=&nearby;n->conversation=LLUUID::null;bindings["nearby"]=n;
 auto s=selection();im.text=s.url;gifSelections={s};
 FSChatUpload::acceptedIM(LLUUID("private"),s.url);assert(FSKlipy::shares==0&&gifSelections.size()==1); // Programmatic send.
 {FSChatUpload::SendScope outer(&nearby);FSChatUpload::acceptedNearby(s.url);assert(FSKlipy::shares==0);
  {FSChatUpload::SendScope inner(&im);FSChatUpload::acceptedIM(LLUUID("other-destination"),s.url);assert(FSKlipy::shares==0);}
  assert(sendingEditor.get()==&nearby);}
 {FSChatUpload::SendScope scope(&im);FSChatUpload::acceptedIM(LLUUID("private"),s.url);FSChatUpload::acceptedIM(LLUUID("private"),s.url);}
 assert(FSKlipy::shares==1&&gifSelections.empty()&&sendingEditor.get()==nullptr);
 for(int failure=0;failure<4;++failure){gifSelections={failure==3?selection("im","old-login"):s};im.text=failure==0?"deleted":s.url;restricted=failure==2;
  {FSChatUpload::SendScope scope(&im);FSChatUpload::acceptedIM(LLUUID("private"),failure==1?"filtered":s.url);}
  assert(FSKlipy::shares==1&&gifSelections.empty());restricted=false;}
 assert(!containsGif(s.url+"suffix",s.url)&&!containsGif("https://other/"+s.url,s.url));
 assert(containsGif("caption ("+s.url+")",s.url));
 gifSelections={selection("nearby")};nearby.text=s.url;
 {FSChatUpload::SendScope scope(&nearby);FSChatUpload::acceptedNearby(s.url);}
 assert(FSKlipy::shares==2&&gifSelections.empty());
}
'''


class KlipyLifecycle(unittest.TestCase):
    def test_production_client(self):
        text = (ROOT / "fsklipyclient.cpp").read_text()
        snapshot = (ROOT / "fsklipyclient.h").read_text()
        snapshot = snapshot[snapshot.index("struct Snapshot\n"):snapshot.index("const Snapshot& snapshot()")]
        owner = text[text.index("struct Scope\n"):text.index("Config saved()")]
        body = text[text.index("struct Intent"):text.index("Client* active=nullptr;")]
        run(COMMON + "using Clock=std::chrono::steady_clock;\nnamespace FSKlipy{\n" + snapshot + "}\n" + owner + CONTROLLER + body + CASES)

    def test_accepted_send_only(self):
        text = (ROOT / "fschatupload.cpp").read_text()
        body = text[text.index("FSChatUpload::SendScope::SendScope"):text.index("bool FSChatUpload::blocksSend")]
        run(COMMON + TRACKING_STUBS + body + TRACKING_CASES)


if __name__ == "__main__":
    unittest.main()
