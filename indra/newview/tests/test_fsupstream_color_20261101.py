"""Compile actual LLUIColor traversal, including cycles and valid deep aliases."""
from pathlib import Path
import os,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[2]
class ColorTraversalTests(unittest.TestCase):
 def test_cycle_bound_and_deep_acyclic_policy(self):
  text=(ROOT/'llui/lluicolor.cpp').read_text()
  source=r'''
#include <array>
#include <cassert>
#include <cstddef>
#include <sstream>
int assertions=0,warnings=0;
#define llassert(x) do{if(!(x))++assertions;}while(false)
std::ostringstream warning(){++warnings;return {};}
#define LL_WARNS_ONCE() warning()
#define LL_ENDL ""
struct LLColor4 {int value=0;static const LLColor4 magenta;};const LLColor4 LLColor4::magenta{-1};
struct LLUIColor {LLColor4 mColor{7};const LLUIColor* mColorPtr=nullptr;const LLColor4& get()const;};
'''+function(text,'const LLColor4& LLUIColor::get() const')+r'''
int main(){LLUIColor leaf;assert(leaf.get().value==7);LLUIColor alias;alias.mColorPtr=&leaf;leaf.mColor.value=12;assert(alias.get().value==12);
 std::array<LLUIColor,11> valid;for(std::size_t i=0;i+1<valid.size();++i)valid[i].mColorPtr=&valid[i+1];assert(valid[0].get().value==7&&warnings==0&&assertions==0);
 std::array<LLUIColor,12> deep;for(std::size_t i=0;i+1<deep.size();++i)deep[i].mColorPtr=&deep[i+1];assert(deep[0].get().value==-1&&warnings==1&&assertions==1);
 LLUIColor cycle;cycle.mColorPtr=&cycle;assert(cycle.get().value==-1&&warnings==2&&assertions==2);
 LLUIColor a,b;a.mColorPtr=&b;b.mColorPtr=&a;assert(a.get().value==-1&&warnings==3&&assertions==3);}
'''
  with tempfile.TemporaryDirectory(prefix='fs-upstream-color-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(source);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
if __name__=='__main__':unittest.main()
