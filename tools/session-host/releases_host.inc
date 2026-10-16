// Included inside Host. Network work owns values only, never HWNDs or character data.
fs_release::WindowsChecker releaseChecker;
bool releaseStarted=false,releaseShowing=false;
std::optional<fs_release::Result> releaseReady;
std::wstring releaseStatus=L"Check published Folderstorm releases.";
void checkUpdates()
{releaseChecker.start(true);message(L"Checking Folderstorm releases…");}
void pollUpdates()
{
    if(!releaseStarted){releaseStarted=true;releaseChecker.start(false);}
    if(auto next=releaseChecker.poll())
    {
        const auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);
        if(prefs.channel!=releaseChecker.channel){const bool requested=releaseChecker.manual;releaseChecker.manual=false;if(requested)checkUpdates();return;}
        if(next->error.empty() && !next->release.version.empty())*next=fs_release::choose({next->release},fs_release::runningBuild(),prefs,releaseChecker.manual,fs_release::DownloadHosts);
        const bool show=releaseChecker.manual || next->state==fs_release::State::Available || next->state==fs_release::State::NoArtifact;
        releaseChecker.manual=false;
        releaseStatus=!next->error.empty()?wide(next->error):next->state==fs_release::State::NoReleases?L"No Folderstorm releases have been published yet.":
            next->state==fs_release::State::Current?L"You're up to date for this channel.":
            next->state==fs_release::State::Available?L"Folderstorm "+wide(next->release.version)+L" is available.":
            L"A newer release has no matching download for this installation.";
        if(show)releaseReady=std::move(*next);
    }
    if(!releaseReady || preferencesOpen || releaseShowing || closing)return;
    auto result=std::move(*releaseReady);releaseReady.reset();releaseShowing=true;
    const auto open=[&](const std::string& url){if(fs_release::trustedUrl(url,fs_release::DownloadHosts))ShellExecuteW(window,L"open",wide(url).c_str(),nullptr,nullptr,SW_SHOWNORMAL);};
    const auto title=L"Folderstorm updates · installed "+wide(fs_release::runningBuild().version);
    const auto notes=wide(fs_release::plainText(result.release.notes,4000));
    std::vector<TASKDIALOG_BUTTON> buttons;
    if(result.artifact && result.error.empty())buttons.push_back({100,L"Download"});
    buttons.push_back({101,L"What's new / all releases"});
    if(result.state==fs_release::State::Available || result.state==fs_release::State::NoArtifact)buttons.push_back({102,L"Skip this version"});
    buttons.push_back({IDCANCEL,L"Later"});
    TASKDIALOGCONFIG dialog{};dialog.cbSize=sizeof(dialog);dialog.hwndParent=window;dialog.pszWindowTitle=title.c_str();
    dialog.pszMainInstruction=releaseStatus.c_str();dialog.pszContent=notes.c_str();dialog.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION|TDF_SIZE_TO_CONTENT;
    dialog.cButtons=static_cast<UINT>(buttons.size());dialog.pButtons=buttons.data();dialog.nDefaultButton=IDCANCEL;
    int selected=IDCANCEL;
    if(FAILED(TaskDialogIndirect(&dialog,&selected,nullptr,nullptr)))
        selected=MessageBoxW(window,(releaseStatus+L"\n\nOpen the releases page?").c_str(),title.c_str(),MB_YESNO)==IDYES?101:IDCANCEL;
    if(selected==100 && result.artifact && result.error.empty())open(result.artifact->url);
    if(selected==101)open(result.release.url.empty()?fs_release::ReleasesPage:result.release.url);
    if(selected==102)
    {
        auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);
        if(fs_release::Version::parse(result.release.version)){prefs.skipped=result.release.version;if(!fs_release::savePreferences(prefs))message(L"The skipped version could not be saved.");}
    }
    releaseShowing=false;
}
