/** SPDX-License-Identifier: LGPL-2.1-or-later */
// Native monitor entry points shared with the portable production-adapter test.
// A controller-owned cycle restricts only that cycle's ongoing source selection.
bool monitorEligible(int index) const
{return index!=active&&contactsLive(index)&&!(slots[index]->snapshot.flags&(PreviewUnavailable|Promoting));}
bool cycleEligible(int index) const
{
    if(!monitorEligible(index))return false;
    if(!controllerCycleOwned)return true;
    if(!controllerConfig.enabled||controllerCyclePolicy!=controllerConfig.revision||pauseAllAssistant||!controllerOpted(index))return false;
    const auto source=controllerSource(index);for(const auto& allowed:controllerCycleSources)if(allowed==source)return true;return false;
}
void releaseControllerCycle()
{controllerCycleOwned=false;controllerCycleSources.clear();controllerCyclePolicy=0;}
void openCycler()
{
    if(busy())return;
    releaseControllerCycle();
    const int target=cycler.next(-1,1,experience.order,[this](int i){return cycleEligible(i);});
    if(target<0){message(L"Log in an inactive character before opening a cycling monitor.");return;}
    cycleOpen=true;cycler.seconds=experience.cycleSeconds;cycler.play(GetTickCount64());openMonitor(target,true);
}
void openAllMonitors()
{for(const auto i:experience.order)if(monitorEligible(static_cast<int>(i)))openMonitor(static_cast<int>(i));}
void stopMonitor(int id,bool hide=false)
{
    if(id<0||id>=MonitorViews)return;
    if(id==CyclingView){cycleOpen=false;cycler.pause();releaseControllerCycle();}
    monitorSet.stop(id);clearMonitor(id);monitors[id].dirty=true;
    refreshMonitorChat(id);
    if(hide&&monitors[id].window)ShowWindow(monitors[id].window,SW_HIDE);
}
