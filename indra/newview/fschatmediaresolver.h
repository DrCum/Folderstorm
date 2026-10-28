/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CHAT_MEDIA_RESOLVER_H
#define FS_CHAT_MEDIA_RESOLVER_H
#include "fschatmediahttp.h"
namespace fs_chat_media
{
using Fetch = std::function<Response(const std::string&, std::size_t)>;
Response resolveMedia(const std::string& address, const std::string& sites, const Fetch& fetcher);
}
#endif
