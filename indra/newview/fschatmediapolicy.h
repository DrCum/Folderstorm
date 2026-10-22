/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_POLICY_H
#define FS_CHAT_MEDIA_POLICY_H
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fs_chat_media
{
constexpr std::size_t MaxImageBytes = 15 * 1024 * 1024;
constexpr std::size_t MaxMetadataBytes = 512 * 1024;
constexpr std::size_t MaxDecodedBytes = 64 * 1024 * 1024;
constexpr std::size_t MaxPixels = 16 * 1024 * 1024;
constexpr unsigned MaxFrames = 120;
constexpr unsigned MaxCards = 3;
constexpr unsigned MaxJobs = 4;
constexpr unsigned MaxEntries = 48;
constexpr const char* DefaultSites = "gyazo.com,jmp.sh,jumpshare.com,snipboard.io,imgur.com,ibb.co,imgbb.com,imgchest.com";
inline std::string lower(std::string text)
{ for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A')); return text; }
inline bool ends(const std::string& text, const std::string& suffix)
{ return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0; }
struct Url { std::string value, host, path; };
inline std::optional<Url> parse(const std::string& text)
{
    if (text.size() > 4096 || text.compare(0, 8, "https://") != 0) return {};
    for (unsigned char c : text) if (c <= 32 || c == 127 || c == '\\' || c == '"' || c == '\'' || c == '<' || c == '>') return {};
    const auto end = text.find_first_of("/?#", 8);
    auto host = lower(text.substr(8, end == std::string::npos ? end : end - 8));
    if (ends(host, ":443")) host.resize(host.size() - 4);
    if (host.empty() || host.size() > 253 || host.front() == '.' || host.back() == '.' || host.find('.') == std::string::npos) return {};
    bool labelStart = true;
    for (unsigned char c : host)
    {
        if (c == '.') { if (labelStart) return {}; labelStart = true; }
        else { if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return {}; labelStart = false; }
    }
    for (const char* suffix : {".localhost", ".local", ".internal", ".test", ".invalid", ".home", ".lan", ".onion"})
        if (ends(host, suffix)) return {};
    auto value = text.substr(0, text.find('#'));
    std::string path = end == std::string::npos || text[end] == '#' ? "/" : value.substr(end);
    if (!path.empty() && path.front() == '?') path = "/" + path;
    return Url{value, host, path};
}
inline bool publicV4(const unsigned char* ip)
{
    if (ip[0] == 0 || ip[0] == 10 || ip[0] == 127 || ip[0] >= 224 ||
        (ip[0] == 100 && (ip[1] & 0xc0) == 64) || (ip[0] == 169 && ip[1] == 254) ||
        (ip[0] == 172 && ip[1] >= 16 && ip[1] <= 31) ||
        (ip[0] == 192 && (ip[1] == 0 || ip[1] == 168)) ||
        (ip[0] == 198 && (ip[1] == 18 || ip[1] == 19 || (ip[1] == 51 && ip[2] == 100))) ||
        (ip[0] == 203 && ip[1] == 0 && ip[2] == 113)) return false;
    return true;
}
inline bool publicV6(const unsigned char* ip)
{
    // Global unicast only; exclude mapped, link-local, NAT64 and transition ranges.
    return (ip[0] & 0xe0) == 0x20 && !(ip[0] == 0x20 && ip[1] == 0x02) &&
        !(ip[0] == 0x20 && ip[1] == 0x01 && (ip[2] < 2 || (ip[2] == 0x0d && ip[3] == 0xb8)));
}
inline bool site(const std::string& host, const char* name)
{ return host == name || ends(host, std::string(".") + name); }
enum class Provider { Direct, Gyazo, Jumpshare, Snipboard, Imgur, ImgBB, ImageChest, Custom };
inline bool imagePath(const Url& url)
{
    const auto path = lower(url.path.substr(0, url.path.find('?')));
    return ends(path, ".png") || ends(path, ".jpg") || ends(path, ".jpeg") || ends(path, ".gif") || ends(path, ".webp");
}
inline bool selectedSite(const Url& url, const std::string& sites)
{
    std::size_t offset = 0;
    while (offset < sites.size())
    {
        const auto end = sites.find(',', offset);
        auto name = lower(sites.substr(offset, end == std::string::npos ? end : end - offset));
        const auto first = name.find_first_not_of(" \t"); const auto last = name.find_last_not_of(" \t");
        if (first != std::string::npos && site(url.host, name.substr(first, last - first + 1).c_str())) return true;
        if (end == std::string::npos) break;
        offset = end + 1;
    }
    return false;
}
inline std::optional<Provider> provider(const Url& url, const std::string& sites = DefaultSites)
{
    // These are share pages even when their public URL ends in .jpg/.gif.
    if (url.host == "snipboard.io" || url.host == "www.snipboard.io") return Provider::Snipboard;
    if (url.host == "gyazo.com" || url.host == "www.gyazo.com") return Provider::Gyazo;
    if (url.host == "imgur.com" || url.host == "www.imgur.com") return Provider::Imgur;
    if (imagePath(url)) return Provider::Direct;
    if (site(url.host, "gyazo.com")) return Provider::Gyazo;
    if (site(url.host, "jumpshare.com") || site(url.host, "jmp.sh")) return Provider::Jumpshare;
    if (site(url.host, "snipboard.io")) return Provider::Snipboard;
    if (site(url.host, "imgur.com")) return Provider::Imgur;
    if (site(url.host, "ibb.co") || site(url.host, "imgbb.com")) return Provider::ImgBB;
    if (site(url.host, "imgchest.com")) return Provider::ImageChest;
    if (selectedSite(url, sites)) return Provider::Custom;
    return {};
}
inline std::vector<std::string> discover(const std::string& text, const std::string& sites = DefaultSites)
{
    std::vector<std::string> urls; std::size_t offset = 0;
    while (urls.size() < MaxCards && (offset = text.find("https://", offset)) != std::string::npos)
    {
        auto end = text.find_first_of(" \t\r\n<>\"'[]", offset);
        auto value = text.substr(offset, end == std::string::npos ? end : end - offset);
        while (!value.empty() && (value.back() == '.' || value.back() == ',' || value.back() == ';' || value.back() == '!' || value.back() == '?')) value.pop_back();
        while (!value.empty() && value.back() == ')' && std::count(value.begin(), value.end(), ')') > std::count(value.begin(), value.end(), '(')) value.pop_back();
        const auto url = parse(value);
        if (url && provider(*url, sites) && std::find(urls.begin(), urls.end(), url->value) == urls.end()) urls.push_back(url->value);
        offset = end == std::string::npos ? text.size() : end;
    }
    return urls;
}
inline std::string entity(std::string text)
{
    for (const auto& pair : {std::pair<const char*,const char*>{"&amp;", "&"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&#x2F;", "/"}, {"&#47;", "/"}})
    {
        std::size_t pos = 0;
        while ((pos = text.find(pair.first, pos)) != std::string::npos) { text.replace(pos, std::char_traits<char>::length(pair.first), pair.second); pos += std::char_traits<char>::length(pair.second); }
    }
    return text;
}
inline std::string resolve(const Url& origin, std::string target)
{
    if (target.size() > 4096) return {};
    target = entity(std::move(target));
    if (target.compare(0, 2, "//") == 0) target = "https:" + target;
    else if (!target.empty() && target[0] == '/') target = "https://" + origin.host + target;
    else if (!target.empty() && target.find(':') == std::string::npos)
    {
        const auto path = origin.path.substr(0, origin.path.find('?'));
        target = "https://" + origin.host + (target[0] == '?' ? path : path.substr(0, path.find_last_of('/') + 1)) + target;
    }
    const auto url = parse(target); return url ? url->value : std::string{};
}
inline std::string metadataImage(const Url& origin, const std::string& html)
{
    if (html.size() > MaxMetadataBytes) return {};
    const auto folded = lower(html); const auto headEnd = folded.find("</head>"); std::string fallback;
    for (std::size_t at = 0; (at = folded.find("<meta", at)) != std::string::npos;)
    {
        if (headEnd < at) break;
        const auto end = html.find('>', at + 5); if (end == std::string::npos) break;
        const auto tag = html.substr(at + 5, end - at - 5); at = end + 1;
        std::string name, content;
        for (std::size_t i = 0; i < tag.size();)
        {
            while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
            const auto keyStart = i;
            while (i < tag.size() && (std::isalnum(static_cast<unsigned char>(tag[i])) || tag[i] == ':' || tag[i] == '-' || tag[i] == '_')) ++i;
            if (i == keyStart) { ++i; continue; }
            const auto key = lower(tag.substr(keyStart, i - keyStart));
            while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
            if (i >= tag.size() || tag[i++] != '=') continue;
            while (i < tag.size() && std::isspace(static_cast<unsigned char>(tag[i]))) ++i;
            if (i >= tag.size()) break;
            const char quote = tag[i] == '\'' || tag[i] == '"' ? tag[i++] : '\0'; const auto valueStart = i;
            while (i < tag.size() && (quote ? tag[i] != quote : !std::isspace(static_cast<unsigned char>(tag[i])))) ++i;
            const auto value = tag.substr(valueStart, i - valueStart); if (quote && i < tag.size()) ++i;
            if (key == "property" || key == "name") name = lower(value);
            if (key == "content") content = value;
        }
        const auto url = resolve(origin, content); if (url.empty()) continue;
        if (name == "og:image:secure_url") return url;
        if ((name == "og:image" || name == "twitter:image" || name == "twitter:image:src") && fallback.empty()) fallback = url;
    }
    return fallback;
}
inline unsigned frameDelay(int ms) { return static_cast<unsigned>(std::clamp(ms, 40, 10000)); }
inline int textureExtent(int dimension)
{ if (dimension < 1 || dimension > 1024) return 0; int size = 1; while (size < dimension) size *= 2; return size; }
struct Fit { int width, height; };
inline Fit fit(int width, int height, int maxWidth, int maxHeight)
{
    if (width <= 0 || height <= 0 || maxWidth <= 0 || maxHeight <= 0) return {0,0};
    const double scale = (std::min)({1., double(maxWidth) / width, double(maxHeight) / height});
    return {(std::max)(1, static_cast<int>(width * scale)), (std::max)(1, static_cast<int>(height * scale))};
}
}
#endif
