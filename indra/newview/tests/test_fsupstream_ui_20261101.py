"""Native callback/messaging snippets with bounded lifecycle fixtures."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[1]

class UpstreamUITests(unittest.TestCase):
 def run_cpp(self,code):
  with tempfile.TemporaryDirectory(prefix='fs-upstream-ui-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(code);exe=p.with_suffix('')
   includes=['-I'+os.environ['FS_UPSTREAM_BOOST_INCLUDE']] if os.environ.get('FS_UPSTREAM_BOOST_INCLUDE') else []
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG']+includes+[str(p),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
 def test_closed_conversation_is_not_dereferenced(self):
  text=(ROOT/'llfloaterimcontainer.cpp').read_text();header=(ROOT/'llfloaterimcontainer.h').read_text()
  self.assertIn('std::map<LLUUID, LLHandle<LLFloater> >',header)
  self.assertEqual(text.count('mSessions[session_id] = floaterp->getHandle();'),2)
  self.run_cpp(r'''
#include <cassert>
#include <map>
#include <memory>
using LLUUID=int;
struct LLSD {const LLSD& operator[](const char*)const{return *this;}int asUUID()const{return 1;}};
struct LLFloater{};
template<class T>struct LLHandle{std::weak_ptr<T> value;T* get()const{return value.lock().get();}};
struct LLMultiFloater {static inline LLFloater active;static inline int flashes=0;static LLFloater* getActiveFloater(){return &active;}
 static bool isFloaterFlashing(LLFloater*){return false;}static void setFloaterFlashing(LLFloater*,bool){++flashes;}};
struct LLFloaterIMContainer {using avatarID_panel_map_t=std::map<int,LLHandle<LLFloater>>;avatarID_panel_map_t mSessions;void onNewMessageReceived(const LLSD&);};
'''+function(text,'void LLFloaterIMContainer::onNewMessageReceived(const LLSD& data)')+r'''
int main(){LLFloaterIMContainer container;auto f=std::make_shared<LLFloater>();container.mSessions[1]={f};
 container.onNewMessageReceived({});assert(LLMultiFloater::flashes==1);f.reset();container.onNewMessageReceived({});assert(LLMultiFloater::flashes==1);
 container.mSessions.clear();container.onNewMessageReceived({});assert(LLMultiFloater::flashes==1);}
''')
 def test_login_focus_callbacks_disconnect_before_children_die(self):
  text=(ROOT/'fspanellogin.cpp').read_text()
  body=function(text,'FSPanelLogin::~FSPanelLogin()')
  begin=body.index('    if (mStartLocationFocusLostConnection.connected())');end=body.index('    FSPanelLogin::sInstance',begin)
  for member,combo,handler in [('mStartLocationFocusLostConnection','favorites_combo','onLocationSLURL'),('mUsernameFocusLostConnection','username_combo','onSelectUser')]:
   self.assertIn(member+' = '+combo+'->setFocusLostCallback(boost::bind(&FSPanelLogin::'+handler+', this));',text)
  self.run_cpp(r'''
#include <cassert>
#include <boost/signals2.hpp>
struct Panel {boost::signals2::connection mStartLocationFocusLostConnection,mUsernameFocusLostConnection;
 ~Panel(){
'''+body[begin:end]+r'''
}};
int main(){boost::signals2::signal<void()> start,user;int calls=0;
 {Panel p;p.mStartLocationFocusLostConnection=start.connect([&]{++calls;});p.mUsernameFocusLostConnection=user.connect([&]{++calls;});start();user();assert(calls==2);}
 start();user();assert(calls==2);}
''')
 def test_tab_enum_accepts_signed_s32_positions(self):
  text=(ROOT.parent/'llui/lltabcontainer.h').read_text()
  enum=re.search(r'typedef enum e_insertion_point : S32.*?} eInsertionPoint;',text,re.S).group()
  self.run_cpp('#include <type_traits>\n#include <cstdint>\n#include <cassert>\nusing S32=std::int32_t;\n'+enum+'\nint main(){static_assert(std::is_same_v<std::underlying_type_t<eInsertionPoint>,S32>);assert(static_cast<S32>(static_cast<eInsertionPoint>(42))==42);assert(START==-1&&END==-2);}\n')
 def test_concurrent_directory_pickers_have_unique_listener_names(self):
  text=(ROOT/'lldirpicker.cpp').read_text()
  self.assertIn('LLEventPump::inventName("DirPicker")',text)
  self.assertNotIn('"DirPicker",\n            [this]',text)

if __name__=='__main__':unittest.main()
