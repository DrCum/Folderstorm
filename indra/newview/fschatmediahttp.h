/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_HTTP_H
#define FS_CHAT_MEDIA_HTTP_H
#include "fschatmediapolicy.h"
#include <atomic>
#include <functional>
namespace fs_chat_media
{
struct Response { std::vector<unsigned char> body; std::string type, location; long status = 0; std::string url; unsigned retryAfter = 0; };
// Isolated handle: no cookies, credentials, redirects, browser or proxy state.
Response fetch(const std::string& url, const std::string& caFile, std::size_t limit, const std::atomic<bool>& cancelled, bool head = false);
// Fixed-origin KLIPY API only, no redirects. Empty body is GET, otherwise JSON POST.
Response klipyRequest(const std::string& url, const std::string& caFile, const std::string& body, const std::atomic<bool>& cancelled);
}
#endif
