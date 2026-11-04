"""Production snippets at native buffer/DOM/rebuild boundaries; no GL/Windows runtime."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[2]

class UpstreamMemoryTests(unittest.TestCase):
    def run_cpp(self, code):
        with tempfile.TemporaryDirectory(prefix='fs-upstream-memory-') as directory:
            p=Path(directory)/'test.cpp';p.write_text(code);exe=p.with_suffix('')
            subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_windows_thread_name_argument_count(self):
        text=(ROOT/'llcommon/llthread.cpp').read_text()
        layout=text[text.index('#pragma pack(push,8)'):text.index('#pragma pack(pop)')+len('#pragma pack(pop)')]
        body=function(text,'void set_thread_name( DWORD dwThreadID, const char* threadName)')
        self.run_cpp(r'''
#include <cassert>
#include <cstdint>
using DWORD=std::uint32_t;using ULONG_PTR=std::uintptr_t;using LPCSTR=const char*;
#define __try try
#define __except(x) catch(...)
constexpr DWORD MS_VC_EXCEPTION=0x406D1388;
'''+layout+r'''
void RaiseException(DWORD,DWORD,DWORD count,ULONG_PTR* words){
 assert(count==sizeof(THREADNAME_INFO)/sizeof(ULONG_PTR));
 auto* info=reinterpret_cast<THREADNAME_INFO*>(words);assert(info->dwThreadID==42&&info->dwType==0x1000);
}
'''+body+'int main(){set_thread_name(42,"worker");}\n')

    def test_missing_collada_attribute_arrays(self):
        text=(ROOT/'llprimitive/lldaeloader.cpp').read_text()
        start=text.index('    domListOfFloats  v_dummy;');end=text.index('    LLVolumeFace::VertexMapData::PointMap point_map;',start)
        self.run_cpp(r'''
#include <cassert>
#include <vector>
using F32=float;
namespace LLModel {enum EModelStatus {NO_ERRORS,BAD_ELEMENT};}
struct domListOfFloats:std::vector<float>{using std::vector<float>::vector;std::size_t getCount()const{return size();}};
struct Array {domListOfFloats values{1.f,2.f,3.f};domListOfFloats& getValue(){return values;}};
struct Source {Array* array;Array* getFloat_array(){return array;}};
struct Face {struct Extent {void set(float,float,float){}} mExtents[2];};
LLModel::EModelStatus acquire(Source* pos_source,Source* tc_source,Source* norm_source){Face face;
'''+text[start:end]+r'''
 assert(v.size()==3);if(tc_source)assert(tc.size()==3);if(norm_source)assert(n.size()==3);
 return LLModel::NO_ERRORS;
}
int main(){Array a;Source good{&a},bad{nullptr};
 assert(acquire(&good,nullptr,nullptr)==LLModel::NO_ERRORS);
 assert(acquire(&good,&good,&good)==LLModel::NO_ERRORS);
 assert(acquire(&bad,&good,&good)==LLModel::BAD_ELEMENT);
 assert(acquire(&good,&bad,&good)==LLModel::BAD_ELEMENT);
 assert(acquire(&good,&good,&bad)==LLModel::BAD_ELEMENT);
}
''')

    def test_both_texture_animation_removal_paths_rebuild(self):
        text=(ROOT/'newview/llvovolume.cpp').read_text()
        blocks=re.findall(r'gPipeline\.markTextured\(mDrawable\);\s*if \(LLSpatialGroup\* group = mDrawable->getSpatialGroup\(\)\).*?mTexAnimMode = 0;',text,re.S)
        self.assertEqual(len(blocks),2)
        source=r'''
#include <cassert>
struct LLSpatialGroup {bool dirty=false;void dirtyGeom(){dirty=true;}};
struct Drawable {LLSpatialGroup* group;LLSpatialGroup* getSpatialGroup(){return group;}};
struct Pipeline {int marked=0,rebuild=0;void markTextured(Drawable*){++marked;}void markRebuild(LLSpatialGroup* group){assert(group->dirty);++rebuild;}}gPipeline;
'''
        for i,block in enumerate(blocks):
            source+=f'void removal{i}(Drawable* mDrawable){{bool mFaceMappingChanged=false;int mTexAnimMode=1;\n'+block+'\nassert(mFaceMappingChanged&&mTexAnimMode==0);}\n'
        source+='int main(){LLSpatialGroup group;Drawable with{&group},without{nullptr};removal0(&with);removal1(&with);removal0(&without);removal1(&without);assert(gPipeline.marked==4&&gPipeline.rebuild==2);}\n'
        self.run_cpp(source)

if __name__=='__main__':unittest.main()
