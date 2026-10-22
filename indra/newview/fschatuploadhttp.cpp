/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fschatuploadpolicy.h"
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
#include <curl/curl.h>
#include <cstring>
namespace fs_chat_upload
{
namespace
{
struct Transfer
{
    const std::vector<unsigned char>& bytes;
    std::string prefix, suffix, reply;
    std::size_t read = 0;
    Progress& progress;
};
curl_socket_t openSocket(void*, curlsocktype purpose, curl_sockaddr* address)
{
    if (purpose != CURLSOCKTYPE_IPCXN) return CURL_SOCKET_BAD;
    if (address->family == AF_INET)
    { if (!fs_chat_media::publicV4(reinterpret_cast<const unsigned char*>(&reinterpret_cast<sockaddr_in*>(&address->addr)->sin_addr))) return CURL_SOCKET_BAD; }
    else if (address->family == AF_INET6)
    { if (!fs_chat_media::publicV6(reinterpret_cast<const unsigned char*>(&reinterpret_cast<sockaddr_in6*>(&address->addr)->sin6_addr))) return CURL_SOCKET_BAD; }
    else return CURL_SOCKET_BAD;
    return ::socket(address->family, address->socktype, address->protocol);
}
std::size_t read(char* buffer, std::size_t size, std::size_t count, void* opaque)
{
    auto& t = *static_cast<Transfer*>(opaque);
    if (t.progress.cancelled) return CURL_READFUNC_ABORT;
    if (size && count > MaxFileBytes / size) return CURL_READFUNC_ABORT;
    const auto total = t.prefix.size() + t.bytes.size() + t.suffix.size();
    auto remaining = (std::min)(size * count, total - t.read); const auto copied = remaining;
    while (remaining)
    {
        const char* data; std::size_t available;
        if (t.read < t.prefix.size()) { data = t.prefix.data() + t.read; available = t.prefix.size() - t.read; }
        else if (t.read < t.prefix.size() + t.bytes.size()) { const auto at = t.read - t.prefix.size(); data = reinterpret_cast<const char*>(t.bytes.data() + at); available = t.bytes.size() - at; }
        else { const auto at = t.read - t.prefix.size() - t.bytes.size(); data = t.suffix.data() + at; available = t.suffix.size() - at; }
        const auto length = (std::min)(remaining, available); std::memcpy(buffer, data, length); buffer += length; remaining -= length; t.read += length;
    }
    if (copied) t.progress.submitted = true;
    t.progress.percent = static_cast<unsigned>(t.read * 100 / total);
    return copied;
}
std::size_t write(char* data, std::size_t size, std::size_t count, void* opaque)
{
    auto& t = *static_cast<Transfer*>(opaque);
    if (size && count > MaxReplyBytes / size) return 0;
    const auto length = size * count;
    if (t.reply.size() > MaxReplyBytes - length) return 0;
    try { t.reply.append(data, length); } catch (...) { return 0; }
    return length;
}
int progress(void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{ return static_cast<Progress*>(opaque)->cancelled ? 1 : 0; }
}
Reply upload(const Config& config, const std::vector<unsigned char>& data, const std::string& type,
             const std::string& boundary, const std::string& caFile, Progress& status)
{
    if (status.cancelled || !config.enabled || !config.valid() || data.empty() || data.size() > MaxFileBytes ||
        extension(type).empty() || mime(data) != type || boundary.size() < 32 || boundary.size() > 80 ||
        !std::all_of(boundary.begin(), boundary.end(), [](unsigned char c) { return std::isalnum(c); })) return {};
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) return {};
    const auto url = config.base() + "/api/upload";
    Transfer t{data, "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"image." + extension(type) + "\"\r\nContent-Type: " + type + "\r\n\r\n", "\r\n--" + boundary + "--\r\n", "", 0, status};
    curl_slist* rawHeaders = nullptr;
    for (const auto& header : {"Content-Type: multipart/form-data; boundary=" + boundary, "x-zipline-folder: " + config.folder,
        std::string("x-zipline-format: random"), std::string("x-zipline-original-name: false"), std::string("Expect:")})
    { auto* next = curl_slist_append(rawHeaders, header.c_str()); if (!next) { curl_slist_free_all(rawHeaders); return {}; } rawHeaders = next; }
    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(rawHeaders, curl_slist_free_all);
    auto* handle = curl.get();
#define FS_UPLOAD_OPT(option, value) if (curl_easy_setopt(handle, option, value) != CURLE_OK) return {}
    FS_UPLOAD_OPT(CURLOPT_URL, url.c_str()); FS_UPLOAD_OPT(CURLOPT_PROXY, ""); // UI refuses configured HTTP proxies.
#if LIBCURL_VERSION_NUM >= 0x075500
    FS_UPLOAD_OPT(CURLOPT_PROTOCOLS_STR, "https");
#else
    FS_UPLOAD_OPT(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
#endif
    FS_UPLOAD_OPT(CURLOPT_HTTPHEADER, headers.get()); FS_UPLOAD_OPT(CURLOPT_POST, 1L);
    FS_UPLOAD_OPT(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(t.prefix.size() + data.size() + t.suffix.size()));
    FS_UPLOAD_OPT(CURLOPT_READFUNCTION, read); FS_UPLOAD_OPT(CURLOPT_READDATA, &t);
    FS_UPLOAD_OPT(CURLOPT_WRITEFUNCTION, write); FS_UPLOAD_OPT(CURLOPT_WRITEDATA, &t);
    FS_UPLOAD_OPT(CURLOPT_OPENSOCKETFUNCTION, openSocket); FS_UPLOAD_OPT(CURLOPT_FOLLOWLOCATION, 0L);
    FS_UPLOAD_OPT(CURLOPT_SSL_VERIFYPEER, 1L); FS_UPLOAD_OPT(CURLOPT_SSL_VERIFYHOST, 2L);
    if (!caFile.empty()) { FS_UPLOAD_OPT(CURLOPT_CAINFO, caFile.c_str()); }
    FS_UPLOAD_OPT(CURLOPT_NOSIGNAL, 1L); FS_UPLOAD_OPT(CURLOPT_CONNECTTIMEOUT_MS, 5000L); FS_UPLOAD_OPT(CURLOPT_TIMEOUT_MS, 60000L);
    FS_UPLOAD_OPT(CURLOPT_USERAGENT, "Folderstorm chat upload/1"); FS_UPLOAD_OPT(CURLOPT_NOPROGRESS, 0L);
    FS_UPLOAD_OPT(CURLOPT_XFERINFOFUNCTION, progress); FS_UPLOAD_OPT(CURLOPT_XFERINFODATA, &status);
#undef FS_UPLOAD_OPT
    const auto result = curl_easy_perform(handle); long code = 0;
    if (result != CURLE_OK || curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &code) != CURLE_OK)
    { Reply reply; reply.state = status.submitted ? State::Uncertain : State::Failed; reply.error = status.submitted ? "Upload outcome unknown. Retrying may create another file." : "Could not connect to the upload service."; return reply; }
    if (code != 200)
    {
        Reply reply; reply.error = quotaFailure(t.reply) ? "Chat storage is full. Try again in about one minute." : code == 413 ? "Image exceeds the server's size limit." : code == 429 ? "Upload rate limit reached. Try again later." :
            code == 401 || code == 403 || code == 404 ? "Upload folder is unavailable or does not allow uploads." : "Upload service refused the image.";
        return reply;
    }
    auto reply = parseReply(config, t.reply, type);
    if (reply.state == State::Failed) { reply.state = State::Uncertain; reply.error = "Image may have uploaded, but its link could not be verified. Retrying may create another file."; }
    return reply;
}
}
