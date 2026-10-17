/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <boost/json.hpp>
#include <Velopack.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <future>
#include "fsupdatewindows.h"
#include "fsupdateconfig.h"
#include "fsreleasebuild.h"
#include "release_http.h"
namespace
{
using namespace fs_update;
std::wstring wide(const std::string& value)
{
    const int size=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    if(size<=0)return {};
    std::wstring result(static_cast<std::size_t>(size),L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size);return result;
}
bool ask(const std::wstring& text){return MessageBoxW(nullptr,text.c_str(),L"Folderstorm application update",MB_YESNO|MB_ICONQUESTION)==IDYES;}
void explain(const std::wstring& text){MessageBoxW(nullptr,text.c_str(),L"Folderstorm application update",MB_OK|MB_ICONINFORMATION);}
std::atomic<bool> cancelled{false};
LRESULT CALLBACK progressProc(HWND window,UINT message,WPARAM wp,LPARAM lp)
{
    if(message==WM_CLOSE || (message==WM_COMMAND && LOWORD(wp)==IDCANCEL)){cancelled=true;return 0;}
    return DefWindowProcW(window,message,wp,lp);
}
template<class Work> auto work(const wchar_t* title,Work action)
{
    cancelled=false;WNDCLASSW klass{};klass.lpfnWndProc=progressProc;klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"FolderstormUpdateProgress";klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&klass);
    const auto window=CreateWindowExW(WS_EX_DLGMODALFRAME,klass.lpszClassName,title,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,470,135,nullptr,nullptr,klass.hInstance,nullptr);
    CreateWindowExW(0,L"STATIC",L"Working… Your characters can remain logged in during download.",WS_CHILD|WS_VISIBLE,14,16,430,36,window,nullptr,klass.hInstance,nullptr);
    CreateWindowExW(0,L"BUTTON",L"Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,340,60,100,25,window,reinterpret_cast<HMENU>(IDCANCEL),klass.hInstance,nullptr);
    auto job=std::async(std::launch::async,std::move(action));
    while(job.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)
    {
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
        MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT);
    }
    if(window)DestroyWindow(window);
    return job.get();
}
struct Hash
{
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;
    Hash(){if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0 || BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)<0)throw std::runtime_error("Hashing unavailable");}
    ~Hash(){if(hash)BCryptDestroyHash(hash);if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);}
    bool add(char* bytes,DWORD size){return BCryptHashData(hash,reinterpret_cast<PUCHAR>(bytes),size,0)>=0;}
    std::string finish(){unsigned char bytes[32]{};if(BCryptFinishHash(hash,bytes,sizeof(bytes),0)<0)return {};const char* hex="0123456789abcdef";std::string result;for(const auto byte:bytes){result+=hex[byte>>4];result+=hex[byte&15];}return result;}
};
struct Source
{
    fs_release::UpdatePackage package;
    std::string feed;
    static char* releases(void* data,const char*)
    {const auto& feed=static_cast<Source*>(data)->feed;auto* result=static_cast<char*>(std::malloc(feed.size()+1));if(result)std::memcpy(result,feed.c_str(),feed.size()+1);return result;}
    static void freeFeed(void*,char* data){std::free(data);}
    static bool download(void* data,const vpkc_asset_t* asset,const char* destination,size_t progress)
    {
        const auto& expected=static_cast<Source*>(data)->package;
        if(!asset || !asset->FileName || expected.file!=asset->FileName || !asset->Version || expected.version!=asset->Version || asset->Size!=expected.bytes)return false;
        const std::filesystem::path output(wide(destination));
        try
        {
            const auto url=wide(expected.url);URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
            if(!fs_release::trustedUrl(expected.url,fs_release::DownloadHosts) || !WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts))return false;
            const std::wstring server(parts.lpszHostName,parts.dwHostNameLength),path=std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength)+std::wstring(parts.lpszExtraInfo,parts.dwExtraInfoLength);
            fs_release::InternetHandle session(WinHttpOpen(L"Folderstorm updater/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));
            if(!session.value)return false;WinHttpSetTimeouts(session.value,5000,5000,5000,15000);
            fs_release::InternetHandle connection(WinHttpConnect(session.value,server.c_str(),parts.nPort,0));
            fs_release::InternetHandle request(connection.value?WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,nullptr,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr);
            if(!request.value)return false;DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
            WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
            if(!WinHttpSendRequest(request.value,WINHTTP_NO_ADDITIONAL_HEADERS,0,nullptr,0,0,0)||!WinHttpReceiveResponse(request.value,nullptr))return false;
            DWORD status=0,length=sizeof(status);if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX)||status!=200)return false;
            std::ofstream file(output,std::ios::binary|std::ios::trunc);if(!file)return false;
            Hash hash;std::uint64_t received=0;char buffer[65536];const auto deadline=GetTickCount64()+30*60*1000;
            for(;;)
            {
                DWORD count=0;if(cancelled || GetTickCount64()>deadline || !WinHttpReadData(request.value,buffer,sizeof(buffer),&count))throw std::runtime_error("Download interrupted");
                if(!count)break;
                if(count>expected.bytes-received || !hash.add(buffer,count))throw std::runtime_error("Invalid package");
                received+=count;file.write(buffer,count);if(!file)throw std::runtime_error("Package storage unavailable");
                vpkc_source_report_progress(progress,static_cast<std::int16_t>(received*100/expected.bytes));
            }
            file.close();if(!file || received!=expected.bytes || hash.finish()!=expected.sha256)throw std::runtime_error("Package verification failed");
            return true;
        }
        catch(const std::exception&){std::error_code error;std::filesystem::remove(output,error);return false;}
    }
};
bool bundleMatches(const std::filesystem::path& root)
{
    std::ifstream file(root/L"current"/L"folderstorm-bundle.json",std::ios::binary);std::string bytes(4097,'\0');file.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));bytes.resize(static_cast<std::size_t>(file.gcount()));
    if(bytes.size()>4096)return false;
    try
    {
        const auto parsed=boost::json::parse(bytes);if(!parsed.is_object())return false;const auto& obj=parsed.as_object();
        const auto same=[&](const char* key,const std::string& expected){const auto* value=obj.if_contains(key);return value && value->is_string() && value->as_string()==expected;};
        return same("version",fs_release::runningBuild().version)&&same("commit",SourceCommit)&&same("app_id",AppId)&&same("installer","velopack");
    }catch(const std::exception&){return false;}
}
bool verifyCached(const std::filesystem::path& path,const fs_release::UpdatePackage& expected)
{
    try
    {
        if(!regular(path) || std::filesystem::file_size(path)!=expected.bytes)return false;
        std::ifstream file(path,std::ios::binary);if(!file)return false;
        Hash hash;char bytes[65536];
        while(file)
        {
            if(cancelled)return false;
            file.read(bytes,sizeof(bytes));const auto size=file.gcount();
            if(size && !hash.add(bytes,static_cast<DWORD>(size)))return false;
        }
        return file.eof() && hash.finish()==expected.sha256;
    }catch(const std::exception&){return false;}
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int)
{
    using namespace fs_update;
    try
    {
        const auto root=installation(binaryPath());if(root.empty() || !bundleMatches(root)){explain(L"Use the matching Velopack installer from Folderstorm releases first. This directory is not a recognized installed bundle.");return 1;}
        Handle prepareLock(CreateFileW((root/L".folderstorm-update-prepare.lock").c_str(),GENERIC_READ,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!prepareLock){explain(L"Another update window is already open.");return 1;}
        if(GetFileAttributesW((root/L".folderstorm-update-pending").c_str())!=INVALID_FILE_ATTRIBUTES)
        {
            const auto barrier=readBarrier(root);Handle lock(CreateFileW((root/L".folderstorm-update.lock").c_str(),GENERIC_READ,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
            if(!barrier || !lock || !idle(root)){explain(L"The previous update is still in use. Close Folderstorm, its MCP client and the updater before trying recovery.");return 1;}
            if(!ask(L"The previous update did not complete its restart. Clear its launch barrier and keep the currently installed version?"))return 0;
            if(!DeleteFileW((root/L".folderstorm-update-pending").c_str())){explain(L"The update barrier could not be cleared.");return 1;}
        }
        const auto prefs=fs_release::loadPreferences(fs_release::DefaultChannel);
        auto result=work(L"Checking Folderstorm releases",[&]{std::string error;const auto feed=fs_release::fetchFeed(fs_release::FeedUrl,fs_release::DownloadHosts,fs_release::fetchWindows,error);auto result=fs_release::choose(feed,fs_release::runningBuild(),prefs,true,fs_release::DownloadHosts);if(!error.empty())result.error=error;return result;});
        if(cancelled)return 0;
        if(!result.error.empty() || !result.artifact || !result.artifact->update || result.state!=fs_release::State::Available)
        {explain(L"No matching in-app update is published. Use Check for updates to open the installer or release page.");return 0;}
        const auto package=*result.artifact->update;
        if(package.id!=AppId || package.version!=packageVersion(result.release.version)){explain(L"This release uses a different application channel or bundle. Use its installer to migrate.");return 1;}
        if(!ask(L"Download Folderstorm "+wide(result.release.version)+L"?\n\n"+wide(fs_release::plainText(result.release.notes,3000))+L"\n\nDownloading does not log out characters or install the update."))return 0;
        Source context;context.package=package;
        boost::json::object asset{{"PackageId",package.id},{"Version",package.version},{"Type","Full"},{"FileName",package.file},{"SHA1",package.sha1},{"SHA256",package.sha256},{"Size",package.bytes},{"NotesMarkdown",""},{"NotesHtml",""}};
        context.feed=boost::json::serialize(boost::json::object{{"Assets",boost::json::array{asset}}});
        auto* source=vpkc_new_source_custom_callback(Source::releases,Source::freeFeed,Source::download,&context);
        if(!source)throw std::runtime_error("Update source unavailable");
        struct SourceCleanup{vpkc_update_source_t* value;~SourceCleanup(){vpkc_free_source(value);}} sourceCleanup{source};
        vpkc_update_options_t options{};options.AllowVersionDowngrade=false;options.MaximumDeltasBeforeFallback=-1;
        vpkc_update_manager_t* manager=nullptr;
        if(!vpkc_new_update_manager_with_source(source,&options,nullptr,&manager))throw std::runtime_error("Installed update manager unavailable");
        struct ManagerCleanup{vpkc_update_manager_t* value;~ManagerCleanup(){vpkc_free_update_manager(value);}} managerCleanup{manager};
        char installed[256]{},id[256]{};vpkc_get_current_version(manager,installed,sizeof(installed));vpkc_get_app_id(manager,id,sizeof(id));
        if(vpkc_is_portable(manager) || packageVersion(fs_release::runningBuild().version)!=installed || package.id!=id)throw std::runtime_error("Installation identity changed");
        vpkc_update_info_t* info=nullptr;
        if(vpkc_check_for_updates(manager,&info)!=UPDATE_AVAILABLE || !info)throw std::runtime_error("No update available");
        struct InfoCleanup{vpkc_update_info_t* value;~InfoCleanup(){vpkc_free_update_info(value);}} infoCleanup{info};
        if(!info->TargetFullRelease || !info->TargetFullRelease->Version || package.version!=info->TargetFullRelease->Version)throw std::runtime_error("Target version changed");
        const bool downloaded=work(L"Downloading verified Folderstorm package",[&]{return vpkc_download_updates(manager,info,nullptr,nullptr) && verifyCached(root/L"packages"/wide(package.file),package);});
        if(!downloaded || cancelled){if(!cancelled)explain(L"The package could not be downloaded and verified. Nothing was installed.");return 0;}
        if(!ask(L"Ready to install after ALL Folderstorm windows and its MCP client close.\n\nUse the host's Quit all characters action and confirm native logout. Cancelled logout or Keep logged in will block installation. No characters are forcibly disconnected.\n\nContinue to the installation check?"))return 0;
        Coordinator coordinator;coordinator.request();
        for(;;)
        {
            Handle exclusive(CreateFileW((root/L".folderstorm-update.lock").c_str(),GENERIC_READ,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
            if(coordinator.mayApply(static_cast<bool>(exclusive),idle(root),installation(binaryPath())==root,bundleMatches(root)))
            {
                if(!ask(L"All Folderstorm processes are closed. Install the verified update now? The viewer will restart afterward.")){coordinator.cancel();return 0;}
                if(!idle(root) || !bundleMatches(root))continue;
                if(fs_release::loadPreferences(fs_release::DefaultChannel).channel!=prefs.channel)
                    throw std::runtime_error("Release channel changed; check again");
                Barrier barrier{fs_release::runningBuild().version,result.release.version,result.release.commit};
                if(!writeBarrier(root,barrier))throw std::runtime_error("Could not reserve installation");
                coordinator.applying();
                // Updater waits for THIS helper to exit. The persistent barrier
                // prevents fresh viewer/host/MCP launches after its lock closes.
                if(!vpkc_wait_exit_then_apply_updates(manager,info->TargetFullRelease,false,true,nullptr,0))
                {throw std::runtime_error("Updater did not accept the request");}
                return 0;
            }
            if(MessageBoxW(nullptr,L"A viewer, host, MCP client or helper is still using this installation, or the bundle changed. Close it and Retry. Cancel leaves the verified download unapplied.",L"Folderstorm update is waiting",MB_RETRYCANCEL|MB_ICONINFORMATION)!=IDRETRY){coordinator.cancel();return 0;}
        }
    }
    catch(const std::exception&){explain(L"The update could not continue. No action will be retried automatically. Reopen this updater to retry or recover a pending launch barrier.");return 1;}
}
