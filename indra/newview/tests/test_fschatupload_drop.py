"""Exercise production native file-drop hit testing and source tracking."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include "fschatuploadpolicy.h"
#include <cassert>
#include <memory>
#include <algorithm>
using S32=int;
using namespace fs_chat_upload;
namespace ll_clipboard_image {constexpr unsigned MaxFiles=4;}
struct LLUUID {std::string value;LLUUID()=default;LLUUID(const char* s):value(s){}bool operator!=(const LLUUID& b)const{return value!=b.value;}};
struct Agent {LLUUID account{"account"},login{"login"};LLUUID getID()const{return account;}LLUUID getSessionID()const{return login;}}gAgent;
bool gDisconnected=false;constexpr int STATE_STARTED=1;
struct LLStartUp{static int getStartupState(){return STATE_STARTED;}};
struct LLGridManager{std::string grid="grid";static LLGridManager* getInstance(){static LLGridManager v;return &v;}std::string getGrid(){return grid;}};
struct FSSessionWorker{static bool inputAllowed(){return true;}};
struct LLWindowCallbacks{enum DragNDropAction{DNDA_START_TRACKING,DNDA_TRACK,DNDA_STOP_TRACKING,DNDA_DROPPED};};
struct Rect{int mLeft=0,mBottom=0,width=200,height=200;};
struct LLView{
 virtual ~LLView()=default;bool visible=true,opaque=false;Rect rect;LLView* parent=nullptr;std::vector<LLView*> children;
 bool getVisible()const{return visible;}bool pointInView(int x,int y)const{return x>=0&&y>=0&&x<rect.width&&y<rect.height;}
 bool getMouseOpaque()const{return opaque;}const std::vector<LLView*>* getChildList()const{return &children;}Rect getRect()const{return rect;}
 LLView* getParent()const{return parent;}bool isInVisibleChain()const{return visible&&(!parent||parent->isInVisibleChain());}
 template<class T>T* getParentByType(){for(auto* at=parent;at;at=at->parent)if(auto* p=dynamic_cast<T*>(at))return p;return nullptr;}
 void add(LLView& child){children.insert(children.begin(),&child);child.parent=this;}
};
struct LLFloater:LLView{};
struct Editor:LLView{};
struct Handle{LLView* view=nullptr;LLView* get()const{return view;}};
struct Binding{Editor* editor=nullptr;Handle button;bool allowed=true,dragOver=false;unsigned generation=0;std::string target="IM";Editor* input(){return editor;}bool valid()const{return allowed;}std::string stamp()const{return target;}};
struct Controller{Config config;std::vector<std::shared_ptr<Binding>> bindings;bool connection(){return true;}}host;
Controller& controller(){return host;}
struct LLUI{LLView* root=nullptr;static LLUI* getInstance(){static LLUI v;return &v;}LLView* getRootView(){return root;}};
unsigned submissions=0;Editor* submittedTo=nullptr;
namespace FSChatUpload{
 void stageFiles(Editor* editor,const std::vector<std::string>& files){assert(files.size()==1);++submissions;submittedTo=editor;}
 bool fileDrop(S32,S32,LLWindowCallbacks::DragNDropAction,const std::vector<std::string>&);
}
'''
CASES = r'''
int main(){
 LLView root;root.rect={0,0,500,400};LLFloater first,second;second.rect.mLeft=220;Editor a,b;a.opaque=b.opaque=true;first.add(a);second.add(b);root.add(first);root.add(second);LLUI::getInstance()->root=&root;
 auto x=std::make_shared<Binding>();x->editor=&a;auto y=std::make_shared<Binding>();y->editor=&b;host.bindings={x,y};
 const std::vector<std::string> files{"/tmp/image.png"};
 const auto call=[&](int px,int py,LLWindowCallbacks::DragNDropAction action){return FSChatUpload::fileDrop(px,py,action,files);};
 const auto reset=[&]{call(0,0,LLWindowCallbacks::DNDA_STOP_TRACKING);submissions=0;submittedTo=nullptr;};
 reset();assert(!call(450,350,LLWindowCallbacks::DNDA_START_TRACKING));assert(call(20,20,LLWindowCallbacks::DNDA_TRACK));assert(call(20,20,LLWindowCallbacks::DNDA_DROPPED));assert(submissions==1&&submittedTo==&a);
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));x->generation++;assert(!call(20,20,LLWindowCallbacks::DNDA_DROPPED)&&submissions==0);
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));gAgent.login=LLUUID("replacement");assert(!call(20,20,LLWindowCallbacks::DNDA_DROPPED)&&submissions==0);gAgent.login=LLUUID("login");
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));LLGridManager::getInstance()->grid="another";assert(!call(20,20,LLWindowCallbacks::DNDA_DROPPED));LLGridManager::getInstance()->grid="grid";
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));host.config.folder="other-folder";assert(!call(20,20,LLWindowCallbacks::DNDA_DROPPED));host.config=Config{};
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));x->target="Nearby";assert(!call(20,20,LLWindowCallbacks::DNDA_DROPPED)&&submissions==0);x->target="IM";
 reset();LLView overlay;overlay.opaque=true;root.add(overlay);assert(!call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));root.children.erase(root.children.begin());
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));assert(call(240,20,LLWindowCallbacks::DNDA_TRACK));assert(call(240,20,LLWindowCallbacks::DNDA_DROPPED)&&submittedTo==&b);
 reset();assert(!FSChatUpload::fileDrop(20,20,LLWindowCallbacks::DNDA_START_TRACKING,{"/tmp/image.png.exe"}));assert(!FSChatUpload::fileDrop(20,20,LLWindowCallbacks::DNDA_START_TRACKING,{"\\\\server\\share\\image.png"}));
 reset();gDisconnected=true;assert(!call(20,20,LLWindowCallbacks::DNDA_START_TRACKING));gDisconnected=false;
 reset();assert(call(20,20,LLWindowCallbacks::DNDA_START_TRACKING)&&x->dragOver);reset();assert(!x->dragOver&&!y->dragOver);
}
'''
source = (ROOT / 'fschatupload.cpp').read_text()
start = source.index('namespace\n{\nstruct DragBinding')
end = source.index('void FSChatUpload::preferencesOpening()', start)
with tempfile.TemporaryDirectory(prefix='fs-chat-file-drop-') as directory:
    cpp = Path(directory) / 'test.cpp'
    exe = Path(directory) / 'test'
    cpp.write_text(STUBS + source[start:end] + CASES)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT), str(cpp), '-o', str(exe)], check=True, timeout=45)
    subprocess.run([str(exe)], check=True, timeout=20)
print('Production file-drop routing: 11 destination/lifecycle scenarios passed')
