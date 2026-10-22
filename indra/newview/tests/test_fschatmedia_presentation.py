"""Production URL-collapse and image interaction bodies; native font/GL boundaries are stubbed."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
from test_fschatupload_composers import function

ROOT = Path(__file__).resolve().parents[1]
STUBS = r'''
#include "fschatmediacodec.h"
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <set>
using S32=int;using F32=float;using MASK=int;using KEY=int;
constexpr MASK MASK_NONE=0;constexpr KEY KEY_RETURN=13;
struct LLRectf{float mLeft=0;};struct Rect{int width=320;int getWidth()const{return width;}};
struct LLView{virtual ~LLView()=default;Rect rect;const Rect& getRect()const{return rect;}LLView* getParent()const{return nullptr;}};
struct LLPanel:LLView{bool childHandled=false;void setFocus(bool){}bool childrenHandleMouseDown(int,int,int){return childHandled;}bool handleKeyHere(KEY,MASK){return false;}};
struct Focus{LLView* owner=nullptr;LLView* keyboard=nullptr;LLView* getKeyboardFocus()const{return keyboard;}void setMouseCapture(LLView* p){owner=p;}}gFocusMgr;
template<class T>struct Ptr{std::shared_ptr<T> value;Ptr()=default;Ptr(T* p):value(p){}T* get()const{return value.get();}
 T* operator->(){return value.get();}const T* operator->()const{return value.get();}bool notNull()const{return bool(value);}};
struct Style{std::string href;int color=0;bool isLink()const{return !href.empty();}const std::string& getLinkHREF()const{return href;}};
using LLStyleConstSP=Ptr<const Style>;
struct LLTextBase:LLView{std::wstring text;const auto& getWText()const{return text;}int getLength()const{return static_cast<int>(text.size());}};
struct LLTextSegment;using LLTextSegmentPtr=Ptr<LLTextSegment>;
struct LLTextSegment{
 int mStart,mEnd;LLTextSegment(int a,int b):mStart(a),mEnd(b){}virtual ~LLTextSegment()=default;
 int getStart()const{return mStart;}int getEnd()const{return mEnd;}
 static LLStyleConstSP cloneStyle(LLTextBase&,const Style* style){return new Style(*style);}
 virtual LLTextSegmentPtr clone(LLTextBase&)const{return {};}
 virtual LLStyleConstSP getStyle()const=0;virtual bool canEdit()const{return true;}
 virtual bool getDimensionsF32(S32,S32,F32&,S32&){return false;}
 virtual S32 getNumChars(S32,S32,S32,S32,S32)const{return 0;}
 virtual S32 getOffset(S32,S32,S32,bool)const{return 0;}
 virtual F32 draw(S32,S32,S32,S32,const LLRectf&){return 0;}
 virtual bool handleMouseDown(S32,S32,MASK){return true;}virtual bool handleMouseUp(S32,S32,MASK){return true;}
 virtual bool handleRightMouseDown(S32,S32,MASK){return true;}virtual bool handleHover(S32,S32,MASK){return true;}
 virtual bool handleToolTip(S32,S32,MASK){return true;}
 virtual void unlinkFromDocument(LLTextBase*){}virtual void linkToDocument(LLTextBase*){}virtual void setToolTip(const std::string&){}
};
struct LLNormalTextSegment:LLTextSegment{
 LLStyleConstSP style;bool editable=true;
 LLNormalTextSegment(LLStyleConstSP s,int a,int b,LLTextBase&):LLTextSegment(a,b),style(s){}
 LLStyleConstSP getStyle()const override{return style;}void setAllowEdit(bool e){editable=e;}bool canEdit()const override{return editable;}
 bool getDimensionsF32(S32,S32 count,F32& width,S32& height)override{width=float(count*8);height=12;return false;}
 S32 getNumChars(S32,S32 offset,S32,S32 maximum,S32)const override{return (std::min)(maximum,mEnd-mStart-offset);}
 S32 getOffset(S32,S32,S32 count,bool)const override{return count;}
 F32 draw(S32 start,S32 end,S32,S32,const LLRectf& r)override{return r.mLeft+float((end-start)*8);}
};
struct ByEnd{bool operator()(const LLTextSegmentPtr& a,const LLTextSegmentPtr& b)const{return a->getEnd()<b->getEnd();}};
struct FSChatHistory:LLTextBase{
 std::set<LLTextSegmentPtr,ByEnd> mSegments;int reflows=0;
 auto getSegIterContaining(int at){auto i=mSegments.begin();while(i!=mSegments.end()&&(*i)->getEnd()<=at)++i;return i;}
 void needsReflow(S32=0){++reflows;}
 void collapseMediaLink(S32,S32,const std::string&,const std::shared_ptr<bool>&);
};
std::string wstring_to_utf8str(const std::wstring& text){return std::string(text.begin(),text.end());}
'''
LINK_CASES = r'''
LLTextSegmentPtr segment(FSChatHistory& h,int a,int b,const std::string& href,int color=0){auto* style=new Style;style->href=href;style->color=color;return new LLNormalTextSegment(LLStyleConstSP(style),a,b,h);}
int main(){
 const std::string url="https://cdn.example.org/picture.gif?q=1";
 const std::wstring wide(url.begin(),url.end());
 FSChatHistory h;h.text=L"caption "+wide+L" tail";const auto original=h.text;
 const int begin=8,end=begin+static_cast<int>(url.size()),split=begin+static_cast<int>(url.find('?'));
 h.mSegments.insert(segment(h,0,begin,""));h.mSegments.insert(segment(h,begin,split,url,1));
 h.mSegments.insert(segment(h,split,end,url,2));h.mSegments.insert(segment(h,end,h.getLength(),""));
 auto hidden=std::make_shared<bool>(false);h.collapseMediaLink(0,h.getLength(),url,hidden);
 assert(h.mSegments.size()==4&&h.text==original);int replaced=0;
 for(auto p:h.mSegments)if(auto* link=dynamic_cast<FSChatMediaLinkSegment*>(p.get())){
  ++replaced;float width=0;int height=0;const auto count=link->getEnd()-link->getStart();
  link->getDimensionsF32(0,count,width,height);assert(width>0&&height==12&&link->handleMouseDown(0,0,0));
  *hidden=true;link->getDimensionsF32(0,count,width,height);assert(width==0&&height==0);
  assert(link->getNumChars(0,0,0,1000,0)==count&&link->draw(0,count,0,0,LLRectf{})==0&&!link->handleMouseDown(0,0,0));
  assert(link->getNumChars(0,count,0,1000,0)==0); // Hidden spans still make bounded forward progress.
  assert(link->getStyle()->color==(link->getStart()==begin?1:2));
  auto clone=link->clone(h);assert(dynamic_cast<FSChatMediaLinkSegment*>(clone.get()));
  *hidden=false;clone->getDimensionsF32(0,count,width,height);assert(width>0&&height==12);
 }
 assert(replaced==2&&h.text==original); // Copying retains URLs and captions; no text rewrite.
 FSChatHistory labelled;labelled.text=L"a descriptive caption";
 labelled.mSegments.insert(segment(labelled,0,labelled.getLength(),url));
 labelled.collapseMediaLink(0,labelled.getLength(),url,hidden);
 assert(!dynamic_cast<FSChatMediaLinkSegment*>(labelled.mSegments.begin()->get()));
 FSChatHistory repeated;repeated.text=wide+L" "+wide;const int n=static_cast<int>(wide.size());
 repeated.mSegments.insert(segment(repeated,0,n,url));repeated.mSegments.insert(segment(repeated,n,n+1,""));
 repeated.mSegments.insert(segment(repeated,n+1,repeated.getLength(),url));
 repeated.collapseMediaLink(n+1,repeated.getLength(),url,hidden);
 assert(!dynamic_cast<FSChatMediaLinkSegment*>(repeated.mSegments.begin()->get()));
 assert(dynamic_cast<FSChatMediaLinkSegment*>(repeated.mSegments.rbegin()->get())); // Only this message body.
}
'''
CARD_STUBS = r'''
struct Handle{LLView* p=nullptr;LLView* get()const{return p;}};
struct Prefs{bool hideLinks=false,animate=false;int mode=1;};bool automaticMotion(){return true;}struct Service{Prefs prefs;};Service host;Service& service(){return host;}
struct Source{bool allowed=true,personal=false;bool valid()const{return allowed;}};
struct Entry{std::shared_ptr<std::atomic<bool>> cancelled=std::make_shared<std::atomic<bool>>(false);std::shared_ptr<fs_chat_media::Image> image;bool finished=false;std::string token="image";};
struct LLFloaterReg{static inline unsigned opens=0;static void* findInstance(const char*,const std::string&){return nullptr;}static std::vector<int> getFloaterList(const char*){return {};}static void showInstance(const char*,const std::string&){++opens;}};
namespace FSChatMedia{static unsigned resizes=0;void resize(FSChatHistory*){++resizes;}}
struct ImageCard:LLPanel{
 Source mSource;std::shared_ptr<Entry> mEntry;std::shared_ptr<bool> mLinkHidden=std::make_shared<bool>(false);Handle mHistory;
 bool mLarge=false,mRenderUnavailable=false,mBoard=false,mBoardPlay=false,mBoardVisible=false,mPlaying=false,mPlaybackSet=false;std::function<void()> mInsert;bool playing()const;int opens=0,mDragX=0,mDragY=0;
 std::function<void()> mSelect;std::shared_ptr<int> mTexture;std::shared_ptr<void> mTextureReservation;int mFrame=-1;bool mRequested=false;
 void fitImage(){}
 void activate(){++opens;}void setLinkHidden(bool);void syncLinkVisibility();
 S32 desiredHeight(S32 width=0)const;
 bool handleMouseDown(S32,S32,MASK);bool handleKeyHere(KEY,MASK);
};
using namespace fs_chat_media;
'''
CARD_CASES = r'''
int main(){
 FSChatHistory history;ImageCard c;c.mHistory.p=&history;
 c.syncLinkVisibility();assert(!*c.mLinkHidden&&c.desiredHeight()==28);
 host.prefs.hideLinks=true;c.syncLinkVisibility();assert(!*c.mLinkHidden);
 c.mEntry=std::make_shared<Entry>();c.mEntry->image=std::make_shared<Image>();c.mEntry->image->width=640;c.mEntry->image->height=320;
 c.syncLinkVisibility();assert(*c.mLinkHidden&&c.desiredHeight()==160&&c.desiredHeight(160)==80);
 const auto reflows=history.reflows;for(int i=0;i<100;++i){c.syncLinkVisibility();}assert(history.reflows==reflows);
 c.mRenderUnavailable=true;c.syncLinkVisibility();assert(!*c.mLinkHidden);c.mRenderUnavailable=false;
 *c.mEntry->cancelled=true;c.syncLinkVisibility();assert(!*c.mLinkHidden);*c.mEntry->cancelled=false;
 c.syncLinkVisibility();assert(*c.mLinkHidden);c.mSource.allowed=false;c.syncLinkVisibility();assert(!*c.mLinkHidden);
 c.mSource.allowed=true;host.prefs.mode=0;c.syncLinkVisibility();assert(!*c.mLinkHidden&&c.desiredHeight()==0);
 host.prefs.mode=1;c.syncLinkVisibility();assert(*c.mLinkHidden);c.mEntry->image.reset();c.syncLinkVisibility();assert(!*c.mLinkHidden&&c.desiredHeight()==28);
 c.childHandled=true;c.handleMouseDown(5,5,0);assert(c.opens==0); // Pause/play dispatch must not also open a preview.
 c.childHandled=false;c.handleMouseDown(5,5,0);assert(c.opens==1);
 gFocusMgr.keyboard=&c;c.handleKeyHere(KEY_RETURN,0);assert(c.opens==2);
 LLPanel button;gFocusMgr.keyboard=&button;
 assert(!c.handleKeyHere(' ',0)&&!c.handleKeyHere(KEY_RETURN,0)&&c.opens==2); // Child buttons own their shortcuts.
 gFocusMgr.keyboard=&c;
 c.mLarge=true;c.handleMouseDown(5,10,0);assert(!gFocusMgr.owner);c.handleMouseDown(5,100,0);assert(gFocusMgr.owner==&c);
 c.mBoard=true;c.mBoardVisible=true;assert(!c.playing());c.handleKeyHere(' ',0);assert(c.playing()); // Explicit Play works in Manual/reduced-motion mode.
 c.mBoardVisible=false;assert(!c.playing());c.mBoardVisible=true;c.handleKeyHere(' ',0);c.mBoardPlay=true;assert(!c.playing()); // Explicit Pause beats autoplay.
 int inserts=0;c.mInsert=[&]{++inserts;};c.handleKeyHere(KEY_RETURN,0);assert(inserts==1);
 gFocusMgr.keyboard=&button;const bool prior=c.playing();
 assert(!c.handleKeyHere(' ',0)&&c.playing()==prior&&!c.handleKeyHere(KEY_RETURN,0)&&inserts==1);
}
'''

RETRY_CASES = r'''
int main(){
 ImageCard c;c.mBoard=true;c.mSource.personal=true;host.prefs.mode=0;
 unsigned selections=0;c.mSelect=[&]{++selections;};
 auto failed=std::make_shared<Entry>();failed->finished=true;c.mEntry=failed;
 c.mTexture=std::make_shared<int>(1);c.mTextureReservation=std::make_shared<int>(1);c.mFrame=5;
 c.activate();assert(selections==1&&*failed->cancelled&&!c.mEntry&&c.mRequested&&!c.mTexture&&!c.mTextureReservation&&c.mFrame==-1);
 // Selecting a loaded or pending tile must never restart its transfer.
 auto loaded=std::make_shared<Entry>();loaded->finished=true;loaded->image=std::make_shared<Image>();c.mEntry=loaded;c.mRequested=false;
 c.activate();assert(selections==2&&c.mEntry==loaded&&!*loaded->cancelled&&!c.mRequested);
 auto pending=std::make_shared<Entry>();c.mEntry=pending;c.activate();assert(selections==3&&c.mEntry==pending&&!*pending->cancelled);
 // A lost source cannot request data merely because the board has independent loading.
 c.mSource.allowed=false;c.mEntry=failed;c.mRequested=false;c.activate();assert(c.mEntry==failed&&!c.mRequested);
 // Incoming-chat Load/Retry remains subject to its own disabled preference.
 c.mSource.allowed=true;c.mSource.personal=false;c.mBoard=false;c.activate();assert(c.mEntry==failed&&!c.mRequested);
 host.prefs.mode=1;c.activate();assert(!c.mEntry&&c.mRequested);
 // Explicit preview of a personal board result works with incoming previews off.
 host.prefs.mode=0;c.mSource.personal=true;c.mEntry=loaded;c.activate();assert(LLFloaterReg::opens==1);
}
'''


class PresentationTests(unittest.TestCase):
    def run_cpp(self, source):
        with tempfile.TemporaryDirectory(prefix='fs-chat-presentation-') as temporary:
            cpp = Path(temporary) / 'test.cpp'
            cpp.write_text(source)
            exe = cpp.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-I', str(ROOT), str(cpp), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_native_link_segments(self):
        text = (ROOT / 'fschathistory.cpp').read_text()
        self.run_cpp(STUBS + function(text, 'class FSChatMediaLinkSegment final') + ';\n' + function(text, 'void FSChatHistory::collapseMediaLink(') + LINK_CASES)

    def test_card_state_and_interaction(self):
        text = (ROOT / 'fschatmedia.cpp').read_text()
        methods = ''.join(function(text, signature).replace('S32 width = 0', 'S32 width') for signature in (
            '    void setLinkHidden(', '    void syncLinkVisibility(', '    S32 desiredHeight(', '    bool handleMouseDown(', '    bool handleKeyHere(', '    bool playing() const'))
        for name in ('setLinkHidden', 'syncLinkVisibility', 'desiredHeight', 'handleMouseDown', 'handleKeyHere', 'playing'):
            methods = methods.replace(' '+name+'(', ' ImageCard::'+name+'(', 1)
        methods = methods.replace(' override', '')
        self.run_cpp(STUBS + CARD_STUBS + methods + CARD_CASES)

    def test_board_retry_with_incoming_previews_disabled(self):
        text = (ROOT / 'fschatmedia.cpp').read_text()
        methods = ''
        # retryLoad is extracted once the production fix adds the shared path;
        # the same test fails against activate() before that fix.
        for name in ('retryLoad', 'activate'):
            if '    void '+name+'(' in text:
                body = function(text, '    void '+name+'(')
                methods += body.replace(' '+name+'(', ' ImageCard::'+name+'(', 1)
        stubs = CARD_STUBS.replace('void activate(){++opens;}', 'void activate();void retryLoad();')
        self.run_cpp(STUBS + stubs + methods + RETRY_CASES)


if __name__ == '__main__':
    unittest.main()
