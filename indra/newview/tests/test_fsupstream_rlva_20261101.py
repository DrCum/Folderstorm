"""Production native restriction methods against bounded position/UI fixtures."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[1]
COMMON=r'''
#include <cassert>
#include <algorithm>
#include <map>
#include <string>
using F32=float;using LLUUID=int;
constexpr int VX=0,VY=1,VZ=2,RLV_BHVR_SITTP=1,RLV_BHVR_TPLOCAL=2,RLV_BHVR_SHOWLOC=3,RLV_MODIFIER_SITTPDIST=4,RLV_MODIFIER_TPLOCALDIST=5;
struct LLVector3d {double mdV[3];LLVector3d operator-(const LLVector3d& b)const{return {{mdV[0]-b.mdV[0],mdV[1]-b.mdV[1],mdV[2]-b.mdV[2]}};}double lengthSquared()const{return mdV[0]*mdV[0]+mdV[1]*mdV[1]+mdV[2]*mdV[2];}};
struct LLVector2 {float x,y;LLVector2(float a,float b):x(a),y(b){}LLVector2 operator-(const LLVector2& b)const{return {x-b.x,y-b.y};}float lengthSquared()const{return x*x+y*y;}};
struct LLViewerRegion{std::string getName(){return "region";}}region;
struct Agent{LLVector3d position{{100000.,100000.,1000.}};LLVector3d getPositionGlobal(){return position;}LLVector3d getPositionAgent(){return position;}LLViewerRegion* getRegion(){return &region;}}gAgent;
struct Handler{LLUUID current=0;std::map<int,bool> blocked;const LLUUID& getCurrentObject(){return current;}bool hasBehaviourExcept(int b,const LLUUID& except){return !except&&blocked[b];}bool hasBehaviour(int b){return blocked[b];}}gRlvHandler;
template<class T>T llmin(T a,T b){return std::min(a,b);}
'''
class RLVaTests(unittest.TestCase):
 def run_cpp(self,code):
  with tempfile.TemporaryDirectory(prefix='fs-upstream-rlva-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(code);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 def test_local_teleport_3d_default_max_and_native_exceptions(self):
  text=(ROOT/'rlvactions.cpp').read_text();defs=(ROOT/'rlvdefines.h').read_text()
  macros='\n'.join(re.findall(r'^#define RLV_MODIFIER_TPLOCAL_(?:DEFAULT|MAX)\s+[^\n]+',defs,re.M))
  self.run_cpp(COMMON+macros+r'''
struct Modifier{float value;template<class T>T getValue(){return static_cast<T>(value);}};
struct RlvBehaviourDictionary {Modifier local{RLV_MODIFIER_TPLOCAL_DEFAULT},sit{1.5f};static RlvBehaviourDictionary& instance(){static RlvBehaviourDictionary d;return d;}
 Modifier* getModifier(int id){return id==RLV_MODIFIER_TPLOCALDIST?&local:&sit;}};
struct RlvActions{static inline bool stand=true;static bool canStand(const LLUUID&){return stand;}static bool canTeleportToLocal(const LLVector3d&);static bool isLocalTp(const LLVector3d&);};
'''+function(text,'bool RlvActions::canTeleportToLocal(const LLVector3d& posGlobal)')+function(text,'bool RlvActions::isLocalTp(const LLVector3d& posGlobal)')+r'''
LLVector3d offset(double x,double y,double z){auto p=gAgent.position;p.mdV[0]+=x;p.mdV[1]+=y;p.mdV[2]+=z;return p;}
int main(){gRlvHandler.blocked[RLV_BHVR_TPLOCAL]=true;assert(RlvActions::canTeleportToLocal(offset(1,0,0)));assert(!RlvActions::canTeleportToLocal(offset(1.5,0,0)));assert(!RlvActions::canTeleportToLocal(offset(0,0,2)));assert(!RlvActions::canTeleportToLocal(offset(1,1,1)));
 RlvBehaviourDictionary::instance().local.value=300.f;assert(RlvActions::canTeleportToLocal(offset(255,0,0)));assert(!RlvActions::canTeleportToLocal(offset(257,0,0)));
 assert(RlvActions::isLocalTp(offset(200,0,0))&&!RlvActions::isLocalTp(offset(300,0,0)));gRlvHandler.current=9;assert(RlvActions::canTeleportToLocal(offset(500,0,0)));
 RlvActions::stand=false;assert(!RlvActions::canTeleportToLocal(offset(0,0,0)));}
''')
  self.assertIn('RLV_MODIFIER_TPLOCAL_DEFAULT, true',(ROOT/'rlvhelper.cpp').read_text())
 def test_profile_set_location_enforces_showloc_at_action_and_ui(self):
  text=(ROOT/'llpanelprofilepicks.cpp').read_text()
  self.run_cpp(COMMON+r'''
struct LLParcel{int getID(){return 4;}std::string getName(){return "parcel";}}parcel;
struct LLViewerParcelMgr{static LLViewerParcelMgr* getInstance(){static LLViewerParcelMgr m;return &m;}LLParcel* getAgentParcel(){return &parcel;}};
struct Button{bool visible=false,enabled=false;void setVisible(bool v){visible=v;}void setEnabled(bool v){enabled=v;}};
struct LLPanelProfilePick{Button button;Button* mSetCurrentLocationButton=&button;bool self=true,mLocationChanged=false,saved=false;int mParcelId=0,writes=0;
 bool getSelfProfile(){return self;}void setPosGlobal(const LLVector3d&){++writes;}LLVector3d getPosGlobal(){return gAgent.position;}
 std::string getLocationNotice(){return "notice";}std::string createLocationText(const std::string&,const std::string&,const std::string&,const LLVector3d&){return "label";}
 void setPickLocation(const std::string&){++writes;}void enableSaveButton(bool value){saved=value;}void updateButtons();void onClickSetLocation();};
'''+function(text,'void LLPanelProfilePick::updateButtons()')+function(text,'void LLPanelProfilePick::onClickSetLocation()')+r'''
int main(){LLPanelProfilePick pick;gRlvHandler.blocked[RLV_BHVR_SHOWLOC]=true;pick.updateButtons();assert(pick.button.visible&&!pick.button.enabled);pick.onClickSetLocation();assert(pick.writes==0&&!pick.mLocationChanged&&!pick.saved);
 gRlvHandler.blocked[RLV_BHVR_SHOWLOC]=false;pick.updateButtons();assert(pick.button.enabled);pick.onClickSetLocation();assert(pick.writes==2&&pick.mLocationChanged&&pick.saved);
 pick.self=false;pick.updateButtons();assert(!pick.button.visible&&!pick.button.enabled);}
''')
 def test_fartouch_compares_neighbor_prim_in_agent_space(self):
  text=(ROOT/'lltoolselect.cpp').read_text();expression=re.search(r'dist_vec_squared\(gAgent.getPositionAgent\(\), object->getPositionAgent\(\)\) > nFartouchDistSq',text).group()
  self.run_cpp(COMMON+r'''
struct Object{LLVector3d getPositionAgent(){return {{257,128,0}};}LLVector3d getPositionRegion(){return {{1,128,0}};}}prim;
double dist_vec_squared(const LLVector3d& a,const LLVector3d& b){return (a-b).lengthSquared();}
bool outside(float nFartouchDistSq){auto* object=&prim;return
'''+expression+r''';}
int main(){gAgent.position={{255,128,0}};assert(!outside(4.f));assert(outside(1.f));}
''')
if __name__=='__main__':unittest.main()
