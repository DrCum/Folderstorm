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
struct LLButton:LLView{void setOrigin(int,int){}void setToggleState(bool){}void setVisible(bool){}void reshape(int,int){}void setLabel(const std::string&){}void setEnabled(bool){}void setToolTip(const std::string&){} };
struct LLStringOps{static bool isSpace(wchar_t c){return iswspace(c)!=0;}};
std::string LLStringExplicit(const std::string& v){return v;}
struct Handle{LLView* pointer=nullptr;LLView* get()const{return pointer;}};
struct Editor:LLView{
 std::wstring text;int cursor=0;std::string selection;
 std::string getSelectionString()const{return selection;}
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
 std::string id="binding";Handle button;S32 rightInset=0;bool dragOver=false;std::string error;LLUUID conversation{"private-im"};unsigned generation=0;bool allowed=true;Editor* editor=nullptr;std::string target="private-im";
 Editor* input()const{return editor;}bool currentAuthority()const;
 bool valid()const{return allowed&&currentAuthority();}std::string stamp()const{return target;}
};
void layoutAttachment(const std::shared_ptr<Binding>&){}
struct Dir{std::string getCAFile(){return {};}}dir;Dir* gDirUtilp=&dir;
struct Callbacks{void addFunction(void(*)(void*),void*){}}gIdleCallbacks;
struct FSSessionWorker{static bool inputAllowed(){return true;}};
'''
BOUNDARIES = r'''
std::shared_future<void> prepareGate,uploadGate;
Work prepare(std::shared_ptr<std::vector<unsigned char>> bytes,const std::string&,bool dib){
 if(prepareGate.valid())prepareGate.wait();
 if(!bytes||dib)bytes=std::make_shared<std::vector<unsigned char>>(MaxFileBytes);
 Work result;result.bytes=std::move(bytes);result.image=std::make_shared<fs_chat_media::Image>();return result;
}
namespace fs_chat_upload {Reply upload(const Config&,const std::vector<unsigned char>&,const std::string&,const std::string&,const std::string&,Progress& progress){
 assert(uploadGate.valid());progress.submitted=true;uploadGate.wait();Reply reply;reply.state=State::Complete;return reply;
}}
namespace fs_chat_media {struct Response{int status=0;std::string type;};Response fetch(const std::string&,const std::string&,int,const std::atomic<bool>&,bool){return {};}}
'''
ADMISSION_BOUNDARIES = r'''
Controller* active=nullptr;
Controller& controller(){assert(active);return *active;}
void show(const std::shared_ptr<Binding>&,bool=true){}
struct FSChatUpload{static void stageBytes(LLChatEntry*,std::vector<unsigned char>);};
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
 {Editor editor;editor.text=L"caption ";editor.cursor=8;Controller c;auto first=jobFor(editor),second=jobFor(editor);second->binding=first->binding;c.jobs={first,second};
  std::promise<Work> a,b;first->work=a.get_future();second->work=b.get_future();a.set_value(success());c.poll();assert(first->inserted&&!second->inserted);
  auto result=success();result.reply.url="https://images.sodie.online/raw/two.png";b.set_value(std::move(result));c.poll();
  assert(second->inserted&&editor.getText()=="caption https://images.sodie.online/raw/test.png https://images.sodie.online/raw/two.png");}
 {Editor editor;editor.text=L"caption";editor.cursor=7;Controller c;auto job=jobFor(editor);job->reply=success().reply;job->state=State::Complete;editor.cursor=0;assert(!c.insert(job,false)&&editor.text==L"caption");}
 {Editor editor;editor.text=L"caption";editor.cursor=7;Controller c;auto job=jobFor(editor);job->reply=success().reply;job->state=State::Complete;editor.selection="caption";assert(!c.insert(job,false)&&editor.text==L"caption");}

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
 {Editor editor;Controller c;active=&c;auto binding=jobFor(editor)->binding;c.bindings={binding};
  for(int i=0;i<3;++i)FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(9*1024*1024));
  assert(c.jobs.size()==3);
  for(int poll=0;poll<100;++poll){c.poll();for(const auto& job:c.jobs)if(job->work.valid())job->work.wait();}
  for(const auto& job:c.jobs)assert(!job->work.valid()&&job->state==State::Review);
 }
 {Editor editor;editor.text=L"caption ";editor.cursor=8;Controller c;auto first=jobFor(editor),second=jobFor(editor);
  second->binding=first->binding;first->state=State::Processing;first->reply=success().reply;
  first->checkAfter=std::chrono::steady_clock::time_point::max();c.jobs={first,second};
  std::promise<Work> b;second->work=b.get_future();auto result=success();result.reply.url="https://images.sodie.online/raw/two.png";
  b.set_value(std::move(result));c.poll();assert(!first->inserted&&!second->inserted&&editor.text==L"caption ");
  std::promise<Work> a;first->checkWork=true;first->work=a.get_future();a.set_value(success());c.poll();
  assert(first->inserted&&second->inserted&&editor.getText()=="caption https://images.sodie.online/raw/test.png https://images.sodie.online/raw/two.png");
 }
 {Editor editor;Controller c;active=&c;auto binding=jobFor(editor)->binding;c.bindings={binding};
  for(int i=0;i<3;++i)FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(9*1024*1024));
  std::promise<void> release;prepareGate=release.get_future().share();c.poll();auto first=c.jobs.front();
  assert(first->work.valid()&&!first->bytes&&c.retainedBytes()==27*1024*1024);
  FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(6*1024*1024));assert(c.jobs.size()==3&&binding->error=="FSChatUploadTooMany");
  FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(5*1024*1024));assert(c.jobs.size()==4&&c.retainedBytes()==MaxStagedBytes);
  c.cancel(first);assert(c.retainedBytes()==MaxStagedBytes);release.set_value();first->work.wait();c.poll();
  assert(first->workBytes==0&&c.retainedBytes()==23*1024*1024);
  for(int poll=0;poll<10;++poll){c.poll();for(const auto& job:c.jobs)if(job->work.valid())job->work.wait();}prepareGate={};
 }
 for(bool dib:{false,true}){Editor editor;Controller c;active=&c;auto binding=jobFor(editor)->binding;c.bindings={binding};
  for(int i=0;i<2;++i){FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(MaxFileBytes));c.jobs.back()->state=State::Review;}
  auto job=newJob(&editor);assert(job);job->path="test.png";job->dib=dib;if(dib)job->bytes=std::make_shared<std::vector<unsigned char>>(1024);
  std::promise<void> release;prepareGate=release.get_future().share();c.poll();
  assert(job->work.valid()&&job->workBytes==MaxFileBytes&&c.retainedBytes()==30*1024*1024);
  FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(3*1024*1024));assert(c.jobs.size()==3&&binding->error=="FSChatUploadTooMany");
  c.cancel(job);assert(c.retainedBytes()==30*1024*1024);release.set_value();job->work.wait();c.poll();
  assert(job->workBytes==0&&c.retainedBytes()==20*1024*1024);prepareGate={};
 }
 {Editor editor;Controller c;active=&c;auto binding=jobFor(editor)->binding;c.bindings={binding};
  for(int i=0;i<3;++i){FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(MaxFileBytes));c.jobs.back()->state=State::Review;}
  auto first=c.jobs.front();first->state=State::Queued;std::promise<void> release;uploadGate=release.get_future().share();c.poll();
  assert(first->work.valid()&&c.retainedBytes()==30*1024*1024);c.cancel(first);assert(!first->bytes&&c.retainedBytes()==30*1024*1024);
  FSChatUpload::stageBytes(&editor,std::vector<unsigned char>(3*1024*1024));assert(c.jobs.size()==3&&binding->error=="FSChatUploadTooMany");
  release.set_value();first->work.wait();c.poll();assert(c.retainedBytes()==20*1024*1024);uploadGate={};
 }
 {Editor editor,otherEditor;Controller c;auto first=jobFor(editor),second=jobFor(editor),other=jobFor(otherEditor);second->binding=first->binding;
  first->state=State::Processing;first->checkAfter=std::chrono::steady_clock::time_point::max();c.jobs={first,other,second};
  second->state=other->state=State::Complete;second->reply=other->reply=success().reply;c.poll();
  assert(!second->inserted&&other->inserted);c.cancel(first);c.poll();assert(second->inserted);
 }
 {Editor editor;Controller c;auto first=jobFor(editor),second=jobFor(editor);second->binding=first->binding;c.jobs={first,second};
  first->state=State::Processing;first->checkAfter=std::chrono::steady_clock::time_point::max();second->state=State::Complete;second->reply=success().reply;
  editor.text=L"edited caption";editor.cursor=14;c.poll();assert(!second->inserted);first->state=State::Complete;first->reply=success().reply;c.poll();
  assert(!first->inserted&&!second->inserted&&editor.text==L"edited caption");
 }
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
preamble+='\nvoid saveGifLink(const std::string&) {}\n'
admission=source[source.index('std::shared_ptr<Job> newJob('):source.index('class Thumbnail final')]
stage=source[source.index('void FSChatUpload::stageBytes('):source.index('void FSChatUpload::pasteImage(')]
with tempfile.TemporaryDirectory(prefix='fs-upload-controller-') as temp:
    path=Path(temp)/'test.cpp';path.write_text(STUBS+authority+preamble+BOUNDARIES+controller+ADMISSION_BOUNDARIES+admission+stage+TESTS)
    exe=Path(temp)/'test'
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-Werror','-pthread','-I',str(ROOT),str(path),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('Production upload controller: 22 lifecycle/draft/participant/capacity/order scenarios passed')
