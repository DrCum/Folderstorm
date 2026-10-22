/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_HTTP_H
#define FS_CHAT_MEDIA_HTTP_H
#include "fschatmediapolicy.h"
#include <atomic>
#include <functional>
namespace fs_chat_media
{
struct Response { std::vector<unsigned char> body; std::string type, location; long status = 0; std::string url; };
// Isolated handle: no cookies, credentials, redirects, browser or proxy state.
Response fetch(const std::string& url, const std::string& caFile, std::size_t limit, const std::atomic<bool>& cancelled);
}
#endif
