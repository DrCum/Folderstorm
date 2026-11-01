"""Native clipboard/color snippets and shipped browser XML invariants."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile,unittest,xml.etree.ElementTree as ET
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[2]
class EditorTests(unittest.TestCase):
 def run_cpp(self,source):
  with tempfile.TemporaryDirectory(prefix='fs-upstream-editor-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(source);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 def test_no_copy_link_and_nonlink_clipboard_eligibility(self):
  text=(ROOT/'llui/llfolderview.cpp').read_text()
  self.run_cpp(r'''
#include <cassert>
#include <vector>
struct LLFolderViewModelItem{bool copy=false,link=false;bool isItemCopyable()const{return copy;}bool isItemLinkable()const{return link;}};
struct LLFolderViewItem{LLFolderViewModelItem* model;const LLFolderViewModelItem* getViewModelItem()const{return model;}};
struct LLFolderView{using selected_items_t=std::vector<LLFolderViewItem*>;selected_items_t mSelectedItems;bool visible=true,enabled=true;
 bool getVisible()const{return visible;}bool getEnabled()const{return enabled;}bool canCopy()const;};
'''+function(text,'bool LLFolderView::canCopy() const')+r'''
int main(){LLFolderView view;LLFolderViewModelItem noCopyLink{false,true},noCopyNoLink{false,false},copy{true,false};LLFolderViewItem a{&noCopyLink},b{&noCopyNoLink},c{&copy};
 assert(!view.canCopy());view.mSelectedItems={&a};assert(view.canCopy()&&!noCopyLink.isItemCopyable());view.mSelectedItems={&b};assert(!view.canCopy());view.mSelectedItems={&a,&c};assert(view.canCopy());
 view.mSelectedItems.push_back(&b);assert(!view.canCopy());view.mSelectedItems={&a};view.enabled=false;assert(!view.canCopy());view.enabled=true;view.visible=false;assert(!view.canCopy());}
''')
  bridge=(ROOT/'newview/llinventorybridge.cpp').read_text()
  self.assertIn('item->getPermissions().allowCopyBy(gAgent.getID())',bridge)
 def test_legacy_tokens_keep_live_color_table_reference(self):
  text=(ROOT/'llui/llkeywords.cpp').read_text();color=(ROOT/'llui/lluicolor.cpp').read_text()
  declaration=re.search(r'^\s*LLUIColor cur_color\(LLUIColorTable::instance\(\).getColor\("ScriptText"\)\);',text,re.M).group()
  self.run_cpp(r'''
#include <cassert>
#include <cstddef>
using LLColor4=int;
struct LLUIColor {LLColor4 mColor=0;const LLUIColor* mColorPtr=nullptr;LLUIColor()=default;LLUIColor(int v):mColor(v){}LLUIColor(const LLUIColor* v):mColorPtr(v){}
 const LLColor4& get()const;operator const LLColor4&()const{return get();}};
struct LLUIColorTable {LLUIColor base{7};static LLUIColorTable& instance(){static LLUIColorTable t;return t;}LLUIColor getColor(const char*){return LLUIColor(&base);}};
'''+function(color,'const LLColor4& LLUIColor::get() const')+'int main(){\n'+declaration+r'''
 struct Token{LLUIColor color;};Token token{cur_color};assert(token.color.get()==7);LLUIColorTable::instance().base.mColor=12;assert(token.color.get()==12);}
''')
 def test_browser_failure_overlay_is_above_visible_browser(self):
  for name in ['floater_web_content.xml','floater_media_browser.xml','floater_marketplace.xml']:
   root=ET.parse(ROOT/'newview/skins/default/xui/en'/name).getroot();found=False
   for parent in root.iter():
    children=list(parent);fail=[i for i,c in enumerate(children) if c.get('name')=='plugin_fail_text'];browser=[i for i,c in enumerate(children) if c.tag=='web_browser']
    if fail and browser:
     self.assertLess(browser[0],fail[0]);self.assertNotEqual(children[browser[0]].get('visible'),'false');found=True
   self.assertTrue(found,name)

if __name__=='__main__':unittest.main()
