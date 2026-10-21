// Included inside Host. All dispatch and approval work executes on its UI thread.
int controllerProfile(const std::string& id) const
{for(int i=0;i<MaxCharacters;++i)if(controllerConfig.profiles[i].id==id)return i;return -1;}
fs_controller::Source controllerSource(int index) const
{
    fs_controller::Source out;if(!validSlot(index))return out;const auto& p=controllerConfig.profiles[index];out.profile=p.id;out.revision=p.revision;
    const auto* slot=slots[index].get();if(!slot)return out;
    out.running=slot->running();out.worker=utf8(hexId(slot->id));out.generation=slot->snapshot.generation;
    out.account=slot->snapshot.account;out.grid=slot->snapshot.grid;
    if(!slot->recovery.active() && slot->controllerSessionSource.owns(slot->snapshot))out.viewerSession=slot->controllerViewerSession;
    return out;
}
bool controllerOpted(int index) const
{return validSlot(index)&&controllerConfig.enabled&&controllerConfig.profiles[index].enabled;}
void controllerLoad()
{
    std::ifstream input(profiles/L"host-controller.dat",std::ios::binary);std::string data(8193,'\0');input.read(data.data(),static_cast<std::streamsize>(data.size()));data.resize(static_cast<std::size_t>(input.gcount()));
    if(!data.empty()&&!fs_controller::Config::decode(data,controllerConfig))message(L"Controller MCP settings were invalid; access remains off.");
    bool generated=false;for(auto& profile:controllerConfig.profiles)if(profile.id.empty()){profile.id=fs_controller::randomHex(16);generated=true;}
    if(generated&&!saveStore(L"host-controller.dat",controllerConfig.encode())){controllerConfig.enabled=false;message(L"Controller MCP identities could not be saved; access remains off.");}
}
void controllerApply(fs_controller::Config next)
{
    if(next.encode()==controllerConfig.encode())
    {if(next.enabled&&!controllerEndpoint.active()&&!controllerEndpoint.start())message(L"Controller MCP endpoint could not start.");return;}
    next.revision=controllerConfig.revision+1;
    for(int i=0;i<MaxCharacters;++i)if(next.profiles[i].enabled!=controllerConfig.profiles[i].enabled)++next.profiles[i].revision;
    controllerOperations.revoke("policy_changed");controllerConfig=std::move(next);
    if(controllerCycleOwned){cycler.pause();controllerCycleOwned=false;stopMonitor(CyclingView,true);}
    if(!controllerConfig.enabled)controllerEndpoint.stop();
    else if(!controllerEndpoint.running()&&!controllerEndpoint.start())message(L"Controller MCP endpoint could not start. Access was not granted to a viewer.");
}
std::string controllerProfileJSON(int index) const
{
    using fs_controller::quote;const auto source=controllerSource(index);const auto* slot=slots[index].get();
    std::string out="{\"profile_id\":"+quote(source.profile)+",\"profile_revision\":"+quote(std::to_string(source.revision))+",\"running\":"+(source.running?"true":"false");
    const bool restricted=slot&&(slot->snapshot.flags&ChatRestricted);
    if(!restricted)out+=",\"display_name\":"+quote(utf8(profileName(index)));
    if(source.running)
    {
        out+=",\"worker_id\":"+quote(source.worker)+",\"worker_generation\":"+quote(std::to_string(source.generation))+",\"viewer_session\":"+quote(source.viewerSession);
        out+=",\"state\":"+quote(slot->detached?"detached":slot->recovery.active()?"recovering":slot->snapshot.state==State::Ready?"ready":slot->snapshot.state==State::Disconnected?"disconnected":"login");
        out+=",\"active\":"+std::string(index==active?"true":"false")+",\"background_mode\":"+quote(slot->snapshot.mode==Mode::Economy?"economy":slot->snapshot.mode==Mode::Warm?"warm":"active");
        out+=",\"restart_pending\":"+std::string(slot->restart.phase()!=Restart::Phase::Idle?"true":"false")+",\"observed_age_ms\":"+std::to_string(GetTickCount64()-slot->pollAt);
        if(!restricted)out+=",\"account\":"+quote(source.account)+",\"grid\":"+quote(source.grid);
        if(slot->sampleAt && std::isfinite(slot->fps) && std::isfinite(slot->loops))out+=",\"frames_per_second\":"+std::to_string(slot->fps)+",\"maintenance_per_second\":"+std::to_string(slot->loops);
        else out+=",\"diagnostics_unavailable\":true";
        if(source.viewerSession.empty())out+=",\"control_unavailable\":\"login_reference_not_ready\"";
    }
    return out+"}";
}
std::string controllerMonitorsJSON() const
{
    using fs_controller::quote;std::string out="[";bool first=true;
    for(int i=0;i<MonitorViews;++i)
    {
        const auto& b=monitorSet.bindings[i];if(!b.enabled||!controllerOpted(b.slot)||!slots[b.slot]||!b.key.owns(slots[b.slot]->snapshot))continue;
        if(!first)out+=',';
        first=false;
        out+="{\"monitor_id\":"+std::to_string(i)+",\"profile_id\":"+quote(controllerConfig.profiles[b.slot].id)+",\"cycling\":"+(i==CyclingView?"true":"false")+",\"paused\":"+(i==CyclingView&&!cycler.playing?"true":"false")+",\"seconds\":"+std::to_string(cycler.seconds)+",\"visible\":"+(monitors[i].window&&IsWindowVisible(monitors[i].window)?"true":"false")+"}";
    }
    return out+"]";
}
std::string controllerMonitorSnapshot() const
{
    std::string out=controllerScreenStamp()+":"+std::to_string(active)+":"+std::to_string(cycleOpen)+":"+std::to_string(cycler.playing)+":"+std::to_string(cycler.seconds);
    for(const auto index:experience.order)out+=":"+std::to_string(index);
    for(int i=0;i<MonitorViews;++i)
    {
        const auto& b=monitorSet.bindings[i];
        out+="|"+std::to_string(i)+":"+std::to_string(b.enabled)+":"+std::to_string(b.slot)+":"+std::to_string(b.key.generation)+":"+b.key.account+":"+b.key.grid+":"+std::to_string(b.size)+":"+std::to_string(b.rate)+":"+std::to_string(monitors[i].dpi)+":"+std::to_string(reinterpret_cast<std::uintptr_t>(monitors[i].window))+":"+std::to_string(monitors[i].window&&IsWindowVisible(monitors[i].window));
        out+=utf8(hexId(b.key.worker));
    }
    return out;
}
bool controllerBusyFor(const fs_controller::Request& q) const
{
    if(!busy())return false;
    const int target=controllerProfile(q.profile);
    if(q.op!=fs_controller::Op::Detach||!validSlot(target)||!slots[target]||slots[target]->restart.phase()==Restart::Phase::Idle||profileSetupOpen||detaching||handoff.step()!=Handoff::Step::Idle)return true;
    for(int i=0;i<MaxCharacters;++i)if(i!=target&&slots[i]&&slots[i]->restart.phase()!=Restart::Phase::Idle&&slots[i]->running())return true;
    return false;
}
bool controllerDetachApprovalPending(int index) const
{
    for(const auto& entry:controllerOperations.receipts)
        if(entry.second.request.op==fs_controller::Op::Detach&&!entry.second.terminal()&&entry.second.request.profile==controllerConfig.profiles[index].id)return true;
    return false;
}
std::string controllerDispatch(const fs_controller::Request& request)
{
    using namespace fs_controller;
    if(!controllerConfig.enabled||request.instance!=controllerEndpoint.instance)return errorJSON("controller_changed");
    if(request.op==Op::Receipt)
    {
        auto* receipt=controllerOperations.find(request.id);if(receipt&&!receipt->terminal())controllerAllowed(*receipt);return receipt&&receipt->request.instance==request.instance?receiptJSON(*receipt,controllerEndpoint.instance):errorJSON("unknown_operation_inspect_before_retry");
    }
    if(pauseAllAssistant)return errorJSON("controller_paused");
    if(readOp(request.op))
    {
        if(controllerConfig.level(request.op)!=Level::Allow)return errorJSON("metadata_denied");
        std::string body="{\"controller_instance\":"+quote(controllerEndpoint.instance)+",\"observed_at_ms\":"+std::to_string(utcMilliseconds());
        if(request.op==Op::MonitorList)return body+",\"max_fixed_monitors\":4,\"cycling_monitor_id\":4,\"max_total_preview_fps\":10,\"monitors\":"+controllerMonitorsJSON()+"}";
        if(request.op==Op::Status)
        {
            const int index=controllerProfile(request.profile);if(!controllerOpted(index)||request.revision!=controllerConfig.profiles[index].revision)return errorJSON("profile_changed");
            return body+",\"profile\":"+controllerProfileJSON(index)+"}";
        }
        body+=",\"profiles\":[";bool first=true;for(int i=0;i<MaxCharacters;++i)if(controllerOpted(i)){if(!first)body+=',';first=false;body+=controllerProfileJSON(i);}return body+"]}";
    }
    if(auto* old=controllerOperations.find(request.id))
    {if(old->request.encode(false)!=request.encode(false))return errorJSON("request_id_conflict");return receiptJSON(*old,controllerEndpoint.instance);}
    if(controllerConfig.level(request.op)==Level::Deny)return errorJSON("permission_denied");
    if(controllerApproving||preferencesOpen||closing||detaching||controllerBusyFor(request))return errorJSON("busy");
    std::vector<Source> sources;
    const auto add=[&](int index)->bool
    {
        if(!controllerOpted(index))return false;
        const auto source=controllerSource(index);
        const bool armedDetach=request.op==Op::Detach&&slots[index]&&slots[index]->restart.phase()!=Restart::Phase::Idle;
        if(source.running&&(source.viewerSession.empty()||!slots[index]||((slots[index]->detached||!slots[index]->pipe.alive())&&!armedDetach)))return false;
        for(const auto& old:sources)if(old.profile==source.profile)return true;
        sources.push_back(source);return true;
    };
    const bool global=request.op==Op::MonitorOpenAll||request.op==Op::MonitorArrange||request.op==Op::CycleConfigure||request.op==Op::CyclePause||request.op==Op::CycleNext;
    int target=-1;
    if(!global)
    {
        target=controllerProfile(request.profile);if(!add(target))return errorJSON("profile_unavailable");const auto& source=sources.front();
        if(request.revision!=source.revision)return errorJSON("profile_changed");
        const bool armedDetach=request.op==Op::Detach&&slots[target]&&slots[target]->restart.phase()!=Restart::Phase::Idle;
        if(source.running||armedDetach)
        {if(request.worker!=source.worker||request.generation!=source.generation||request.viewerSession!=source.viewerSession)return errorJSON("session_changed");}
        else if(!request.worker.empty()||request.generation||!request.viewerSession.empty())return errorJSON("session_changed");
        if(request.op!=Op::OpenLogin&&!source.running&&!armedDetach)return errorJSON("not_running");
        if(request.op==Op::OpenLogin&&source.running)return errorJSON("already_running");
        if((request.op==Op::Activate||request.op==Op::Background||request.op==Op::MonitorOpen)&&slots[target]->snapshot.state!=State::Ready)return errorJSON("not_ready");
        if((request.op==Op::Background||request.op==Op::MonitorOpen)&&target==active)return errorJSON("requires_inactive");
        if(request.op==Op::Activate&&validSlot(active)&&!add(active))return errorJSON("current_owner_not_opted_in");
        if(request.op==Op::MonitorClose)
        {const auto& b=monitorSet.bindings[request.monitor];if(!b.enabled||b.slot!=target||!b.key.owns(slots[target]->snapshot))return errorJSON("monitor_changed");}
    }
    else
    {
        if(!request.profile.empty()||request.revision||!request.worker.empty()||request.generation||!request.viewerSession.empty())return errorJSON("invalid_global_target");
        if(request.op==Op::MonitorOpenAll||request.op==Op::CycleConfigure||request.op==Op::CycleNext)
        {for(int i=0;i<MaxCharacters;++i)if(monitorEligible(i)&&controllerOpted(i)&&!add(i))return errorJSON("session_changed");}
        if(request.op==Op::MonitorArrange&&cycleOpen&&!monitorSet.bindings[CyclingView].enabled&&monitors[CyclingView].window&&IsWindowVisible(monitors[CyclingView].window))return errorJSON("monitor_unbound");
        if(request.op==Op::MonitorArrange||request.op==Op::CyclePause||request.op==Op::CycleNext)
        {for(int i=0;i<MonitorViews;++i){const auto& b=monitorSet.bindings[i];if(b.enabled&&!add(b.slot))return errorJSON("monitor_profile_not_opted_in");}}
        if(sources.empty())return errorJSON("no_eligible_sources");
    }
    std::string error;auto* receipt=controllerOperations.admit(request,sources,controllerConfig.revision,utcMilliseconds(),error,GetTickCount64());
    if(!receipt)return errorJSON(error);
    if(permission(request.op)==Permission::Monitors)receipt->nativeSnapshot=controllerMonitorSnapshot();
    if(request.op==Op::CycleNext)
    {
        const int next=cycler.next(monitorSet.bindings[CyclingView].slot,1,experience.order,[this](int i){return i!=active&&controllerOpted(i)&&contactsLive(i)&&!(slots[i]->snapshot.flags&(PreviewUnavailable|Promoting));});
        if(next<0){receipt->state="refused";receipt->error="no_eligible_sources";return receiptJSON(*receipt,controllerEndpoint.instance);}
        receipt->selectedTarget=controllerConfig.profiles[next].id;
    }
    receipt->state=controllerConfig.level(request.op)==Level::Ask?"pending_approval":"queued";
    return receiptJSON(*receipt,controllerEndpoint.instance);
}
bool controllerAllowed(fs_controller::Receipt& receipt,bool allowExited=false)
{
    if(pauseAllAssistant){receipt.state=receipt.submitted?"uncertain":"cancelled";receipt.error="controller_paused";return false;}
    const int index=controllerProfile(receipt.request.profile);
    if((receipt.request.op==fs_controller::Op::Restart||receipt.request.op==fs_controller::Op::Detach)&&validSlot(index)&&slots[index]&&slots[index]->restart.phase()!=Restart::Phase::Idle)allowExited=true;
    return controllerOperations.allowed(receipt,controllerConfig,GetTickCount64(),[this](const std::string& profile){return controllerSource(controllerProfile(profile));},allowExited);
}
bool controllerTransportAllowed(int index,Kind kind)
{
    if(!validSlot(index))return false;
    if(!controllerHandoffRequest.empty()&&kind==Kind::SetMode&&handoff.step()!=Handoff::Step::Idle&&handoff.step()!=Handoff::Step::Rollback)
    {
        auto* receipt=controllerOperations.find(controllerHandoffRequest);
        if(!receipt||!controllerAllowed(*receipt)){controllerHandoffRequest.clear();failHandoff(index,L"Controller permission or source changed; switching cancelled through native recovery.");return false;}
    }
    const auto& id=controllerNativeRequests[index];if(id.empty())return true;
    auto* receipt=controllerOperations.find(id);if(!receipt)return false;
    if(receipt->request.op==fs_controller::Op::Background&&kind==Kind::SetMode&&(handoff.step()!=Handoff::Step::Idle||index==active))
    {receipt->state="cancelled";receipt->error="manual_handoff";controllerNativeRequests[index].clear();return true;}
    if((receipt->request.op==fs_controller::Op::Restart&&kind==Kind::Quit)||(receipt->request.op==fs_controller::Op::Background&&kind==Kind::SetMode))
    {
        if(!controllerAllowed(*receipt)){if(receipt->request.op==fs_controller::Op::Restart)slots[index]->restart.cancel();else if(!receipt->submitted&&standby[index]==(receipt->request.mode?Mode::Economy:Mode::Warm))standby[index]=slots[index]->snapshot.mode;controllerNativeRequests[index].clear();return false;}
        if(receipt->request.op==fs_controller::Op::Background&&standby[index]!=(receipt->request.mode?Mode::Economy:Mode::Warm))
        {receipt->state="cancelled";receipt->error="manual_mode_change";controllerNativeRequests[index].clear();return false;}
    }
    return true;
}
void controllerTransportSubmitted(int index,Kind kind)
{
    if(kind==Kind::SetMode&&!controllerHandoffRequest.empty())if(auto* r=controllerOperations.find(controllerHandoffRequest))r->submitted=true;
    if(validSlot(index)&&!controllerNativeRequests[index].empty())if(auto* r=controllerOperations.find(controllerNativeRequests[index]))
        if((r->request.op==fs_controller::Op::Restart&&kind==Kind::Quit)||(r->request.op==fs_controller::Op::Background&&kind==Kind::SetMode))r->submitted=true;
}
void controllerReply(int index,const Message& request,const Message& response)
{
    if(!controllerHandoffRequest.empty()&&request.kind==Kind::SetMode&&handoff.step()==Handoff::Step::Idle)
    {if(auto* r=controllerOperations.find(controllerHandoffRequest)){const bool success=!(response.flags&Error)&&active==controllerProfile(r->request.profile);r->state=success?"completed":"uncertain";r->error=success?"":"native_handoff_failed";}controllerHandoffRequest.clear();}
    const auto id=controllerNativeRequests[index];if(id.empty())return;auto* r=controllerOperations.find(id);if(!r)return;
    const bool matching=(r->request.op==fs_controller::Op::Background&&request.kind==Kind::SetMode)||((r->request.op==fs_controller::Op::Restart||r->request.op==fs_controller::Op::Logout)&&request.kind==Kind::Quit)||(r->request.op==fs_controller::Op::Detach&&request.kind==Kind::Detach);
    if(!matching)return;
    if(response.flags&Error){r->state="refused";r->error="native_refused";controllerNativeRequests[index].clear();}
    else if(r->request.op!=fs_controller::Op::Restart){r->state=r->request.op==fs_controller::Op::Background?"completed":"submitted";controllerNativeRequests[index].clear();}
}
bool controllerRestartLaunch(int index)
{
    const auto id=controllerNativeRequests[index];if(id.empty())return true;auto* r=controllerOperations.find(id);
    if(!r||r->request.op!=fs_controller::Op::Restart||!controllerAllowed(*r,true)){controllerNativeRequests[index].clear();return false;}
    return true;
}
void controllerExecute(fs_controller::Receipt& r)
{
    using namespace fs_controller;const auto& q=r.request;const int index=controllerProfile(q.profile);
    if(!controllerAllowed(r))return;
    if(preferencesOpen||closing||detaching||controllerBusyFor(q))return;
    if(validSlot(index)&&slots[index]&&(slots[index]->waiting||slots[index]->pipe.writing()))return;
    if(validSlot(index)&&slots[index]&&slots[index]->recovery.active())return;
    const auto fail=[&](const char* error){r.state="refused";r.error=error;};
    if(permission(q.op)==Permission::Monitors&&r.nativeSnapshot!=controllerMonitorSnapshot()){fail("monitor_changed");return;}
    if(validSlot(index)&&(q.op==Op::Activate||q.op==Op::Background||q.op==Op::MonitorOpen)&&slots[index]->snapshot.state!=State::Ready){fail("not_ready");return;}
    if(validSlot(index)&&(q.op==Op::Background||q.op==Op::MonitorOpen)&&index==active){fail("requires_inactive");return;}
    if(q.op==Op::MonitorClose)
    {const auto& b=monitorSet.bindings[q.monitor];if(!b.enabled||b.slot!=index||!b.key.owns(slots[index]->snapshot)){fail("monitor_changed");return;}}
    switch(q.op)
    {
    case Op::Activate:
        if(index==active){r.state="completed";return;}controllerHandoffRequest=q.id;switchTo(index);
        if(handoff.step()==Handoff::Step::Idle){controllerHandoffRequest.clear();fail("native_handoff_unavailable");return;}r.state="waiting_handoff";return;
    case Op::Background:
        if(index==active){fail("requires_inactive");return;}
        standby[index]=q.mode?Mode::Economy:Mode::Warm;SendMessageW(standbyControl[index],CB_SETCURSEL,q.mode?1:0,0);
        if(slots[index]->snapshot.mode==standby[index]){r.state="completed";return;}controllerNativeRequests[index]=q.id;r.state="waiting_native";return;
    case Op::OpenLogin:
        launch(index,{},q.id);if(slots[index]&&slots[index]->running()){r.submitted=true;r.state="completed";}else fail("native_login_cancelled_or_unavailable");return;
    case Op::Restart:
        restartCharacter(index);if(slots[index]->restart.phase()==Restart::Phase::Idle){fail("native_restart_unavailable");return;}controllerNativeRequests[index]=q.id;r.state="waiting_native_close";return;
    case Op::Logout:case Op::Detach:
    {
        auto& slot=*slots[index];
        const bool cancellingRestart=q.op==Op::Detach&&slot.restart.phase()!=Restart::Phase::Idle;
        if(cancellingRestart)
        {
            slot.restart.cancel();
            if(auto* previous=controllerOperations.find(controllerNativeRequests[index]))if(previous!=&r){previous->state=previous->submitted?"uncertain":"cancelled";previous->error="explicit_detach";}
            controllerNativeRequests[index].clear();
            if(!slot.running()){r.state="completed";r.submitted=true;r.error="relaunch_cancelled_viewer_already_exited";return;}
            if(!slot.pipe.alive()||slot.detached){r.state="uncertain";r.submitted=true;r.error="relaunch_cancelled_native_close_already_submitted";return;}
        }
        slot.restart.cancel();slot.hasPendingSend=slot.hasPendingTyping=slot.hasPendingAction=slot.hasPendingReview=false;
        const auto kind=q.op==Op::Logout?Kind::Quit:Kind::Detach;const auto bound=slot.snapshot;
        if(!send(index,kind,Mode::Warm,0,q.op==Op::Logout?&bound:nullptr)){fail("native_submission_unavailable");return;}
        r.submitted=true;r.state="waiting_native";controllerNativeRequests[index]=q.id;return;
    }
    case Op::MonitorOpen:
        openMonitor(index,false,true);if(monitorSet.find(index)<0){fail("monitor_unavailable");return;}break;
    case Op::MonitorClose:stopMonitor(static_cast<int>(q.monitor),true);break;
    case Op::MonitorOpenAll:
    {
        auto staged=monitorSet;
        for(const auto& source:r.sources){const int target=controllerProfile(source.profile);if(!monitorEligible(target)){fail("source_changed");return;}if(staged.open(target,slots[target]->snapshot,monitorSize,monitorRate)<0){fail("monitor_capacity");return;}}
        for(const auto& source:r.sources)
        {
            const int target=controllerProfile(source.profile);if(!controllerAllowed(r))return;openMonitor(target,false,true);
            if(monitorSet.find(target)<0){r.state=r.submitted?"uncertain":"refused";r.error="native_monitor_partial_or_unavailable";return;}r.submitted=true;
        }
        break;
    }
    case Op::MonitorArrange:
    {RECT rect{};GetWindowRect(window,&rect);if(!arrangeMonitors({rect.left+(rect.right-rect.left)/2,rect.top+(rect.bottom-rect.top)/2},q.row)){fail("layout_unavailable_or_too_small");return;}break;}
    case Op::CycleConfigure:
    {
        const int target=controllerProfile(r.sources.front().profile);controllerCycleOwned=true;controllerCycleSources=r.sources;controllerCyclePolicy=controllerConfig.revision;
        experience.cycleSeconds=q.seconds;cycler.seconds=q.seconds;cycleOpen=true;openMonitor(target,true,true);if(!monitorSet.bindings[CyclingView].enabled){fail("cycling_monitor_unavailable");return;}
        if(q.playing)cycler.play(GetTickCount64());else cycler.pause();refreshMonitorChat(CyclingView);break;
    }
    case Op::CyclePause:cycler.pause();refreshMonitorChat(CyclingView);break;
    case Op::CycleNext:
    {cycler.pause();controllerCycleOwned=true;controllerCycleSources=r.sources;controllerCyclePolicy=controllerConfig.revision;const int next=controllerProfile(r.selectedTarget);if(next<0||!cycleEligible(next)){fail("no_eligible_sources");return;}if(!cycleOpen){cycleOpen=true;openMonitor(next,true,true);}else selectCycle(next);refreshMonitorChat(CyclingView);break;}
    default:fail("unsupported");return;
    }
    r.submitted=true;r.state="completed";
}
void controllerTick()
{
    if(controllerEndpoint.active())controllerEndpoint.tick([this](const fs_controller::Request& q){return controllerDispatch(q);});
    if(controllerApproving)return;
    if(controllerCycleOwned&&(!controllerConfig.enabled||controllerCyclePolicy!=controllerConfig.revision||pauseAllAssistant))
    {cycler.pause();stopMonitor(CyclingView,true);controllerCycleOwned=false;}
    for(auto& id:controllerNativeRequests)if(!id.empty())
    {
        auto* r=controllerOperations.find(id);if(!r||r->terminal())
        {
            const int index=static_cast<int>(&id-controllerNativeRequests.data());
            if(r&&slots[index]&&r->request.op==fs_controller::Op::Restart)slots[index]->restart.cancel();
            if(r&&!r->submitted&&slots[index]&&r->request.op==fs_controller::Op::Background&&standby[index]==(r->request.mode?Mode::Economy:Mode::Warm))standby[index]=slots[index]->snapshot.mode;
            id.clear();
        }
    }
    for(auto& entry:controllerOperations.receipts)
    {
        auto& receipt=entry.second;if(receipt.terminal())continue;if(!controllerAllowed(receipt))continue;
        if(receipt.state=="pending_approval")
        {
            if(preferencesOpen||controllerBusyFor(receipt.request)||menuOpen||closing||detaching)return;
            DWORD owner=0;GetWindowThreadProcessId(GetForegroundWindow(),&owner);if(owner!=GetCurrentProcessId())return;
            std::wstring summary=L"Allow "+wide(fs_controller::opName(receipt.request.op))+L"?\n\n";
            for(const auto& source:receipt.sources){const int index=controllerProfile(source.profile);summary+=profileName(index)+L"\nProfile "+wide(source.profile)+L"\n"+wide(source.account)+L" · "+wide(source.grid)+L"\n";}
            if(receipt.request.op==fs_controller::Op::Restart)summary+=L"\nNative close, release the profile, then open login. This does not log in automatically.";
            if(receipt.request.op==fs_controller::Op::MonitorArrange)summary+=receipt.request.row?L"\nArrange existing previews in a row.":L"\nArrange existing previews in a grid.";
            if(receipt.request.op==fs_controller::Op::MonitorClose)summary+=L"\nClose monitor "+std::to_wstring(receipt.request.monitor)+L".";
            if(receipt.request.op==fs_controller::Op::CycleNext)summary+=L"\nNext preview: "+profileName(controllerProfile(receipt.selectedTarget))+L" · "+wide(receipt.selectedTarget);
            if(receipt.request.op==fs_controller::Op::Background)summary+=receipt.request.mode?L"\nSet inactive worker to Economy.":L"\nSet inactive worker to Warm.";
            if(receipt.request.op==fs_controller::Op::CycleConfigure)summary+=L"\nCycle interval: "+std::to_wstring(receipt.request.seconds)+L" seconds; "+(receipt.request.playing?L"playing":L"paused");
            summary+=L"\nThis approval expires after 70 seconds. Changed sources or permissions require a fresh request.";
            controllerApproving=true;const int answer=MessageBoxW(window,summary.c_str(),L"Controller MCP request",MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2);controllerApproving=false;
            if(answer!=IDYES){receipt.state="refused";receipt.error="user_denied";return;}
            if(!controllerAllowed(receipt))return;
            receipt.state="queued";
        }
        if(receipt.state=="queued"){controllerExecute(receipt);return;}
    }
    if(controllerCycleOwned)
    {
        if(!controllerConfig.enabled||controllerCyclePolicy!=controllerConfig.revision||pauseAllAssistant){cycler.pause();stopMonitor(CyclingView,true);controllerCycleOwned=false;}
    }
}
