/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fschatmediaresolver.h"
#include <boost/json.hpp>
namespace fs_chat_media
{
namespace
{
std::string encode(const std::string& text)
{
    constexpr char hex[] = "0123456789ABCDEF"; std::string result;
    for (unsigned char c : text)
    {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') result += static_cast<char>(c);
        else { result += '%'; result += hex[c >> 4]; result += hex[c & 15]; }
    }
    return result;
}
}
Response resolveMedia(const std::string& address, const std::string& sites, const Fetch& fetcher)
{
    const auto url = parse(address); if (!url) return {};
    const auto kind = provider(*url, sites); if (!kind) return {};
    std::string media = address;
    if (*kind == Provider::Gyazo)
    {
        const auto response = fetcher("https://api.gyazo.com/api/oembed?url=" + encode(address), MaxMetadataBytes);
        if (response.status != 200 || response.body.size() > MaxMetadataBytes) return {};
        boost::json::parse_options options; options.max_depth = 8;
        boost::system::error_code error;
        const auto data = boost::json::parse(std::string(response.body.begin(), response.body.end()), error, {}, options);
        if (error || !data.is_object()) return {};
        const auto& object = data.as_object();
        const auto* value = object.if_contains("url");
        if (!value || !value->is_string()) value = object.if_contains("thumbnail_url");
        if (!value || !value->is_string()) return {};
        media = resolve(*url, std::string(value->as_string()));
        if (media.empty()) return {};
    }
    Response response;
    if (*kind == Provider::Jumpshare)
    {
        auto direct = address; const auto query = direct.find('?'); const auto end = query == std::string::npos ? direct.size() : query;
        if (end && direct[end - 1] != '+') direct.insert(end, "+");
        response = fetcher(direct, MaxImageBytes);
        if (response.status != 200 || response.type == "text/html") response = fetcher(address, MaxImageBytes);
    }
    else response = fetcher(media, MaxImageBytes);
    if (response.status != 200) return {};
    if (response.type == "text/html")
    {
        if (*kind == Provider::Direct) return {};
        const auto origin = parse(response.url.empty() ? media : response.url); if (!origin) return {};
        const auto candidate = metadataImage(*origin, std::string(response.body.begin(), response.body.end()));
        if (candidate.empty()) return {};
        response = fetcher(candidate, MaxImageBytes);
    }
    if (response.status != 200 || response.body.size() > MaxImageBytes || (response.type != "image/png" && response.type != "image/jpeg" &&
        response.type != "image/gif" && response.type != "image/webp" && response.type != "application/octet-stream")) return {};
    return response;
}
}
