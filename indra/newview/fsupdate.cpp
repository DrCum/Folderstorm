/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsupdate.h"
#include "fsupdatewindows.h"
#include "fsreleasebuild.h"
#include "fsupdateconfig.h"
namespace fs_update
{
namespace {std::string error;
#if defined(_WIN32)
Handle lifetime;
#endif
}
std::string lastError(){return error;}
bool enter()
{
#if defined(_WIN32)
    const auto binary=binaryPath();auto root=installation(binary);
    if(root.empty())
    {
        // During replacement sq.version/bundle may temporarily disappear. Stable
        // root coordination files must still gate those partially copied apps.
        std::error_code resolutionError;
        const auto current=std::filesystem::canonical(binary.parent_path(),resolutionError);
        if(resolutionError){error="The application directory is unavailable. Finish its update before launching.";return false;}
        const auto candidate=current.parent_path();
        if(CompareStringOrdinal(current.filename().c_str(),-1,L"current",-1,TRUE)!=CSTR_EQUAL || (GetFileAttributesW((candidate/L".folderstorm-update-pending").c_str())==INVALID_FILE_ATTRIBUTES && GetFileAttributesW((candidate/L".folderstorm-update.lock").c_str())==INVALID_FILE_ATTRIBUTES))return true;
        root=candidate;
    }
    // A new target may clear its completed transaction after Update.exe exits.
    // Old binaries never clear a barrier left by an incomplete update.
    const auto pending=root/L".folderstorm-update-pending";
    if(GetFileAttributesW(pending.c_str())!=INVALID_FILE_ATTRIBUTES)
    {
        const auto barrier=readBarrier(root);
        if(!barrier || barrier->target!=fs_release::runningBuild().version || barrier->commit!=SourceCommit || installation(binary)!=root){error="An update is pending. Run folderstorm-update.exe to finish or recover it.";return false;}
        for(int i=0;i<100 && !idle(root,true);++i)Sleep(50);
        Handle exclusive(CreateFileW((root/L".folderstorm-update.lock").c_str(),GENERIC_READ,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr));
        if(!exclusive || !idle(root,true) || !DeleteFileW(pending.c_str())){error="An update is still finishing. Try launching again shortly.";return false;}
    }
    if(installation(binary)!=root){error="This Folderstorm installation is incomplete. Finish or recover its update before launching.";return false;}
    lifetime.value=CreateFileW((root/L".folderstorm-update.lock").c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(!lifetime || GetFileAttributesW(pending.c_str())!=INVALID_FILE_ATTRIBUTES)
    {error="This Folderstorm installation is being updated. Try again when it finishes.";return false;}
#endif
    return true;
}
bool prepare()
{
#if defined(_WIN32)
    const auto binary=binaryPath(),root=installation(binary);
    const auto helper=binary.parent_path()/L"folderstorm-update.exe";
    if(root.empty() || !regular(helper)){error="This install uses NSIS or an unrecognized layout. Download the matching Velopack installer from the release page first.";return false;}
    auto command=L"\""+helper.wstring()+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(helper.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,helper.parent_path().c_str(),&startup,&process))
    {error="The update window could not open.";return false;}
    CloseHandle(process.hThread);CloseHandle(process.hProcess);return true;
#else
    error="Coordinated in-app updates currently support Windows. Use the release download.";return false;
#endif
}
}
