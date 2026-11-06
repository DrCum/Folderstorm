"""Production monitor-cache, event-relay and idle guards with native API stubs.

These checks count Windows enumeration calls and model queued callbacks. They
do not replace a Windows build or real hotplug/mixed-DPI acceptance.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

from test_fsgestureboardsounds import function


INDRA = Path(__file__).resolve().parents[2]
ROOT = INDRA.parent


class MonitorCache(unittest.TestCase):
    def cpp(self, source):
        with tempfile.TemporaryDirectory(prefix="monitor-cache-") as directory:
            path = Path(directory) / "test.cpp"
            path.write_text(source)
            executable = path.with_suffix("")
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                "-I" + str(INDRA / "newview"), str(path), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True, timeout=20)

    def test_native_cache_projection_events_and_retry(self):
        source = (INDRA / "llwindow/llwindowwin32.cpp").read_text()
        header = (INDRA / "llwindow/llwindowwin32.h").read_text()
        fields = header[header.index("    std::vector<MonitorRect> mDesktopMonitors;"):
                        header.index("    void updateWindowTheme();")]
        query = function(source, "struct MonitorRectQuery") + ";\n"
        methods = "\n".join(function(source, signature) for signature in (
            "void LLWindowWin32::invalidateMonitorCache()",
            "bool LLWindowWin32::getMonitorRectsInClient(std::vector<MonitorRect>& monitors)",
        ))
        display = source[source.index("        case WM_DISPLAYCHANGE:"):
                         source.index("        case WM_SETFOCUS:")]
        dpi = source[source.index("        case WM_DPICHANGED:"):
                     source.index("        case WM_DISPLAYCHANGE:")]
        device = function(source, "if (w_param == DBT_DEVNODES_CHANGED || w_param == DBT_CONFIGCHANGED)")
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cwchar>
#include <deque>
#include <functional>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
using BOOL=int; using LONG=std::int32_t; using UINT=unsigned;
using F32=float; using S32=int; using ULONGLONG=std::uint64_t;
using HWND=void*; using HDC=void*; using HMONITOR=std::uintptr_t;
using LPARAM=std::intptr_t; using WPARAM=std::uintptr_t; using LRESULT=std::intptr_t;
constexpr BOOL FALSE=0,TRUE=1;
#define CALLBACK
bool appThread=true;
#define ASSERT_MAIN_THREAD() assert(appThread)
#define LL_PROFILE_ZONE_NAMED_CATEGORY_WIN32(x)
struct RECT {LONG left=0,top=0,right=0,bottom=0;}; using LPRECT=RECT*;
struct POINT {LONG x=0,y=0;};
struct MONITORINFOEXW {unsigned cbSize=0;RECT rcMonitor;wchar_t szDevice[32]{};};
using MONITORINFO=MONITORINFOEXW;
struct DISPLAY_DEVICEW {unsigned cb=0;wchar_t DeviceString[128]{};};
struct LLRect {
    LONG mLeft=0,mTop=0,mRight=0,mBottom=0;
    void set(LONG l,LONG t,LONG r,LONG b){mLeft=l;mTop=t;mRight=r;mBottom=b;}
    void translate(LONG x,LONG y){mLeft+=x;mRight+=x;mTop+=y;mBottom+=y;}
};
struct LLWindow {struct MonitorRect {std::string id,name;LLRect rect;};};
struct DesktopMonitor {std::wstring id,name;RECT bounds;};
std::vector<DesktopMonitor> desktop={
    {L"DISPLAY1",L"Primary",{0,0,1920,1080}},
    {L"DISPLAY2",L"Side screen",{-1280,-200,0,824}},
};
RECT client{0,0,1600,900}; POINT origin{300,100};
int enumerations=0,names=0,originReads=0,failInfo=-1;
bool iconic=false,clientAvailable=true,originAvailable=true,moveOnConfirm=false,failEnumeration=false;
ULONGLONG ticks=0;
BOOL IsIconic(HWND){return iconic;}
BOOL GetClientRect(HWND,RECT* value){*value=client;return clientAvailable;}
BOOL ClientToScreen(HWND,POINT* value){
    if(moveOnConfirm && ++originReads%2==0){origin.x+=20;moveOnConfirm=false;}
    *value=origin;return originAvailable;
}
BOOL EqualRect(const RECT* a,const RECT* b){return a->left==b->left&&a->top==b->top&&a->right==b->right&&a->bottom==b->bottom;}
ULONGLONG GetTickCount64(){return ticks;}
BOOL GetMonitorInfoW(HMONITOR id,MONITORINFO* info){
    const auto index=static_cast<std::size_t>(id-1);
    if(static_cast<int>(index)==failInfo)return FALSE;
    const auto& monitor=desktop.at(index);info->rcMonitor=monitor.bounds;
    std::wcsncpy(info->szDevice,monitor.id.c_str(),31);return TRUE;
}
BOOL EnumDisplayDevicesW(const wchar_t* id,unsigned index,DISPLAY_DEVICEW* device,unsigned flags){
    ++names;assert(index==0&&flags==0);
    for(const auto& monitor:desktop)if(monitor.id==id){std::wcsncpy(device->DeviceString,monitor.name.c_str(),127);return TRUE;}
    return FALSE;
}
BOOL EnumDisplayMonitors(HDC,const RECT*,BOOL(*callback)(HMONITOR,HDC,LPRECT,LPARAM),LPARAM data){
    ++enumerations;if(failEnumeration)return FALSE;
    for(std::size_t i=0;i<desktop.size();++i)if(!callback(i+1,nullptr,nullptr,data))return FALSE;
    return TRUE;
}
template<class T>T ll_convert(const std::wstring& value){return T(value.begin(),value.end());}
struct WindowThread {void pingWindowTimeout(const char*){}};
struct Callbacks {
    int displays=0,dpis=0;
    bool handleDisplayChanged(){++displays;return false;}
    void handleDPIChanged(void*,F32 scale,S32 width,S32 height){++dpis;assert(scale==1.5f&&width==1200&&height==800);}
};
struct LLWindowWin32:LLWindow {
    HWND mWindowHandle=reinterpret_cast<HWND>(1);
    WindowThread thread;WindowThread* mWindowThread=&thread;
    Callbacks callbacks;Callbacks* mCallbacks=&callbacks;
    std::deque<std::function<void()>> queue;
    void post(std::function<void()> task){queue.push_back(std::move(task));}
    void drain(){assert(appThread);while(!queue.empty()){auto task=std::move(queue.front());queue.pop_front();task();}}
    void invalidateMonitorCache();
    bool getMonitorRectsInClient(std::vector<MonitorRect>&);
'''
        events = r'''
};
constexpr UINT WM_DISPLAYCHANGE=1,WM_DPICHANGED=2,WM_DEVICECHANGE=3;
constexpr WPARAM DBT_DEVNODES_CHANGED=10,DBT_CONFIGCHANGED=11;
constexpr unsigned USER_DEFAULT_SCREEN_DPI=96,SWP_NOZORDER=1,SWP_NOACTIVATE=2;
constexpr HWND HWND_TOP=nullptr;
unsigned LOWORD(WPARAM value){return static_cast<unsigned>(value&0xffff);}
BOOL SetWindowPos(HWND,HWND,int x,int y,int width,int height,unsigned){origin={x,y};client={0,0,width,height};return TRUE;}
#define WINDOW_IMP_POST(x) window_imp->post([=](){x;})
LRESULT message(LLWindowWin32* window_imp,UINT message,WPARAM w_param,LPARAM l_param){
    const HWND h_wnd=window_imp->mWindowHandle;
    switch(message){
'''
        cases = r'''
    default:break;
    }
    return 0;
}
void projected(const std::vector<LLWindow::MonitorRect>& result){
    assert(result.size()==desktop.size());
    for(std::size_t i=0;i<result.size();++i){const auto& r=result[i].rect;const auto& raw=desktop[i].bounds;
        assert(r.mLeft==raw.left-origin.x&&r.mRight==raw.right-origin.x);
        assert(r.mTop==client.bottom-client.top+origin.y-raw.top&&r.mBottom==client.bottom-client.top+origin.y-raw.bottom);
        assert(result[i].id==std::string(desktop[i].id.begin(),desktop[i].id.end()));
        assert(result[i].name==std::string(desktop[i].name.begin(),desktop[i].name.end()));
    }
}
int main(){
    LLWindowWin32 window;std::vector<LLWindow::MonitorRect> result;
    assert(window.getMonitorRectsInClient(result));projected(result);
    assert(enumerations==1&&names==2);
    // Backgrounds, recovery and Preferences share this window-layer cache.
    for(int consumer=0;consumer<3;++consumer)for(int call=0;call<100;++call){
        origin={call*10-500,100-call};client.bottom=900+call;
        assert(window.getMonitorRectsInClient(result));projected(result);
    }
    assert(enumerations==1&&names==2);

    // Native events only queue invalidation; repeated notifications coalesce.
    appThread=false;message(&window,WM_DISPLAYCHANGE,32,0);message(&window,WM_DISPLAYCHANGE,32,0);appThread=true;
    assert(!window.mMonitorCacheDirty&&enumerations==1);
    desktop.resize(1);desktop[0].name=L"Reconnected screen";desktop[0].bounds={-1920,0,0,1200};
    window.drain();assert(window.mMonitorCacheDirty&&window.callbacks.displays==2&&enumerations==1);
    assert(window.getMonitorRectsInClient(result));projected(result);assert(enumerations==2&&names==3);

    RECT dpiRect{10,20,1210,820};
    appThread=false;message(&window,WM_DPICHANGED,144,reinterpret_cast<LPARAM>(&dpiRect));appThread=true;
    assert(!window.mMonitorCacheDirty);window.drain();assert(window.callbacks.dpis==1);
    assert(window.getMonitorRectsInClient(result));projected(result);assert(enumerations==3);
    appThread=false;message(&window,WM_DEVICECHANGE,DBT_DEVNODES_CHANGED,0);appThread=true;window.drain();
    assert(window.getMonitorRectsInClient(result));assert(enumerations==4);

    // A partial failed topology stays private and retries at a bounded rate.
    window.invalidateMonitorCache();desktop.push_back({L"DISPLAY3",L"Third",{0,0,2560,1440}});failInfo=1;
    assert(!window.getMonitorRectsInClient(result)&&result.empty());assert(enumerations==5);
    assert(window.mDesktopMonitors.size()==1);
    for(int call=0;call<100;++call)assert(!window.getMonitorRectsInClient(result));
    ticks=999;assert(!window.getMonitorRectsInClient(result)&&enumerations==5);
    failInfo=-1;ticks=1000;assert(window.getMonitorRectsInClient(result));projected(result);assert(enumerations==6);
    window.invalidateMonitorCache();failEnumeration=true;
    assert(!window.getMonitorRectsInClient(result)&&result.empty());assert(enumerations==7);
    failEnumeration=false;
    appThread=false;message(&window,WM_DEVICECHANGE,DBT_CONFIGCHANGED,0);appThread=true;window.drain();
    assert(window.getMonitorRectsInClient(result));assert(enumerations==8);

    // Moving during the query cannot publish coordinates from two placements.
    originReads=0;moveOnConfirm=true;
    assert(!window.getMonitorRectsInClient(result)&&result.empty());assert(enumerations==8);
    assert(window.getMonitorRectsInClient(result));projected(result);assert(enumerations==8);
    iconic=true;assert(!window.getMonitorRectsInClient(result)&&result.empty());iconic=false;
    clientAvailable=false;assert(!window.getMonitorRectsInClient(result));clientAvailable=true;
    originAvailable=false;assert(!window.getMonitorRectsInClient(result));originAvailable=true;
    assert(enumerations==8);
    std::cout<<"300 stable/moving/resizing lookups: one enumeration; display/DPI/device changes, names, retries and racing moves passed\n";
}
'''
        self.cpp(fixture + fields + events + display + dpi + "case WM_DEVICECHANGE:{\n" +
                 device + "break;}\n" + cases + query + methods)

    def test_background_idle_only_queries_geometry_when_needed(self):
        source = (INDRA / "newview/fsworkspacebackground.cpp").read_text()
        visible = function(source, "bool workspaceVisible()")
        poll = function(source, "    void poll(bool force)")
        fixture = r'''
#include "fsworkspacebackgroundmodel.h"
#include <cassert>
#include <iostream>
#include <set>
using namespace FSWorkspaceBackground;
double timeNow=0.;bool online=true,enabled=false,rendering=true;
int queries=0,finishes=0;
struct Timer{double at=0.;float getElapsedTimeF32()const{return float(timeNow-at);}void reset(){at=timeNow;}};
struct Window{bool visible=true,minimized=false;bool getVisible(){return visible;}bool getMinimized(){return minimized;}};
struct Agent{int getID(){return 1;}int getSessionID(){return 2;}}gAgent;
struct Camera{bool cameraMouselook(){return false;}}gAgentCamera;
struct Viewer{Window window;Window* getWindow(){return &window;}Rect getWorldViewBaseRectRaw(bool){return {0,0,1920,1080};}}viewer,*gViewerWindow=&viewer;
struct Settings{bool getBOOL(const char*){return enabled;}}gSavedSettings;
struct FSSessionWorker{static bool renderAllowed(){return rendering;}};
struct LLGridManager{static auto& instance(){static LLGridManager value;return value;}std::string getGrid(){return "grid";}};
bool ready(){return online;}Rect rect(Rect value){return value;}
Rect client(){return {0,0,1920,1080};}Rect viewport(){return {0,0,960,1080};}
Choice current(){return {};}Config defaults(){return {};}
std::vector<Rect> queryDisplays(){++queries;return {client()};}
'''
        service = r'''
struct Service{
    bool stopped=false,hasExtra=false;int account=1,login=2;std::string grid="grid";
    Config config,extra;std::vector<Rect> displayGeometry;
    std::map<std::string,int> textures,errors;Timer timer,geometryTimer;
    void finishImport(){++finishes;}
    bool sourceCurrent(){return online;}
    void clear(){textures.clear();errors.clear();hasExtra=false;}
    void demand(const Image&,std::set<std::string>&){}
    std::vector<Rect> displays(){return displayGeometry;}
'''
        cases = r'''
};
void step(Service& service){timeNow+=.25;service.poll(false);}
int main(){
    Service service;
    for(int frame=0;frame<40;++frame)step(service);
    assert(queries==0&&finishes==40);
    enabled=true;viewer.window.visible=false;
    for(int frame=0;frame<40;++frame)step(service);
    viewer.window.visible=true;viewer.window.minimized=true;
    for(int frame=0;frame<40;++frame)step(service);
    viewer.window.minimized=false;rendering=false;
    for(int frame=0;frame<40;++frame)step(service);
    assert(queries==0&&finishes==160);
    rendering=true;step(service);assert(queries==1);
    enabled=false;step(service);assert(queries==1);
    service.hasExtra=true;service.poll(true);assert(queries==2);
    service.hasExtra=false;step(service);assert(queries==2);
    online=false;step(service);assert(queries==2&&service.textures.empty());
    std::cout<<"Unused/hidden/minimized/standby backgrounds: zero geometry queries; imports, visible demand and Preferences still serviced\n";
}
'''
        self.cpp(fixture + visible + service + poll + cases)

    def test_hidden_recovery_preserves_state_and_defers_queries(self):
        source = (INDRA / "newview/fsworkspacecontroller.cpp").read_text()
        body = function(source, "void FSWorkspaceController::pollDisplays()")
        prefix = body[:body.index("    std::vector<LLWindow::MonitorRect> monitors;")]
        fixture = r'''
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
bool rendering=true,enabled=true;
struct FSSessionWorker{static bool renderAllowed(){return rendering;}};
struct Settings{bool getBOOL(const char*){return enabled;}}gSavedSettings;
struct Window{bool visible=true,minimized=false;bool getVisible(){return visible;}bool getMinimized(){return minimized;}};
struct Viewer{Window window;Window* getWindow(){return &window;}}viewer,*gViewerWindow=&viewer;
struct Timer{float elapsed=3.f;float getElapsedTimeF32(){return elapsed;}void reset(){elapsed=0;}};
struct FSWorkspaceController{
    std::string mDisplaySignature="known topology";std::vector<int> mRecoveryWindows{1};bool mDisplayPending=true;
    Timer mDisplayPollTimer;int queries=0;bool available(){return true;}void pollDisplays();
};
'''
        cases = r'''
int main(){FSWorkspaceController controller;
    rendering=false;controller.pollDisplays();rendering=true;
    viewer.window.visible=false;controller.pollDisplays();viewer.window.visible=true;
    viewer.window.minimized=true;controller.pollDisplays();viewer.window.minimized=false;
    assert(controller.queries==0&&controller.mDisplaySignature=="known topology"&&controller.mRecoveryWindows.size()==1&&controller.mDisplayPending);
    controller.pollDisplays();assert(controller.queries==1);
    controller.pollDisplays();assert(controller.queries==1);
    std::cout<<"Standby recovery keeps its prior display evidence and queries once when visible again\n";
}
'''
        self.cpp(fixture + prefix + "++queries;\n}\n" + cases)

    def test_host_relays_display_changes_only_to_owned_embedded_viewers(self):
        source = (ROOT / "tools/session-host/main.cpp").read_text()
        relay = function(source, "    void relayDisplayChange(WPARAM wparam, LPARAM lparam)")
        surface = function(source, "    HWND surface() const")
        self.assertIn("case WM_DISPLAYCHANGE: host->relayDisplayChange(wparam,lparam);host->refitWindows();", source)
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
struct NativeWindow{};using HWND=NativeWindow*;using DWORD=unsigned;
using WPARAM=std::uintptr_t;using LPARAM=std::intptr_t;
constexpr unsigned WM_DISPLAYCHANGE=1;
NativeWindow windows[8];std::map<HWND,DWORD> owners;std::map<HWND,HWND> parents;
bool IsWindow(HWND window){return owners.count(window);}
DWORD GetWindowThreadProcessId(HWND window,DWORD* owner){*owner=owners[window];return *owner?1:0;}
bool IsChild(HWND root,HWND child){return parents[child]==root;}
struct Posted{HWND window;unsigned message;WPARAM wp;LPARAM lp;};std::vector<Posted> posted;
bool PostMessageW(HWND window,unsigned message,WPARAM wp,LPARAM lp){posted.push_back({window,message,wp,lp});return true;}
struct Slot{
    struct Snapshot{std::uintptr_t surface=0;}snapshot;
    DWORD pid=42;bool detached=false,alive=true;bool running()const{return alive;}
'''
        host = r'''
};
struct Host{HWND window=&windows[0];std::unique_ptr<Slot> slots[7];
'''
        cases = r'''
};
int main(){Host host;
    for(int i=0;i<6;++i){host.slots[i]=std::make_unique<Slot>();auto* handle=&windows[i+1];
        host.slots[i]->snapshot.surface=reinterpret_cast<std::uintptr_t>(handle);owners[handle]=42;parents[handle]=host.window;}
    host.slots[1]->detached=true;
    host.slots[2]->alive=false;
    owners[&windows[4]]=99;
    parents[&windows[5]]=&windows[7];
    host.relayDisplayChange(32,12345);
    assert(posted.size()==2);
    assert(posted[0].window==&windows[1]&&posted[1].window==&windows[6]);
    for(const auto& item:posted)assert(item.message==WM_DISPLAYCHANGE&&item.wp==32&&item.lp==12345);
    std::cout<<"Host asynchronously relays display events to owned, running, embedded viewer windows only\n";
}
'''
        self.cpp(fixture + surface + host + relay + cases)


if __name__ == "__main__":
    unittest.main()
