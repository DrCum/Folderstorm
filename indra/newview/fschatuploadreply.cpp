/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fschatuploadpolicy.h"
#include <boost/json.hpp>
namespace fs_chat_upload
{
bool quotaFailure(const std::string& json)
{
    if (json.size() > MaxReplyBytes) return false;
    boost::system::error_code error; boost::json::parse_options options; options.max_depth = 8;
    const auto value = boost::json::parse(json, error, {}, options);
    if (error || !value.is_object()) return false;
    const auto match = [](const boost::json::object& object)
    {
        for (const auto* key : {"code", "errorCode"}) if (const auto* code = object.if_contains(key))
        {
            if (code->is_string() && (code->as_string() == "E5002" || code->as_string() == "5002")) return true;
            if (code->is_int64() && code->as_int64() == 5002) return true;
        }
        return false;
    };
    if (match(value.as_object())) return true;
    const auto* nested = value.as_object().if_contains("error");
    return nested && nested->is_object() && match(nested->as_object());
}

Reply parseReply(const Config& config, const std::string& json, const std::string& expectedMime)
{
    if (!config.valid() || json.size() > MaxReplyBytes || extension(expectedMime).empty()) return {};
    boost::system::error_code error; boost::json::parse_options options; options.max_depth = 8;
    const auto value = boost::json::parse(json, error, {}, options);
    if (error || !value.is_object()) return {};
    const auto& object = value.as_object(); const auto* files = object.if_contains("files");
    if (!files || !files->is_array() || files->as_array().size() != 1 || !files->as_array()[0].is_object()) return {};
    const auto& file = files->as_array()[0].as_object();
    const auto string = [&](const char* key) -> std::string
    { const auto* item = file.if_contains(key); return item && item->is_string() ? std::string(item->as_string()) : ""; };
    const auto id = string("id"), name = string("name"), type = string("type"), address = string("url");
    const auto parsed = fs_chat_media::parse(address), origin = fs_chat_media::parse(config.base());
    if (id.empty() || id.size() > 128 || name.empty() || name.size() > 255 || type != expectedMime || !parsed ||
        !origin || parsed->host != origin->host || !fs_chat_media::imagePath(*parsed) ||
        name.find_first_of("/\\?#\r\n") != std::string::npos || name == "." || name == ".." ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; })) return {};
    // File names are encoded as one path segment, never concatenated as a path.
    std::string escaped; constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned char c : name)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') escaped += static_cast<char>(c);
        else { escaped += '%'; escaped += hex[c >> 4]; escaped += hex[c & 15]; }
    }
    auto url = address;
    if (origin->host == "images.sodie.online") url = config.base() + "/raw/" + escaped;
    // Other Zipline servers must return a direct image URL; do not accept API links.
    else if (parsed->path.compare(0, 5, "/api/") == 0 || parsed->path.compare(0, 6, "/view/") == 0) return {};
    const auto direct = fs_chat_media::parse(url);
    if (!direct || !fs_chat_media::imagePath(*direct)) return {};
    const auto* pending = file.if_contains("pending");
    if (pending && !pending->is_bool()) return {};
    Reply reply; reply.id = id; reply.url = url; reply.error.clear();
    reply.state = pending && pending->as_bool() ? State::Processing : State::Complete;
    if (const auto* expiry = object.if_contains("deletesAt"); expiry && expiry->is_string() && expiry->as_string().size() <= 128)
        reply.expiry = std::string(expiry->as_string());
    return reply;
}
}
