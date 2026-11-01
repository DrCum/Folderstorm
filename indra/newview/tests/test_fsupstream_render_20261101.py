"""Production render/animation methods with bounded clock, GL and scene fixtures."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[2]
class RenderTests(unittest.TestCase):
 def run_cpp(self,source):
  with tempfile.TemporaryDirectory(prefix='fs-upstream-render-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(source);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 def test_hidden_motion_clock_pause_and_idle_work(self):
  text=(ROOT/'llcharacter/llmotioncontroller.cpp').read_text()
  self.run_cpp(r'''
#include <cassert>
using F32=float;
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR
struct LLMotionController {struct Timer{float value=10.f;float getElapsedTimeF32(){return value;}}mTimer;
 float mPrevTimerElapsed=0.f,mLastTime=0.f,mAnimTime=50.f,mTimeFactor=.5f;bool mPaused=false,mHasRunOnce=false;
 int purges=0,loads=0,resets=0,idles=0,deactivations=0;
 void purgeExcessMotions(){++purges;}void updateLoadingMotions(){++loads;}void resetJointSignatures(){++resets;}
 void deactivateStoppedMotions(){++deactivations;}void updateIdleActiveMotions(){++idles;}void updateMotionsMinimal();};
'''+function(text,'void LLMotionController::updateMotionsMinimal()')+r'''
int main(){LLMotionController c;c.updateMotionsMinimal();assert(c.mAnimTime==55.f&&c.mLastTime==50.f&&c.idles==1&&c.mHasRunOnce);
 c.mPaused=true;c.mTimer.value=20.f;c.updateMotionsMinimal();assert(c.mAnimTime==55.f&&c.mLastTime==55.f&&c.deactivations==1);
 c.mPaused=false;c.mTimer.value=30.f;c.updateMotionsMinimal();assert(c.mAnimTime==60.f&&c.idles==2&&c.mPrevTimerElapsed==30.f);
 assert(c.purges==3&&c.loads==3&&c.resets==3);}
''')
 def test_fxaa_keeps_destination_viewport_for_scaled_custom_world(self):
  text=(ROOT/'newview/pipeline.cpp').read_text()
  self.run_cpp(r'''
#include <algorithm>
#include <cassert>
using S32=int;using U32=unsigned;using F32=float;
#define LL_PROFILE_GPU_ZONE(x)
#define llassert(x) assert(x)
constexpr bool GL_TRUE=true;constexpr int GL_ALWAYS=1,GL_COLOR_BUFFER_BIT=2;
bool gCubeSnapshot=false;int gGLViewport[4];int targetWidth=0,targetHeight=0;
void glViewport(int x,int y,int w,int h){gGLViewport[0]=x;gGLViewport[1]=y;gGLViewport[2]=w;gGLViewport[3]=h;}
struct LLTexUnit {enum {TFO_BILINEAR};template<class T>void bind(T*,bool){}}unit;
struct GL {LLTexUnit* getTexUnit(int){return &unit;}}gGL;
struct LLRender {enum {TRIANGLES};};
struct LLGLDepthTest {LLGLDepthTest(bool,bool,int){}};
struct LLShaderMgr {enum{DEFERRED_DIFFUSE,DIFFUSE_MAP,FXAA_TC_SCALE,FXAA_RCP_SCREEN_RES,FXAA_RCP_FRAME_OPT,FXAA_RCP_FRAME_OPT2,DEFERRED_DEPTH};};
struct LLGLSLShader {bool isComplete(){return true;}void bind(){}void unbind(){}int enableTexture(int,int){return 0;}void disableTexture(int,int){}
 void uniform2f(int,float,float){}void uniform4f(int,float,float,float,float){}int getTextureChannel(int){return 0;}}gGlowCombineFXAAProgram,gFXAAProgram[4];
struct LLRenderTarget {int w,h;LLRenderTarget(int a=1,int b=1):w(a),h(b){}int getWidth(){return w;}int getHeight(){return h;}int getUsage(){return 0;}
 bool isComplete(){return true;}void bindTarget(){targetWidth=w;targetHeight=h;glViewport(0,0,w,h);}void clear(int){}void bindTexture(int,int,int){}void flush(){}};
struct VB {int draws=0;void setBuffer(){}void drawArrays(int,int,int){++draws;assert(gGLViewport[0]==0&&gGLViewport[1]==0&&gGLViewport[2]==targetWidth&&gGLViewport[3]==targetHeight);}}vb;
struct Window {struct Rect{int mLeft=800,mBottom=200;int getWidth(){return 1920;}int getHeight(){return 1080;}};Rect getWorldViewRectRaw(){return {};}}window;Window* gViewerWindow=&window;
struct Saved{}gSavedSettings;template<class T>struct LLCachedControl{T value;LLCachedControl(Saved&,const char*,T v):value(v){}T operator()(){return value;}};
struct LLPipeline {int RenderFSAAType=1,copies=0;LLRenderTarget mFXAAMap{1920,1080};struct RT{LLRenderTarget deferredScreen;}rt;RT* mRT=&rt;VB* mScreenTriangleVB=&vb;
 void copyRenderTarget(LLRenderTarget*,LLRenderTarget*){++copies;}void applyFXAA(LLRenderTarget*,LLRenderTarget*);};
'''+function(text,'void LLPipeline::applyFXAA(LLRenderTarget* src, LLRenderTarget* dst)')+r'''
int main(){LLPipeline p;LLRenderTarget src(1920,1080),scaled(960,540);p.applyFXAA(&src,&scaled);assert(vb.draws==2);
 LLRenderTarget small(320,180);p.applyFXAA(&src,&small);assert(vb.draws==4);p.RenderFSAAType=0;p.applyFXAA(&src,&small);assert(p.copies==1&&vb.draws==4);}
''')
 def test_short_skin_palette_initializes_identity_and_null_upload_is_refused(self):
  text=(ROOT/'newview/llskinningutil.cpp').read_text()
  start=text.index('    if (skin->mInvBindMatrix.size() < count )');end=text.index('    LLMatrix4a world[',start)
  self.run_cpp(r'''
#include <cassert>
#include <vector>
using S32=int;struct LLMatrix4a{int value=99;static LLMatrix4a identity(){return {1};}};
struct Skin{struct Entries{S32 size()const{return 0;}}mInvBindMatrix;};void init(LLMatrix4a* mat,S32 count,const Skin* skin){
'''+text[start:end]+r'''
}
int main(){Skin skin;LLMatrix4a matrices[4];init(matrices,3,&skin);assert(matrices[0].value==1&&matrices[2].value==1&&matrices[3].value==99);}
''')
  pool=(ROOT/'newview/lldrawpool.cpp').read_text();self.assertEqual(pool.count('if (!avatar || !skinInfo)'),3)
  self.assertIn('LLConstPointer<LLMeshSkinInfo> mSkinInfo',(ROOT/'newview/llface.h').read_text())
  self.assertIn('LLConstPointer<LLMeshSkinInfo> mSkinInfo',(ROOT/'newview/llspatialpartition.h').read_text())
 def test_only_culled_terrain_can_be_picked(self):
  text=(ROOT/'newview/llspatialpartition.cpp').read_text()
  match=re.search(r'if \(!drawable \|\| !gPipeline.hasRenderType\(drawable->getRenderType\(\)\)\s*\|\| \(!drawable->isVisible\(\) && drawable->getRenderType\(\) != LLPipeline::RENDER_TYPE_TERRAIN\)\)\s*\{\s*return false;\s*\}',text)
  self.assertIsNotNone(match)
  self.run_cpp(r'''
#include <cassert>
struct LLPipeline{enum {RENDER_TYPE_TERRAIN=1};bool enabled=true;bool hasRenderType(int){return enabled;}}gPipeline;
struct Drawable{int type;bool visible;int getRenderType(){return type;}bool isVisible(){return visible;}};
bool pick(Drawable* drawable){
'''+match.group()+r'''
return true;}
int main(){Drawable terrain{1,false},object{2,false},visible{2,true};assert(pick(&terrain)&&!pick(&object)&&pick(&visible)&&!pick(nullptr));gPipeline.enabled=false;assert(!pick(&terrain));}
''')
if __name__=='__main__':unittest.main()
