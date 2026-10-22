"""Production upload registration/layout against narrow, expanding native composers."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + '\n'


STUBS = r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using S32=int;
struct LLUUID{std::string value;LLUUID()=default;LLUUID(const char* v):value(v){}
 bool operator!=(const LLUUID& b)const{return value!=b.value;}static const LLUUID null;};
const LLUUID LLUUID::null;
struct LLSD{};
struct LLRect{int mLeft=0,mTop=20,mRight=32,mBottom=0;
 LLRect()=default;LLRect(int a,int b,int c,int d):mLeft(a),mTop(b),mRight(c),mBottom(d){}
 int getWidth()const{return mRight-mLeft;}int getHeight()const{return mTop-mBottom;}};
struct LLView;
struct Handle{LLView* value=nullptr;LLView* get()const{return value;}};
struct LLView{LLRect rect;bool visible=true;virtual ~LLView()=default;Handle getHandle(){return {this};}
 const LLRect& getRect()const{return rect;}void reshape(int w,int h){rect.mRight=rect.mLeft+w;rect.mTop=rect.mBottom+h;}};
struct Parent:LLView{std::vector<std::unique_ptr<LLView>> children;void addChild(LLView* p){children.emplace_back(p);}};
struct LLChatEntry:LLView{Parent parent;std::function<bool()> available;std::function<void()> paste;
 Parent* getParent(){return &parent;}bool getVisible()const{return visible;}
 void setImagePasteCallbacks(std::function<bool()> a,std::function<void()> p){available=std::move(a);paste=std::move(p);}};
struct LLButton:LLView{
 struct Params{std::string name,tool_tip,image_overlay,label;LLRect rect;struct{int flags=0;}follows;};
 explicit LLButton(const Params& p){rect=p.rect;}void setVisible(bool v){visible=v;}
 void setOrigin(int x,int y){auto w=rect.getWidth(),h=rect.getHeight();rect=LLRect(x,y+h,x+w,y);}
 template<class F>void setCommitCallback(F){}
};
struct LLUICtrl{};
struct LLUICtrlFactory{template<class T>static T* create(const typename T::Params& p){return new T(p);}};
struct LLUI{static std::string getUIImage(const char* p){return p;}};
constexpr int FOLLOWS_RIGHT=1,FOLLOWS_BOTTOM=2,STATE_STARTED=3;
struct LLStartUp{static int getStartupState(){return STATE_STARTED;}};
struct FSSessionWorker{static bool inputAllowed(){return true;}};
bool gDisconnected=false;
struct Clipboard{bool isClipboardImageAvailable(){return true;}};
struct Viewer{Clipboard clipboard;Clipboard* getWindow(){return &clipboard;}}viewer;
Viewer* gViewerWindow=&viewer;
std::string tr(const char* p){return p;}
struct Binding{Handle editor,button,gifButton;S32 rightInset=0;bool layoutReady=false;LLUUID conversation;unsigned generation=0;
 std::function<bool()> available;std::function<std::string()> destination;
 LLChatEntry* input()const{return dynamic_cast<LLChatEntry*>(editor.get());}
 bool valid()const{return input()!=nullptr;}};
struct Config{bool enabled=true;bool valid()const{return true;}};
struct Controller{Config config;std::vector<std::shared_ptr<Binding>> bindings;
 auto find(LLChatEntry* e){for(auto& b:bindings)if(b->input()==e)return b;return std::shared_ptr<Binding>{};}
 bool connection()const{return true;}}host;
Controller& controller(){return host;}
void show(const std::shared_ptr<Binding>&){}
int pastes=0;LLChatEntry* pastedInto=nullptr;
struct FSFloaterGifBoard{static void open(const LLSD&){} };
struct FSChatUpload{static LLSD captureTarget(LLChatEntry*){return {};}static void bind(LLChatEntry*,const LLUUID&,std::function<bool()>,std::function<std::string()>);
 static void pasteImage(LLChatEntry* p){++pastes;pastedInto=p;}};
'''
CASES = r'''
int main(){
 // Both Conversations composers are born inside auto-sized layout panels.
 for(const auto& destination:{LLUUID::null,LLUUID("private-im")}){
  host.bindings.clear();LLChatEntry editor;editor.rect=LLRect(4,20,28,0);editor.parent.rect=LLRect(0,24,32,0);
  FSChatUpload::bind(&editor,destination,{},{});
  assert(host.bindings.size()==1&&editor.available&&editor.available()&&editor.paste);
  auto binding=host.bindings[0];auto* button=dynamic_cast<LLButton*>(binding->button.get());assert(button&&!button->visible);
  editor.paste();assert(pastedInto==&editor); // Paste registration does not depend on final width.
  layoutAttachment(binding);assert(!binding->layoutReady&&!button->visible&&editor.rect.getWidth()==24);
  editor.parent.rect.mRight=600;editor.rect.mRight=599;layoutAttachment(binding);
  assert(binding->layoutReady&&button->visible&&binding->rightInset==1);
  const auto width=editor.rect.getWidth();assert(button->rect.mLeft==editor.rect.mRight+2&&button->rect.mRight==565);
  for(int i=0;i<20;++i){layoutAttachment(binding);}assert(editor.rect.getWidth()==width); // Reserve once, never shrink on polling.
  editor.parent.rect.mRight=1000;editor.rect.mRight+=400;layoutAttachment(binding);assert(button->rect.mRight==965);
  editor.parent.rect.mRight=50;layoutAttachment(binding);assert(!button->visible&&editor.available());
  editor.parent.rect.mRight=600;editor.rect.mRight=599;layoutAttachment(binding);assert(button->visible&&editor.rect.getWidth()==width);
  const auto generation=binding->generation;
  FSChatUpload::bind(&editor,LLUUID("replacement-im"),{},{});
  assert(host.bindings.size()==1&&editor.parent.children.size()==2&&binding->generation==generation+1);
  editor.paste();assert(pastedInto==&editor);
  host.config.enabled=false;assert(!editor.available());host.config.enabled=true;
 }
 assert(pastes==4);
}
'''


class ComposerTests(unittest.TestCase):
    def test_narrow_nearby_and_private_composers(self):
        text = (ROOT / 'fschatupload.cpp').read_text()
        source = STUBS + function(text, 'void layoutAttachment(') + function(text, 'void FSChatUpload::bind(') + CASES
        with tempfile.TemporaryDirectory(prefix='fs-upload-composers-') as temporary:
            cpp = Path(temporary) / 'test.cpp'
            cpp.write_text(source)
            exe = cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
