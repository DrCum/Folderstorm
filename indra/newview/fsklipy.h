/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_KLIPY_H
#define FS_KLIPY_H
#include "fschatmediahttp.h"
#include "fsgifboardmodel.h"
namespace fs_klipy
{
constexpr unsigned PageSize = 24;
enum class Kind { Trending, Search, Recent, Items, Categories };
struct Request
{
    Kind kind = Kind::Trending;
    std::string key, customer, query, locale;
    std::vector<std::string> slugs;
    unsigned page = 1;
};
struct Item { std::string slug, title, preview, share; bool available = false; };
struct Result
{
    std::vector<Item> items;
    std::vector<std::string> categories;
    bool more = false;
    std::string error;
    unsigned retryAfter = 0;
};
bool keyValid(const std::string& key);
bool slugValid(const std::string& slug);
bool mediaURL(const std::string& url);
std::string requestURL(const Request& request);
std::string shareURL(const std::string& key, const std::string& slug);
std::string shareBody(const std::string& customer, const std::string& query);
std::string reportURL(const std::string& key,const std::string& slug);
std::string reportBody(const std::string& customer,const std::string& reason);
Result parse(const fs_chat_media::Response& response, Kind kind = Kind::Trending);
}
#endif
