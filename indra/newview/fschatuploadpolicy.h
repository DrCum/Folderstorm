/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_UPLOAD_POLICY_H
#define FS_CHAT_UPLOAD_POLICY_H
#include "fschatmediapolicy.h"
#include <atomic>
#include <memory>

namespace fs_chat_upload
{
constexpr std::size_t MaxFileBytes = 10 * 1024 * 1024;
constexpr std::size_t MaxStagedBytes = 32 * 1024 * 1024;
constexpr std::size_t MaxReplyBytes = 256 * 1024;
constexpr unsigned MaxAttachments = 4;
struct Config
{
    bool enabled = true, automatic = false;
    std::string origin = "https://images.sodie.online", folder = "jkydgzspc7t9snhxy7c32yvo";
    bool valid() const
    {
        const auto url = fs_chat_media::parse(origin);
        if (!url || url->path != "/" || origin.find_first_of("?#") != std::string::npos ||
            folder.empty() || folder.size() > 128) return false;
        return std::all_of(folder.begin(), folder.end(), [](unsigned char c)
        { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; });
    }
    std::string base() const { return origin.substr(0, origin.find_last_not_of('/') + 1); }
    std::string destination() const { return base() + "/" + folder; }
};
inline bool sameDestination(const Config& a, const Config& b)
{ return a.base() == b.base() && a.folder == b.folder; }
inline std::string mime(const std::vector<unsigned char>& data)
{
    if (data.size() >= 8 && std::equal(data.begin(), data.begin() + 8, reinterpret_cast<const unsigned char*>("\x89PNG\r\n\x1a\n"))) return "image/png";
    if (data.size() >= 3 && data[0] == 255 && data[1] == 216 && data[2] == 255) return "image/jpeg";
    if (data.size() >= 6 && (std::equal(data.begin(), data.begin() + 6, reinterpret_cast<const unsigned char*>("GIF87a")) ||
        std::equal(data.begin(), data.begin() + 6, reinterpret_cast<const unsigned char*>("GIF89a")))) return "image/gif";
    if (data.size() >= 12 && std::equal(data.begin(), data.begin() + 4, reinterpret_cast<const unsigned char*>("RIFF")) &&
        std::equal(data.begin() + 8, data.begin() + 12, reinterpret_cast<const unsigned char*>("WEBP"))) return "image/webp";
    return {};
}
inline std::string extension(const std::string& type)
{ return type == "image/png" ? "png" : type == "image/jpeg" ? "jpg" : type == "image/gif" ? "gif" : type == "image/webp" ? "webp" : ""; }
enum class State { Preparing, Review, Queued, Uploading, Processing, Complete, Failed, Uncertain, Cancelled };
inline bool blocksSend(State state)
{ return state == State::Preparing || state == State::Review || state == State::Queued || state == State::Uploading || state == State::Processing; }
struct Progress
{
    std::atomic<bool> cancelled{false}, submitted{false};
    std::atomic<unsigned> percent{0};
};
struct Reply
{
    State state = State::Failed;
    std::string id, url, expiry;
    std::string error = "Upload failed.";
};
bool quotaFailure(const std::string& json);
Reply parseReply(const Config& config, const std::string& json, const std::string& expectedMime);
Reply upload(const Config& config, const std::vector<unsigned char>& data, const std::string& type,
             const std::string& boundary, const std::string& caFile, Progress& progress);
}
#endif
