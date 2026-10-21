/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_CONTROLLER_PIPE_H
#define FS_HOST_CONTROLLER_PIPE_H
#include "controller_model.h"
#include <windows.h>
#include <bcrypt.h>
#include <sddl.h>
#include <shlobj.h>
#include <filesystem>
#include <functional>

namespace fs_controller {
inline std::string randomHex(std::size_t bytes)
{
    if(bytes>64)return {};std::array<unsigned char,64> data{};
    if(BCryptGenRandom(nullptr,data.data(),static_cast<ULONG>(bytes),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)return {};
    static constexpr const char* digits="0123456789abcdef";std::string out;
    for(std::size_t i=0;i<bytes;++i){out+=digits[data[i]>>4];out+=digits[data[i]&15];}return out;
}
inline std::uint64_t utcMilliseconds()
{FILETIME ft{};GetSystemTimeAsFileTime(&ft);ULARGE_INTEGER n{};n.LowPart=ft.dwLowDateTime;n.HighPart=ft.dwHighDateTime;return (n.QuadPart-116444736000000000ULL)/10000;}
class PrivateSecurity
{
public:
    PSECURITY_DESCRIPTOR descriptor=nullptr;
    ~PrivateSecurity(){if(descriptor)LocalFree(descriptor);}
    bool initialize()
    {
        HANDLE token=INVALID_HANDLE_VALUE;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return false;
        DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<unsigned char> data(bytes);
        const bool ok=bytes&&GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes);CloseHandle(token);if(!ok)return false;
        wchar_t* sid=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid))return false;
        const std::wstring sddl=L"D:P(A;OICI;GA;;;"+std::wstring(sid)+L")";LocalFree(sid);
        return ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&descriptor,nullptr)!=FALSE;
    }
};
// UI-thread-only overlapped state machine. No blocking read, network worker,
// nested command message or external HWND/handle crosses this endpoint.
class Endpoint
{
    enum class Phase { Connect, Read, Write, Drain };
    HANDLE pipe=INVALID_HANDLE_VALUE,event=nullptr;
    OVERLAPPED overlap{};bool pending=false;Phase phase=Phase::Connect;
    std::array<unsigned char,RequestBytes> input{};std::vector<unsigned char> output;
    DWORD transferred=0;std::size_t offset=0;std::uint64_t deadline=0;
    std::wstring pipeName;std::filesystem::path discovery;
    PrivateSecurity security;
    bool createPipe()
    {
        SECURITY_ATTRIBUTES sa{sizeof(sa),security.descriptor,FALSE};
        pipe=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,ResponseBytes,RequestBytes,0,&sa);
        phase=Phase::Connect;pending=false;offset=0;deadline=0;return pipe!=INVALID_HANDLE_VALUE;
    }
    void resetClient()
    {
        if(pipe!=INVALID_HANDLE_VALUE){if(pending){CancelIoEx(pipe,&overlap);DWORD ignored=0;GetOverlappedResult(pipe,&overlap,&ignored,TRUE);}DisconnectNamedPipe(pipe);CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;}
        pending=false;output.clear();input.fill(0);createPipe();
    }
public:
    std::string instance,secret;
    ~Endpoint(){stop();}
    bool running() const{return pipe!=INVALID_HANDLE_VALUE;}
    bool active() const{return !instance.empty()&&event!=nullptr;}
    void stop()
    {
        if(pipe!=INVALID_HANDLE_VALUE){if(pending){CancelIoEx(pipe,&overlap);DWORD ignored=0;GetOverlappedResult(pipe,&overlap,&ignored,TRUE);}CloseHandle(pipe);pipe=INVALID_HANDLE_VALUE;}
        pending=false;if(event){CloseHandle(event);event=nullptr;}if(!discovery.empty())DeleteFileW(discovery.c_str());discovery.clear();instance.clear();secret.clear();
    }
    bool start()
    {
        stop();instance=randomHex(16);secret=randomHex(32);if(instance.empty()||secret.empty())return false;
        if(!security.descriptor&&!security.initialize())return false;
        wchar_t* local=nullptr;if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&local))||!local)return false;
        const auto parent=std::filesystem::path(local)/L"Folderstorm";const auto directory=parent/L"controllers";CoTaskMemFree(local);
        SECURITY_ATTRIBUTES sa{sizeof(sa),security.descriptor,FALSE};
        for(const auto& path:{parent,directory})
        {
            if(!CreateDirectoryW(path.c_str(),&sa)&&GetLastError()!=ERROR_ALREADY_EXISTS)return false;
            const auto attributes=GetFileAttributesW(path.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        }
        // Protect an existing directory as well; do not trust inherited grants.
        if(!SetFileSecurityW(directory.c_str(),DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,security.descriptor))return false;
        const std::wstring epoch(instance.begin(),instance.end());pipeName=L"\\\\.\\pipe\\FolderstormController-"+epoch;
        event=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!event||!createPipe()){stop();return false;}
        discovery=directory/(L"controller-"+epoch+L".json");
        const std::string data="{\"version\":1,\"controller_instance\":"+quote(instance)+",\"token\":"+quote(secret)+"}";
        HANDLE file=CreateFileW(discovery.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        DWORD written=0;const bool saved=file!=INVALID_HANDLE_VALUE&&WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&written,nullptr)&&written==data.size()&&FlushFileBuffers(file);
        if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);if(!saved){stop();return false;}return true;
    }
    template<class Dispatch> void tick(Dispatch dispatch)
    {
        if(!running()){if(active())createPipe();if(!running())return;}
        const auto now=GetTickCount64();
        if(deadline&&now>=deadline){resetClient();return;}
        if(pending)
        {
            if(!GetOverlappedResult(pipe,&overlap,&transferred,FALSE)){if(GetLastError()==ERROR_IO_INCOMPLETE)return;resetClient();return;}
            pending=false;
            if(phase==Phase::Connect){phase=Phase::Read;deadline=now+3000;offset=0;}
            else {if(!transferred||phase==Phase::Drain){resetClient();return;}offset+=transferred;}
        }
        if(phase==Phase::Read&&offset==input.size())
        {
            const std::size_t bytes=static_cast<std::size_t>(input[0])|(static_cast<std::size_t>(input[1])<<8)|(static_cast<std::size_t>(input[2])<<16)|(static_cast<std::size_t>(input[3])<<24);
            Request request;std::string response;
            if(bytes>RequestBytes-4||!std::all_of(input.begin()+4+bytes,input.end(),[](unsigned char c){return c==0;})||!Request::decode(std::string(reinterpret_cast<const char*>(input.data()+4),bytes),request))response=errorJSON("invalid_request");
            else if(!constantEqual(request.token,secret)||request.instance!=instance)response=errorJSON("unauthorized");
            else {request.token.clear();response=dispatch(request);}
            if(response.empty()||response.size()>ResponseBytes-4)response=errorJSON("response_limit");
            output.resize(response.size()+4);for(unsigned int i=0;i<4;++i)output[i]=static_cast<unsigned char>((response.size()>>(8*i))&255);
            std::copy(response.begin(),response.end(),output.begin()+4);phase=Phase::Write;offset=0;deadline=now+3000;
        }
        // Keep the response buffered until the client closes. Disconnecting
        // immediately after WriteFile completes can discard unread pipe data.
        if(phase==Phase::Write&&offset==output.size()){phase=Phase::Drain;offset=0;}
        ResetEvent(event);overlap={};overlap.hEvent=event;transferred=0;BOOL ok=FALSE;
        if(phase==Phase::Connect)
        {
            ok=ConnectNamedPipe(pipe,&overlap);
            if(!ok&&GetLastError()==ERROR_PIPE_CONNECTED){phase=Phase::Read;deadline=now+3000;return;}
        }
        else if(phase==Phase::Read)ok=ReadFile(pipe,input.data()+offset,static_cast<DWORD>(input.size()-offset),&transferred,&overlap);
        else if(phase==Phase::Write)ok=WriteFile(pipe,output.data()+offset,static_cast<DWORD>(output.size()-offset),&transferred,&overlap);
        else ok=ReadFile(pipe,input.data(),1,&transferred,&overlap);
        if(!ok){if(GetLastError()==ERROR_IO_PENDING){pending=true;return;}resetClient();return;}
        if(phase==Phase::Connect){phase=Phase::Read;deadline=now+3000;offset=0;}
        else {if(!transferred){resetClient();return;}offset+=transferred;}
    }
};
}
#endif
