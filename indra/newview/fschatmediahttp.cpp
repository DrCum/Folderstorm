/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fschatmediahttp.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <curl/curl.h>
#include <chrono>
#include <memory>

namespace fs_chat_media
{
namespace
{
struct Transfer
{
    Response response;
    std::size_t limit, headerBytes = 0;
    const std::atomic<bool>& cancelled;
};
curl_socket_t openSocket(void*, curlsocktype purpose, struct curl_sockaddr* address)
{
    if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKET_BAD;
    if (address->family == AF_INET)
    {
        const auto* ip = reinterpret_cast<const sockaddr_in*>(&address->addr);
        if (!publicV4(reinterpret_cast<const unsigned char*>(&ip->sin_addr))) return CURL_SOCKET_BAD;
    }
    else if (address->family == AF_INET6)
    {
        const auto* ip = reinterpret_cast<const sockaddr_in6*>(&address->addr);
        if (!publicV6(reinterpret_cast<const unsigned char*>(&ip->sin6_addr))) return CURL_SOCKET_BAD;
    }
    else return CURL_SOCKET_BAD;
    // Check the actual connected destination, not an earlier DNS lookup.
    return ::socket(address->family, address->socktype, address->protocol);
}
std::size_t writeBody(char* bytes, std::size_t size, std::size_t count, void* opaque)
{
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (transfer.cancelled || (size && count > transfer.limit / size)) return 0;
    const auto length = size * count;
    const auto limit = transfer.response.type == "text/html" || transfer.response.type == "application/json" ? (std::min)(transfer.limit, MaxMetadataBytes) : transfer.limit;
    if (length > limit || transfer.response.body.size() > limit - length) return 0;
    try { transfer.response.body.insert(transfer.response.body.end(), bytes, bytes + length); }
    catch (...) { return 0; }
    return length;
}
std::size_t readHeader(char* bytes, std::size_t size, std::size_t count, void* opaque)
{
    auto& transfer = *static_cast<Transfer*>(opaque);
    if (size && count > 8192 / size) return 0;
    const auto length = size * count;
    if (transfer.headerBytes + length > 64 * 1024 || transfer.cancelled) return 0;
    transfer.headerBytes += length;
    try
    {
        const std::string line(bytes, length); const auto colon = line.find(':');
        if (colon == std::string::npos) return length;
        auto value = line.substr(colon + 1); const auto first = value.find_first_not_of(" \t");
        const auto last = value.find_last_not_of(" \t\r\n");
        value = first == std::string::npos || last == std::string::npos ? "" : value.substr(first, last - first + 1);
        const auto name = lower(line.substr(0, colon));
        if (name == "location") transfer.response.location = value;
        if (name == "content-type") transfer.response.type = lower(value.substr(0, value.find(';')));
        return length;
    }
    catch (...) { return 0; }
}
int progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{ return static_cast<const std::atomic<bool>*>(opaque)->load() ? 1 : 0; }
}
Response fetch(const std::string& original, const std::string& caFile, std::size_t limit, const std::atomic<bool>& cancelled, bool head)
{
    if (limit > MaxImageBytes) return {};
    auto address = original;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    for (unsigned redirects = 0; redirects <= 3 && !cancelled; ++redirects)
    {
        const auto url = parse(address); if (!url) return {};
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) return {};
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl) return {};
        Transfer transfer{{}, limit, 0, cancelled}; auto* handle = curl.get();
        if (curl_easy_setopt(handle, CURLOPT_URL, address.c_str()) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_USERAGENT, "Folderstorm image preview/1") != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_PROXY, "") != CURLE_OK) return {}; // Caller refuses configured proxies.
#if LIBCURL_VERSION_NUM >= 0x075500
        if (curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https") != CURLE_OK) return {};
#else
        if (curl_easy_setopt(handle, CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS)) != CURLE_OK) return {};
#endif
        if (curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L) != CURLE_OK) return {};
        if (head && curl_easy_setopt(handle, CURLOPT_NOBODY, 1L) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK) return {};
        if (!caFile.empty() && curl_easy_setopt(handle, CURLOPT_CAINFO, caFile.c_str()) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, (std::min)(5000L, static_cast<long>(remaining))) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(remaining)) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_OPENSOCKETFUNCTION, openSocket) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeBody) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_WRITEDATA, &transfer) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, readHeader) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_HEADERDATA, &transfer) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, progress) != CURLE_OK) return {};
        if (curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &cancelled) != CURLE_OK) return {};
        if (curl_easy_perform(handle) != CURLE_OK || cancelled) return {};
        if (curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &transfer.response.status) != CURLE_OK) return {};
        if (transfer.response.status >= 300 && transfer.response.status <= 399)
        {
            address = resolve(*url, transfer.response.location); if (address.empty()) return {};
            continue;
        }
        transfer.response.url = url->value;
        return std::move(transfer.response);
    }
    return {};
}
}
