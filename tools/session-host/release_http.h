/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_HOST_RELEASE_HTTP_H
#define FS_HOST_RELEASE_HTTP_H
#include <windows.h>
#include <winhttp.h>
#include <future>
#include <stdexcept>
#include "fsreleasebuild.h"
#include "fsreleaseprefs.h"
namespace fs_release
{
struct InternetHandle
{
    HINTERNET value=nullptr;
    explicit InternetHandle(HINTERNET handle):value(handle){}
    ~InternetHandle(){if(value)WinHttpCloseHandle(value);}
    InternetHandle(const InternetHandle&)=delete;InternetHandle& operator=(const InternetHandle&)=delete;
};
inline std::string fetchWindows(const std::string& address)
{
    if(host(address).empty())throw std::runtime_error("The configured update URL is invalid.");
    const std::wstring url(address.begin(),address.end());URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);
    parts.dwHostNameLength=parts.dwUrlPathLength=parts.dwExtraInfoLength=static_cast<DWORD>(-1);
    if(!WinHttpCrackUrl(url.c_str(),static_cast<DWORD>(url.size()),0,&parts))throw std::runtime_error("Invalid update URL.");
    const std::wstring server(parts.lpszHostName,parts.dwHostNameLength);
    const std::wstring path=std::wstring(parts.lpszUrlPath,parts.dwUrlPathLength)+std::wstring(parts.lpszExtraInfo,parts.dwExtraInfoLength);
    InternetHandle session(WinHttpOpen(L"Folderstorm release checker/1",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0));
    if(!session.value)throw std::runtime_error("Update networking could not start.");
    WinHttpSetTimeouts(session.value,5000,5000,5000,10000);
    InternetHandle connection(WinHttpConnect(session.value,server.c_str(),parts.nPort,0));
    InternetHandle request(connection.value?WinHttpOpenRequest(connection.value,L"GET",path.c_str(),nullptr,nullptr,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE):nullptr);
    if(!request.value)throw std::runtime_error("Update networking could not start.");
    // Follow only TLS redirects, retaining system certificate verification.
    DWORD redirects=WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(request.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirects,sizeof(redirects));
    if(!WinHttpSendRequest(request.value,L"Accept: application/json\r\n",static_cast<DWORD>(-1),nullptr,0,0,0) || !WinHttpReceiveResponse(request.value,nullptr))
        throw std::runtime_error("Could not check for updates. Try again later.");
    DWORD status=0,length=sizeof(status);
    if(!WinHttpQueryHeaders(request.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("Could not read the update response.");
    if(status==404)return "[]";
    if(status!=200)throw std::runtime_error(status==403 || status==429?"Update checks are temporarily rate limited. Try again later.":"The update server is unavailable.");
    std::string result;char buffer[8192];const auto deadline=GetTickCount64()+20000;
    for(;;)
    {
        DWORD count=0;
        if(GetTickCount64()>deadline || !WinHttpReadData(request.value,buffer,sizeof(buffer),&count))throw std::runtime_error("The update response was interrupted.");
        if(!count)break;
        if(result.size()+count>MaxFeedBytes)throw std::runtime_error("The update response is too large.");
        result.append(buffer,count);
    }
    return result;
}
class WindowsChecker
{
    std::future<Result> job;
public:
    bool manual=false;
    std::string channel;
    bool busy()const{return job.valid();}
    void start(bool requested)
    {
        manual=manual || requested;if(busy())return;
        auto prefs=loadPreferences(DefaultChannel);if(!requested && !prefs.due(now()))return;
        channel=prefs.channel;
        prefs.lastCheck=now();savePreferences(prefs);
        job=std::async(std::launch::async,[prefs,requested]()
        {
            Result result;
            try{std::string error;const auto releases=fetchFeed(FeedUrl,DownloadHosts,fetchWindows,error);result=choose(releases,runningBuild(),prefs,requested,DownloadHosts);if(!error.empty())result.error=error;}
            catch(const std::exception& error){result.error=error.what();}
            return result;
        });
    }
    std::optional<Result> poll()
    {if(!job.valid() || job.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return {};return job.get();}
};
}
#endif
