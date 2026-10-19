/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fscrash.h"
#include "fscrashconfig.h"
#include "fsreleasebuild.h"
#include "fsreleaseprefs.h"
#include <array>
#include <iomanip>
#include <random>
#include <memory>
#if defined(_WIN32)
#include <commctrl.h>
#endif
#if FS_HAS_SENTRY
#include <sentry.h>
#endif
namespace fs_crash
{
namespace
{
bool active=false,started=false;
Consent observed;
std::string processRole="viewer",failure;
std::filesystem::path database;
std::chrono::steady_clock::time_point lastPoll{};
std::filesystem::path root()
{
    const auto prefs=fs_release::preferencesPath();
    return prefs.empty()?std::filesystem::path{}:prefs.parent_path().parent_path()/"CrashReports-v1";
}
std::string nonce()
{
    std::random_device source;std::ostringstream text;
    for(int i=0;i<4;++i)text<<std::hex<<std::setfill('0')<<std::setw(8)<<static_cast<std::uint32_t>(source());
    return text.str();
}
void stop()
{
#if FS_HAS_SENTRY
    if(active){sentry_user_consent_revoke();sentry_close();}
#endif
    active=false;
}
void sync()
{
    const auto next=consent();
    if(next==observed)return;
    stop();observed=next;failure.clear();
    if(!next.enabled || !configured())return;
#if FS_HAS_SENTRY
    // A fresh database can never inherit an old granted-consent marker or replay
    // an old crash. Offline reports are intentionally not retried on next launch.
    std::error_code error;const auto runs=root()/"runs";
    std::filesystem::create_directories(runs,error);
    if(error){failure="Crash storage is unavailable.";return;}
    try{database=runs/nonce();}catch(const std::exception&){failure="Crash storage could not be initialized.";return;}
    if(!std::filesystem::create_directory(database,error) || error){failure="Crash storage could not be initialized.";return;}
    std::array<wchar_t,32768> binary{};
    const auto length=GetModuleFileNameW(nullptr,binary.data(),static_cast<DWORD>(binary.size()));
    if(!length || length>=binary.size()){failure="Crash handler location is unavailable.";return;}
    const auto handler=std::filesystem::path(std::wstring(binary.data(),length)).parent_path()/L"folderstorm-crashpad-handler.exe";
    if(!std::filesystem::is_regular_file(handler,error)){failure="The matching crash handler is missing.";return;}
    auto* options=sentry_options_new();
    sentry_options_set_dsn(options,FS_SENTRY_DSN);
    sentry_options_set_database_pathw(options,database.c_str());
    sentry_options_set_handler_pathw(options,handler.c_str());
    const auto build=fs_release::runningBuild();
    const auto release="folderstorm@"+build.version+"+"+FS_CRASH_COMMIT;
    sentry_options_set_release(options,release.c_str());
    sentry_options_set_environment(options,fs_release::DefaultChannel);
    // send_default_pii is NX-only in SDK 0.17.3. Keep identity and private
    // attachments out of this integration; native minidumps can contain memory.
    sentry_options_set_auto_session_tracking(options,0);
    sentry_options_set_max_breadcrumbs(options,0);
    sentry_options_set_require_user_consent(options,1);
    sentry_options_set_cache_keep(options,SENTRY_CACHE_KEEP_NONE);
    sentry_options_set_shutdown_timeout(options,1500);
    if(sentry_init(options)!=0){failure="Crash reporting could not start.";return;}
    // No user identity, region, command line, log files or screenshots are added.
    sentry_set_tag("process.role",processRole.c_str());
    sentry_set_tag("build.cpu",build.cpu.c_str());sentry_set_tag("build.flavor",build.flavor.c_str());
    sentry_set_tag("source.commit",FS_CRASH_COMMIT);
    // Recheck after initialization, which can take time. Never grant a stale epoch.
    if(!(consent()==observed)){sentry_user_consent_revoke();sentry_close();return;}
    active=true;sentry_user_consent_give();
#endif
}
}
Consent consent()
{
    const auto directory=root();if(directory.empty())return {};
    std::ifstream file(directory/"consent.dat",std::ios::binary);std::string bytes(39,'\0');
    if(!file)return {};
    file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));return Consent::decode(bytes);
}
bool configured(){return FS_HAS_SENTRY && FS_SENTRY_DSN[0];}
bool setConsent(bool enabled)
{
    if(enabled && !configured())return false;
    const auto directory=root();if(directory.empty())return false;
    std::error_code error;std::filesystem::create_directories(directory,error);if(error)return false;
    Consent value;value.enabled=enabled;
    try{value.epoch=nonce();}catch(const std::exception&){return false;}
    auto temporary=directory/("consent.tmp."+value.epoch);
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);const auto bytes=value.encode();
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.close();if(!file)return false;
#if defined(_WIN32)
    const bool saved=MoveFileExW(temporary.c_str(),(directory/"consent.dat").c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!saved)std::filesystem::remove(temporary,error);
#else
    std::filesystem::rename(temporary,directory/"consent.dat",error);const bool saved=!error;
#endif
    if(saved && started)sync();
    return saved;
}
void initialize(const std::string& role)
{processRole=validRole(role)?role:"viewer";started=true;observed={};sync();}
std::optional<ConsentPrompt> consentPrompt()
{
#if defined(_WIN32)
    if(!started || !configured() || processRole=="worker")return {};
    // Keep one pending question across hosts/viewers without blocking their UI.
    struct Lease
    {
        HANDLE handle=CreateMutexW(nullptr,FALSE,L"Local\\Folderstorm.CrashConsent.v1");
        bool locked=false;
        void unlock(){if(locked){ReleaseMutex(handle);locked=false;}}
        ~Lease(){unlock();if(handle)CloseHandle(handle);}
    };
    auto lease=std::make_shared<Lease>();if(!lease->handle)return {};
    const auto acquired=WaitForSingleObject(lease->handle,0);
    if(acquired!=WAIT_OBJECT_0 && acquired!=WAIT_ABANDONED)return {};
    lease->locked=true;
    const auto original=consent();
    if(original.enabled || !original.epoch.empty())return {};
    return ConsentPrompt{[lease,original](std::optional<bool> choice)
    {
        if(!lease->locked)return false;
        struct Release {Lease& owner;~Release(){owner.unlock();}} release{*lease};
        if(!choice || !(consent()==original))return true;
        return offerConsentOnce(configured(),processRole,consent,[choice](){return choice;},setConsent);
    }};
#else
    return {};
#endif
}
void offerConsent()
{
#if defined(_WIN32)
    auto prompt=consentPrompt();if(!prompt)return;
    const auto owner=GetActiveWindow();
    const auto ask=[owner]() -> std::optional<bool>
    {
        const wchar_t* title=L"Folderstorm crash reporting";
        const wchar_t* explanation=L"Automatically send crash reports to Folderstorm's Sentry project to help fix bugs. Reports include technical details and a memory dump, which may contain personal information. Chat logs and screenshots are not attached.\n\n"
            L"Your choice applies to the viewer, host and all character sessions for this Windows user. Change it later in viewer Help or host Settings > Support > Crash reports.";
        using TaskDialog=HRESULT(WINAPI*)(const TASKDIALOGCONFIG*,int*,int*,BOOL*);
        const auto dialog=reinterpret_cast<TaskDialog>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"comctl32.dll"),"TaskDialogIndirect")));
        if(dialog)
        {
            const TASKDIALOG_BUTTON buttons[]={{IDYES,L"Enable crash reporting"},{IDNO,L"Not now"}};
            TASKDIALOGCONFIG options{};options.cbSize=sizeof(options);options.hwndParent=owner;
            options.dwFlags=TDF_ALLOW_DIALOG_CANCELLATION;options.pszWindowTitle=title;
            options.pszMainInstruction=L"Help improve Folderstorm?";options.pszContent=explanation;
            options.cButtons=2;options.pButtons=buttons;options.nDefaultButton=IDNO;
            int selected=0;
            if(SUCCEEDED(dialog(&options,&selected,nullptr,nullptr)))
                return selected==IDYES;
        }
        const std::wstring text=std::wstring(L"Enable crash reporting?\n\n")+explanation;
        const auto selected=MessageBoxW(owner,text.c_str(),title,MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION);
        return selected?std::optional<bool>(selected==IDYES):std::nullopt;
    };
    if(!prompt->respond(ask()))
        MessageBoxW(owner,L"Your crash-reporting choice could not be saved. You can change it in viewer Help or host Settings > Support > Crash reports.",
            L"Folderstorm crash reporting",MB_OK|MB_ICONINFORMATION);
#endif
}
void poll()
{
    if(!started)return;
    const auto now=std::chrono::steady_clock::now();if(now-lastPoll<std::chrono::milliseconds(500))return;
    lastPoll=now;sync();
}
void shutdown(){stop();started=false;}
std::string status()
{
    if(!configured())return "Not configured in this build.";
    if(!failure.empty())return failure;
    return active?"Crash reporting is on for this process.":"Crash reporting is off.";
}
std::string environment()
{
    const auto build=fs_release::runningBuild();
    return environmentText(build.version,build.platform,build.cpu,processRole,FS_CRASH_COMMIT);
}
}
