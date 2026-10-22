"""Compile the production upload Job/Controller with only viewer/transport boundaries stubbed."""
from pathlib import Path
import os, subprocess, tempfile
ROOT=Path(__file__).resolve().parents[1]
STUBS = r'''
#include "fschatuploadpolicy.h"
#include "fschatmediacodec.h"
#include <cassert>
#include <future>
#include <functional>
#include <chrono>
#include <string>
#include <cwctype>
using namespace fs_chat_upload;
using S32=int;
using LLWString=std::wstring;
struct LLUUID {std::string value; LLUUID()=default; LLUUID(const char* s):value(s){} std::string asString()const{return value;} bool operator==(const LLUUID& b)const{return value==b.value;} bool operator!=(const LLUUID& b)const{return !(*this==b);} bool isNull()const{return value.empty();} static LLUUID generateNewID(){return LLUUID("generated-id");}};
struct Agent {LLUUID account{"account"},login{"login"};LLUUID getID()const{return account;}LLUUID getSessionID()const{return login;}bool isInGroup(const LLUUID&)const{return true;}}gAgent;
struct Speaker {LLUUID mID;};
struct LLSpeakerMgr {
 using speaker_list_t=std::vector<std::shared_ptr<Speaker>>;speaker_list_t current;
 void getSpeakerList(speaker_list_t* out,bool includeText){assert(includeText);*out=current;}
};
struct Session {
 bool mTextIMPossible=true;int mType=0;LLUUID mOtherParticipantID{"contact"};
 std::vector<LLUUID> mInitialTargetIDs{LLUUID("contact")};LLSpeakerMgr* mSpeakers=nullptr;bool conference=false;
 bool isGroupSessionType()const{return false;}bool isP2PSessionType()const{return !conference;}
};
struct LLIMModel {Session session;static LLIMModel* getInstance(){static LLIMModel v;return &v;}Session* findIMSession(const LLUUID&){return &session;}};
struct FSData {static FSData& instance(){static FSData v;return v;}bool canSendToIMContact(int,const LLUUID&,const LLUUID&)const{return true;}};
constexpr int RLV_BHVR_SENDCHAT=1,RLV_BHVR_REDIRCHAT=2;
struct RlvHandler {bool hasBehaviour(int)const{return false;}}gRlvHandler;
struct RlvActions {static bool canSendIM(const LLUUID& id){return id.value!="restricted"&&id.value!="account";}};
struct LLApp {static bool isExiting(){return false;}};
bool gDisconnected=false;
constexpr int STATE_STARTED=1;
struct LLStartUp{static int getStartupState(){return STATE_STARTED;}};
struct LLGridManager{static LLGridManager* getInstance(){static LLGridManager v;return &v;}std::string getGrid(){return "grid";}};
struct LLProxy{static bool instanceExists(){return false;}static LLProxy* getInstance(){static LLProxy v;return &v;}bool isHTTPProxyEnabled(){return false;}};
struct LLView{virtual ~LLView()=default;};
struct Rect{int mRight=300,mBottom=0,mLeft=0;int getHeight()const{return 20;}int getWidth()const{return 300;}};
struct LLButton:LLView{void setOrigin(int,int){}void setVisible(bool){}void reshape(int,int){}void setLabel(const std::string&){}void setEnabled(bool){}void setToolTip(const std::string&){} };
std::string LLStringExplicit(const std::string& v){return v;}
struct Handle{LLView* pointer=nullptr;LLView* get()const{return pointer;}};
struct Editor:LLView{
 std::wstring text;int cursor=0;
 bool getEnabled()const{return true;}bool getReadOnly()const{return false;}
 std::string getText()const{return std::string(text.begin(),text.end());}
 const std::wstring& getWText()const{return text;}
 int getMaxTextLength()const{return 1023;}
 int getCursorPos()const{return cursor;}
 void setCursorPos(int value){cursor=value;}
 void deselect(){}
 void insertText(const std::string& value){text.insert(static_cast<std::size_t>(cursor),std::wstring(value.begin(),value.end()));cursor+=static_cast<int>(value.size());}
 struct Parent{Rect getRect()const{return {};}};Parent* getParent()const{return nullptr;}bool getVisible()const{return true;}void reshape(int,int){}
 Rect getRect()const{return {};}
};
using LLChatEntry=Editor;
std::wstring utf8str_to_wstring(const std::string& v){return {v.begin(),v.end()};}
int wstring_utf8_length(const std::wstring& v){return static_cast<int>(v.size());}
std::string tr(const char* key){return key;}
Config savedConfig(){return {};}
struct Binding{
 std::function<bool()> available;
 std::string id="binding";Handle button;S32 rightInset=0;LLUUID conversation{"private-im"};unsigned generation=0;bool allowed=true;Editor* editor=nullptr;std::string target="private-im";
 Editor* input()const{return editor;}bool currentAuthority()const;
 bool valid()const{return allowed&&currentAuthority();}std::string stamp()const{return target;}
};
struct Dir{std::string getCAFile(){return {};}}dir;Dir* gDirUtilp=&dir;
struct Callbacks{void addFunction(void(*)(void*),void*){}}gIdleCallbacks;
'''
BOUNDARIES = r'''
Work prepare(std::shared_ptr<std::vector<unsigned char>>,const std::string&){return {};}
namespace fs_chat_upload {Reply upload(const Config&,const std::vector<unsigned char>&,const std::string&,const std::string&,const std::string&,Progress&){assert(false);return {};}}
namespace fs_chat_media {struct Response{int status=0;std::string type;};Response fetch(const std::string&,const std::string&,int,const std::atomic<bool>&,bool){return {};}}
'''
TESTS = r'''
std::shared_ptr<Job> jobFor(Editor& editor){
 auto job=std::make_shared<Job>();job->binding=std::make_shared<Binding>();job->binding->editor=&editor;
 job->account=gAgent.account;job->login=gAgent.login;job->grid="grid";job->stamp="private-im";
 job->draft=editor.getText();job->cursor=editor.cursor;job->state=State::Uploading;job->uploadWork=true;
 return job;
}
Work success(){Work result;result.reply.state=State::Complete;result.reply.url="https://images.sodie.online/raw/test.png";result.reply.id="confirmed-id";result.reply.error.clear();return result;}
int main(){
 {Editor editor;editor.text=L"caption ";editor.cursor=8;Controller c;auto job=jobFor(editor);c.jobs.push_back(job);std::promise<Work> result;job->work=result.get_future();
  c.cancel(job);c.poll();assert(job->work.valid()&&editor.text==L"caption ");result.set_value(success());c.poll();assert(job->state==State::Cancelled&&job->reply.id=="confirmed-id"&&!job->inserted);}
 {Editor editor;Controller c;auto job=jobFor(editor);c.jobs.push_back(job);std::promise<Work> result;job->work=result.get_future();
  gAgent.login=LLUUID("other-login");c.poll();assert(job->invalidated&&job->progress->cancelled);result.set_value(success());c.poll();assert(editor.text.empty()&&job->reply.id=="confirmed-id");gAgent.login=LLUUID("login");}
 {Editor editor;editor.text=L"caption ";editor.cursor=8;Controller c;auto job=jobFor(editor);c.jobs.push_back(job);std::promise<Work> result;job->work=result.get_future();result.set_value(success());c.poll();
  assert(job->inserted&&editor.getText()=="caption https://images.sodie.online/raw/test.png");}
 {Editor editor;editor.text=L"old";editor.cursor=3;Controller c;auto job=jobFor(editor);c.jobs.push_back(job);std::promise<Work> result;job->work=result.get_future();editor.text=L"new caption ";editor.cursor=12;result.set_value(success());c.poll();
  assert(!job->inserted&&editor.text==L"new caption ");assert(c.insert(job,true));assert(editor.getText()=="new caption https://images.sodie.online/raw/test.png");}
 {Editor editor;Controller c;auto job=jobFor(editor);job->state=State::Queued;job->binding->allowed=false;c.jobs.push_back(job);c.poll();assert(job->state==State::Cancelled&&!job->progress->submitted);}
 {Editor editor;Controller c;auto job=jobFor(editor);job->state=State::Queued;c.jobs.push_back(job);job->binding->generation++;c.poll();assert(job->state==State::Cancelled);}
 {Editor editor;Controller c;auto job=jobFor(editor);job->state=State::Queued;c.jobs.push_back(job);job->binding->target="Nearby";c.poll();assert(job->state==State::Cancelled&&editor.text.empty());}
 {Editor editor;Controller c;auto job=jobFor(editor);job->state=State::Queued;c.jobs.push_back(job);c.config.folder="other-folder";c.poll();assert(job->state==State::Cancelled&&!job->progress->submitted);}
 {Editor editor;Controller c;auto job=jobFor(editor);job->reply=success().reply;job->state=State::Complete;job->binding->allowed=false;assert(!c.insert(job,true)&&editor.text.empty());}
 {Editor editor;Controller c;auto job=jobFor(editor);job->state=State::Queued;c.jobs={job};
  auto& session=LLIMModel::getInstance()->session;LLSpeakerMgr speakers;session.conference=true;session.mSpeakers=&speakers;
  speakers.current={std::make_shared<Speaker>(Speaker{LLUUID("account")}),std::make_shared<Speaker>(Speaker{LLUUID("contact")})};
  assert(job->binding->valid()); // Self is excluded, as in native Send.
  speakers.current.push_back(std::make_shared<Speaker>(Speaker{LLUUID("restricted")}));
  assert(!job->binding->valid());c.poll();assert(job->state==State::Cancelled&&!job->progress->submitted);
  speakers.current.pop_back();session.mInitialTargetIDs.push_back(LLUUID("restricted"));
  assert(job->binding->valid()); // A departed initial participant no longer blocks admission.
  session.mSpeakers=nullptr;assert(!job->binding->valid());session=Session{};}
 {Editor editor;Controller c;auto job=jobFor(editor);c.jobs={job};std::promise<Work> result;job->work=result.get_future();
  auto& session=LLIMModel::getInstance()->session;LLSpeakerMgr speakers;session.conference=true;session.mSpeakers=&speakers;
  assert(job->binding->valid());speakers.current.push_back(std::make_shared<Speaker>(Speaker{LLUUID("restricted")}));
  result.set_value(success());c.poll();assert(job->state==State::Cancelled&&job->reply.id=="confirmed-id"&&!job->inserted&&editor.text.empty());session=Session{};}
}
'''
source=(ROOT/'fschatupload.cpp').read_text()
# Use the production permission check for admission and ongoing Job validation.
authority=source[source.index('    bool valid() const\n'):source.index('    std::string stamp() const\n')]
authority=authority.replace('    bool valid() const', 'bool Binding::currentAuthority() const', 1)
body=source[source.index('struct Work\n'):source.index('Controller* active =')]
# The production Work and Job precede the stubbed asynchronous entry points.
prepare_at=body.index('Work prepare(')
controller_at=body.index('struct Controller')
preamble=body[:prepare_at]
controller=body[controller_at:]
with tempfile.TemporaryDirectory(prefix='fs-upload-controller-') as temp:
    path=Path(temp)/'test.cpp';path.write_text(STUBS+authority+preamble+BOUNDARIES+controller+TESTS)
    exe=Path(temp)/'test'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I',str(ROOT),str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Production upload controller: 11 lifecycle/draft/participant scenarios passed')
