"""Compiled production methods/snippets; native grid transport is a fixture."""
from pathlib import Path
import os,re,shlex,subprocess,tempfile,unittest
from test_fsobjecttools_native import function
ROOT=Path(__file__).resolve().parents[1]
COMMON=r'''
#include <cassert>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <sstream>
using S32=int;using F64=double;
#define LL_DEBUGS(x) std::ostringstream()
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL ""
template<class T>T llmin(T a,T b){return std::min(a,b);}template<class T>T llmax(T a,T b){return std::max(a,b);}
'''
class OpenSimTests(unittest.TestCase):
 def run_cpp(self,code):
  with tempfile.TemporaryDirectory(prefix='fs-opensim-') as directory:
   p=Path(directory)/'test.cpp';p.write_text(code);exe=p.with_suffix('')
   subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',str(p),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
 def test_region_utf8_is_preserved_and_controls_removed(self):
  text=(ROOT/'fsslurl.cpp').read_text();start=text.index('            mRegion.erase(std::remove_if');end=text.index(';',text.index('mRegion.end());',start))+1
  fragment=text[start:end]
  self.run_cpp(COMMON+'std::string filter(std::string mRegion){\n'+fragment+'\nreturn mRegion;}\n'+r'''
int main(){for(const auto& s:{std::string(u8"日本語 Région"),std::string(u8"Київ"),std::string(u8"São Paulo")})assert(filter(s)==s);
 assert(filter(std::string("a\n\t\0b",5))=="ab");}
''')
 def test_grid_probe_retains_alias_results_and_host_precedence(self):
  text=(ROOT/'fsgridhandler.cpp').read_text()
  self.run_cpp(COMMON+r'''
struct LLGridManager {
 std::string getGridByHostName(const std::string& s,bool)const{return s=="host"||s=="collision"?"host-key":"";}
 std::string getGridByGridNick(const std::string& s,bool c)const{return s=="nick"||s=="collision"||(!c&&s=="NICK")?"nick-key":"";}
 std::string getGridByLabel(const std::string& s,bool)const{return s=="label"?"label-key":"";}
 std::string getGridByProbing(const std::string&,bool)const;
};
'''+function(text,'std::string LLGridManager::getGridByProbing(const std::string& probe_for, bool case_sensitive) const')+r'''
int main(){LLGridManager g;assert(g.getGridByProbing("host",true)=="host-key");assert(g.getGridByProbing("nick",true)=="nick-key");assert(g.getGridByProbing("label",true)=="label-key");
 assert(g.getGridByProbing("collision",true)=="host-key");assert(g.getGridByProbing("NICK",true).empty());assert(g.getGridByProbing("NICK",false)=="nick-key");assert(g.getGridByProbing("unknown",false).empty());}
''')
  self.assertIn('mGrid = grid_name;',text);self.assertIn('gSavedSettings.setString("CurrentGrid", mGrid);',text)
 def test_map_requests_cover_cache_blocks_once_within_server_limit(self):
  text=(ROOT/'llworldmap.cpp').read_text()
  self.run_cpp(COMMON+r'''
constexpr S32 MAP_BLOCK_SIZE=16,MAP_BLOCK_RES=128;constexpr double BLOCK_UPDATE_TIMER=60.;
struct LLTimer{static double getElapsedSeconds(){return 100.;}};
struct LLWorldMapMessage {struct Rect{int x0,y0,x1,y1;};std::vector<Rect> requests;
 static LLWorldMapMessage* getInstance(){static LLWorldMapMessage m;return &m;}
 void sendMapBlockRequest(int a,int b,int c,int d){requests.push_back({a,b,c,d});}};
struct LLWorldMap {using block_last_update_map_t=std::map<int,double>;block_last_update_map_t mMapBlockLastUpdateOffsets;std::map<int,bool> mMapBlockLoaded;
 void updateRegions(S32,S32,S32,S32);};
'''+function(text,'void LLWorldMap::updateRegions(S32 x0, S32 y0, S32 x1, S32 y1)')+r'''
int main(){for(int side:{16,32,64}){LLWorldMap map;auto& requests=LLWorldMapMessage::getInstance()->requests;requests.clear();map.updateRegions(0,0,side-1,side-1);
 std::set<std::pair<int,int>> cells;assert(!requests.empty());for(const auto& r:requests){assert(r.x1>=r.x0&&r.y1>=r.y0);assert((r.x1-r.x0+1)*(r.y1-r.y0+1)<=64);
  for(int y=r.y0;y<=r.y1;++y)for(int x=r.x0;x<=r.x1;++x){assert(x>=0&&x<side&&y>=0&&y<side);assert(cells.emplace(x,y).second);}}
 assert(cells.size()==static_cast<std::size_t>(side*side));auto count=requests.size();map.updateRegions(0,0,side-1,side-1);assert(requests.size()==count);}}
''')
 def test_reconnect_with_no_endpoint_can_retry_later(self):
  text=(ROOT/'lllogininstance.cpp').read_text()
  self.run_cpp(COMMON+r'''
struct LLGridManager {std::vector<std::string> uris;static LLGridManager* getInstance(){static LLGridManager g;return &g;}void getLoginURIs(std::vector<std::string>& out){out=uris;}};
struct Login {int calls=0;void connect(const std::string& uri,const std::string& data){assert(uri=="endpoint"&&data=="original");++calls;}};
struct Settings {bool getBOOL(const char*)const{return false;}}gSavedSettings;
struct Window {int shown=0;void setShowProgress(bool,bool){++shown;}}window;Window* gViewerWindow=&window;
struct LLLoginInstance {bool mAttemptComplete=true;std::string mRequestData="original";Login login;Login* mLoginModule=&login;void reconnect();};
'''+function(text,'void LLLoginInstance::reconnect()')+r'''
int main(){LLLoginInstance login;login.reconnect();assert(login.login.calls==0&&login.mRequestData=="original"&&window.shown==0);
 LLGridManager::getInstance()->uris={"endpoint"};login.reconnect();assert(login.login.calls==1&&window.shown==1);}
''')
 def test_hypergrid_constructor_forwarding(self):
  text=(ROOT/'fsslurl.cpp').read_text();header=(ROOT/'fsslurl.h').read_text()
  self.assertIn('LLSLURL(): mType(INVALID), mHypergrid(false)',header)
  for signature in ['LLSLURL::LLSLURL(const std::string& region, const LLVector3& position, bool hypergrid)',
    'LLSLURL::LLSLURL(const std::string& grid, const std::string& region, const LLVector3d& region_origin, const LLVector3d& global_position, bool hypergrid)',
    'LLSLURL::LLSLURL(const std::string& region, const LLVector3d& region_origin, const LLVector3d& global_position, bool hypergrid)']:
   self.assertIn(', hypergrid);',function(text,signature))

if __name__=='__main__':unittest.main()
