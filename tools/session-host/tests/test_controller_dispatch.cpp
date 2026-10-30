/** SPDX-License-Identifier: LGPL-2.1-or-later */
// Compile the production UI adapter against bounded native substitutes. This
// exercises real dispatch/preparation/revalidation, not a second policy model.
#include "../controller_model.h"
#include "fssessionconnection.h"
#include "fssessionmonitortools.h"
#include "fssessionrestart.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
using namespace fs_session;
using HWND=void*;using DWORD=unsigned long;using Kind=fs_session::Kind;
struct RECT{long left=0,top=0,right=800,bottom=600;};struct POINT{long x=0,y=0;};
constexpr int CB_SETCURSEL=1,MB_YESNO=2,MB_ICONQUESTION=4,MB_DEFBUTTON2=8,IDYES=6,SW_HIDE=0;
static std::function<int()> approval;
static std::uint64_t now=1800000000000ULL;
inline std::uint64_t GetTickCount64(){return now;}
inline DWORD GetCurrentProcessId(){return 1;}
inline HWND GetForegroundWindow(){return reinterpret_cast<HWND>(1);}
inline void GetWindowThreadProcessId(HWND,DWORD* owner){*owner=1;}
inline bool IsWindowVisible(HWND h){return h!=nullptr;}
inline void ShowWindow(HWND,int){}
inline int SendMessageW(HWND,int,int,int){return 0;}
inline int GetWindowRect(HWND,RECT*){return 1;}
inline int MessageBoxW(HWND,const wchar_t*,const wchar_t*,int){return approval?approval():IDYES;}
std::wstring wide(const std::string& value){return {value.begin(),value.end()};}
std::wstring hexId(const WorkerId& id){std::wstring out;const wchar_t* h=L"0123456789abcdef";for(auto b:id){out+=h[b>>4];out+=h[b&15];}return out;}
namespace fs_controller {
inline std::uint64_t utcMilliseconds(){return now;}
inline std::string randomHex(std::size_t n){return std::string(n*2,'a');}
struct Endpoint
{
    std::string instance=std::string(32,'a');bool enabled=true;
    bool active()const{return enabled;}bool running()const{return enabled;}bool start(){enabled=true;return true;}void stop(){enabled=false;}
    template<class F> void tick(F){}
};
}
struct ProfileCopyRequest {};
struct Pipe {bool live=true;bool alive()const{return live;}bool writing()const{return false;}};
struct Slot
{
    LinkRecovery recovery;
    Message snapshot;WorkerId id{};Pipe pipe;Restart restart;
    std::string controllerViewerSession;ChatKey controllerSessionSource;
    bool detached=false,waiting=false,hasPendingSend=false,hasPendingTyping=false,hasPendingAction=false,hasPendingReview=false,alive=true;
    std::uint64_t pollAt=now,sampleAt=0;double fps=0,loops=0;
    bool running()const{return alive;}
};
struct Monitor{HWND window=nullptr;unsigned int dpi=96;bool dirty=false;};
struct Host
{
    fs_controller::Config controllerConfig;fs_controller::Operations controllerOperations;fs_controller::Endpoint controllerEndpoint;
    std::array<std::unique_ptr<Slot>,MaxCharacters> slots{};
    std::array<std::string,MaxCharacters> controllerNativeRequests{};std::string controllerHandoffRequest;
    std::vector<fs_controller::Source> controllerCycleSources;std::uint64_t controllerCyclePolicy=0;
    bool controllerApproving=false,controllerCycleOwned=false,cycleOpen=false,pauseAllAssistant=false,preferencesOpen=false,closing=false,detaching=false,profileSetupOpen=false,menuOpen=false;
    int active=-1;unsigned int monitorSize=0,monitorRate=1;HWND window=reinterpret_cast<HWND>(1);
    std::array<HWND,MaxCharacters> standbyControl{};std::array<Mode,MaxCharacters> standby=warmModes();
    std::filesystem::path profiles;MonitorSet monitorSet;CyclingMonitor cycler;std::array<Monitor,MonitorViews> monitors{};Handoff handoff;
    struct {unsigned int cycleSeconds=10;std::array<unsigned int,MaxCharacters> order{{0,1,2,3,4}};}experience;
    int mutations=0,moves=0;
    std::string utf8(const std::wstring& value)const{return {value.begin(),value.end()};}
    std::wstring profileName(int index)const{return L"Profile "+std::to_wstring(index);}
    void message(const std::wstring&){}bool saveStore(const wchar_t*,const std::string&){return true;}
    bool busy()const{if(profileSetupOpen||detaching||handoff.step()!=Handoff::Step::Idle)return true;for(const auto& s:slots)if(s&&s->running()&&s->restart.phase()!=Restart::Phase::Idle)return true;return false;}
    bool contactsLive(int index)const{return validSlot(index)&&slots[index]&&slots[index]->running()&&slots[index]->pipe.alive()&&!slots[index]->detached&&slots[index]->snapshot.state==State::Ready;}
    void failHandoff(int,const std::wstring&){handoff.cancel();}
    void switchTo(int index){handoff.begin(active,index,active>=0?slots[active]->snapshot.generation:0,slots[index]->snapshot.generation);}
    void launch(int,const struct ProfileCopyRequest& ={},const std::string& ={}){}
    void restartCharacter(int index){slots[index]->restart.begin(slots[index]->snapshot,now);}
    void openMonitor(int index,bool cycle=false,bool=false){++mutations;if(cycle)monitorSet.cycle(index,slots[index]->snapshot,monitorSize,monitorRate);else monitorSet.open(index,slots[index]->snapshot,monitorSize,monitorRate);}
    void refreshMonitorChat(int){}void selectCycle(int index){monitorSet.cycle(index,slots[index]->snapshot,monitorSize,monitorRate);}
    bool arrangeMonitors(POINT,bool){++moves;return true;}
    bool send(int index,Kind kind,Mode=Mode::Warm,std::uint64_t=0,const Message* =nullptr)
    {if(!controllerTransportAllowed(index,kind))return false;++mutations;controllerTransportSubmitted(index,kind);return true;}
    std::string controllerScreenStamp()const{return "screen-1";}
    void clearMonitor(int){}
    #include "../monitor_control.inc"
    #include "../controller_host.inc"
};
int main()
{
    using namespace fs_controller;Host h;h.controllerConfig.enabled=true;h.controllerConfig.levels[0]=Level::Allow;
    for(int i=0;i<MaxCharacters;++i){auto& p=h.controllerConfig.profiles[i];p.id=std::string(31,'b')+static_cast<char>('0'+i);p.enabled=true;}
    auto slot=std::make_unique<Slot>();slot->id[0]=42;slot->snapshot.worker=slot->id;slot->snapshot.generation=9;slot->snapshot.pid=3;slot->snapshot.account="account";slot->snapshot.grid="grid";slot->snapshot.state=State::Ready;
    slot->controllerViewerSession="opaque";slot->controllerSessionSource=monitorIdentity(slot->snapshot);h.slots[0]=std::move(slot);
    Request q;q.instance=h.controllerEndpoint.instance;q.id=std::to_string(now)+"-"+std::string(32,'c');q.op=Op::Background;q.profile=h.controllerConfig.profiles[0].id;q.revision=1;q.worker=h.controllerSource(0).worker;q.generation=9;q.viewerSession="opaque";q.mode=1;
    const auto pending=h.controllerDispatch(q);assert(pending.find("pending_approval")!=std::string::npos&&h.mutations==0);
    approval=[&](){++h.controllerConfig.revision;return IDYES;};h.controllerTick();auto* r=h.controllerOperations.find(q.id);assert(r&&r->state=="cancelled"&&h.mutations==0);approval={};
    q.id=std::to_string(now)+"-"+std::string(32,'d');h.controllerDispatch(q);h.controllerTick();r=h.controllerOperations.find(q.id);assert(r&&r->state=="waiting_native"&&h.mutations==0);
    ++h.controllerConfig.revision;assert(!h.controllerTransportAllowed(0,Kind::SetMode)&&h.mutations==0&&r->state=="cancelled");
    Request wrong=q;wrong.id=std::to_string(now)+"-"+std::string(32,'0');wrong.viewerSession="replacement";assert(h.controllerDispatch(wrong).find("session_changed")!=std::string::npos);
    // Rebinding a monitor while Ask is open cannot close the replacement.
    h.monitorSet.open(0,h.slots[0]->snapshot,0,1);q.op=Op::MonitorClose;q.id=std::to_string(now)+"-"+std::string(32,'e');q.monitor=0;h.controllerDispatch(q);h.monitorSet.stop(0);h.controllerTick();r=h.controllerOperations.find(q.id);assert(r&&r->state=="refused"&&r->error=="monitor_changed");
    // An arrangement cannot include a monitor added after its approval snapshot.
    h.monitorSet.open(0,h.slots[0]->snapshot,0,1);q.op=Op::MonitorArrange;q.profile.clear();q.revision=0;q.worker.clear();q.generation=0;q.viewerSession.clear();q.id=std::to_string(now)+"-"+std::string(32,'f');h.controllerDispatch(q);h.monitors[0].window=reinterpret_cast<HWND>(2);h.controllerTick();r=h.controllerOperations.find(q.id);assert(r&&r->state=="refused"&&h.moves==0);
    // A restart's retained authority survives the expected process exit, and
    // an explicitly approved detach cancels its armed relaunch on that source.
    q.op=Op::Restart;q.profile=h.controllerConfig.profiles[0].id;q.revision=1;q.worker=h.controllerSource(0).worker;q.generation=9;q.viewerSession="opaque";q.id=std::to_string(now)+"-"+std::string(32,'1');
    h.controllerDispatch(q);h.controllerTick();auto* restart=h.controllerOperations.find(q.id);assert(restart&&restart->state=="waiting_native_close");
    h.slots[0]->alive=false;assert(h.controllerAllowed(*restart));
    q.op=Op::Detach;q.id=std::to_string(now)+"-"+std::string(32,'2');h.controllerDispatch(q);assert(h.controllerDetachApprovalPending(0));h.controllerTick();
    r=h.controllerOperations.find(q.id);assert(r&&r->state=="completed"&&h.slots[0]->restart.phase()==Restart::Phase::Idle&&restart->state=="cancelled");
    const auto before=h.mutations;const auto duplicate=h.controllerDispatch(q);h.controllerTick();assert(duplicate.find("completed")!=std::string::npos&&h.mutations==before);
    h.controllerConfig.levels[0]=Level::Deny;Request list;list.op=Op::List;list.instance=h.controllerEndpoint.instance;assert(h.controllerDispatch(list).find("metadata_denied")!=std::string::npos);
    // Use the production native entry points: frozen controller-cycle sources
    // must not narrow a fresh fixed-monitor operation after another login.
    Host previews;previews.controllerConfig.enabled=true;
    previews.controllerConfig.levels[static_cast<unsigned int>(Permission::Monitors)]=Level::Allow;
    for(int i=0;i<MaxCharacters;++i){auto& p=previews.controllerConfig.profiles[i];p.id=std::string(31,'e')+static_cast<char>('0'+i);p.enabled=true;}
    const auto readySlot=[](unsigned char worker)
    {
        auto s=std::make_unique<Slot>();s->id[0]=worker;s->snapshot.worker=s->id;s->snapshot.generation=1;s->snapshot.pid=worker;
        s->snapshot.account="account-"+std::to_string(worker);s->snapshot.grid="grid";s->snapshot.state=State::Ready;
        s->controllerViewerSession="login-"+std::to_string(worker);s->controllerSessionSource=monitorIdentity(s->snapshot);return s;
    };
    previews.slots[0]=readySlot(51);
    Request cycle;cycle.instance=previews.controllerEndpoint.instance;cycle.op=Op::CycleConfigure;cycle.id=std::to_string(now)+"-"+std::string(32,'3');
    previews.controllerDispatch(cycle);previews.controllerTick();
    assert(previews.controllerOperations.find(cycle.id)->state=="completed"&&previews.controllerCycleOwned&&previews.controllerCycleSources.size()==1);
    previews.slots[1]=readySlot(52);
    assert(previews.monitorEligible(1)&&!previews.cycleEligible(1)); // continuation stays frozen
    previews.openAllMonitors(); // native Open All includes the newly ready login
    assert(previews.monitorSet.find(0)>=0&&previews.monitorSet.find(1)>=0&&previews.controllerCycleOwned);
    previews.stopMonitor(previews.monitorSet.find(1));assert(previews.controllerCycleOwned);
    Request all;all.instance=cycle.instance;all.op=Op::MonitorOpenAll;all.id=std::to_string(now)+"-"+std::string(32,'4');
    previews.controllerDispatch(all);previews.controllerTick();
    assert(previews.controllerOperations.find(all.id)->state=="completed"&&previews.monitorSet.find(1)>=0);
    assert(previews.controllerCycleOwned&&!previews.cycleEligible(1)); // fresh fixed previews grant no cycle authority
    previews.slots[0]->alive=false;previews.stopMonitor(CyclingView,true);
    assert(!previews.controllerCycleOwned&&previews.controllerCycleSources.empty()&&!previews.controllerCyclePolicy);
    previews.openCycler();assert(previews.monitorSet.bindings[CyclingView].slot==1&&!previews.controllerCycleOwned);
    // Native Open Cycling also takes ownership without an intervening close.
    previews.stopMonitor(CyclingView);previews.slots[0]->alive=true;previews.slots[1]->snapshot.state=State::Disconnected;
    cycle.id=std::to_string(now)+"-"+std::string(32,'5');previews.controllerDispatch(cycle);previews.controllerTick();
    assert(previews.controllerOperations.find(cycle.id)->state=="completed"&&previews.controllerCycleOwned);
    previews.slots[0]->alive=false;previews.slots[1]->snapshot.state=State::Ready;
    assert(!previews.cycleEligible(1));previews.openCycler();
    assert(!previews.controllerCycleOwned&&previews.controllerCycleSources.empty()&&previews.monitorSet.bindings[CyclingView].slot==1);
}
