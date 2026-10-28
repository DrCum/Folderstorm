/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_GIF_BOARD_MODEL_H
#define FS_GIF_BOARD_MODEL_H
#include "fschatmediapolicy.h"
#include <tuple>
namespace fs_gif_board
{
constexpr std::size_t MaxBookmarks = 10000, MaxLibraryBytes = 4 * 1024 * 1024;
inline bool boundedXML(const std::string& document)
{
    if(document.empty()||document.size()>MaxLibraryBytes||document.find("<!")!=std::string::npos)return false;
    int depth=0;unsigned nodes=0;
    for(std::size_t at=0;(at=document.find('<',at))!=std::string::npos;)
    {
        const auto end=document.find('>',at+1);if(end==std::string::npos||end==at+1||++nodes>300000)return false;
        if(document[at+1]!='?'){if(document[at+1]=='/')--depth;else if(document[end-1]!='/')++depth;if(depth<0||depth>12)return false;}
        at=end+1;
    }
    return depth==0;
}
struct Item
{
    std::string id, url, title, tags, collection, provider = "my", slug;
    bool favourite = false;
    bool operator==(const Item& b) const
    { return std::tie(id, url, title, tags, collection, provider, slug, favourite) == std::tie(b.id, b.url, b.title, b.tags, b.collection, b.provider, b.slug, b.favourite); }
};
inline bool text(const std::string& value, std::size_t maximum)
{ return value.size() <= maximum && std::none_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }); }
inline bool valid(const Item& item)
{
    if (item.id.empty() || !text(item.id, 64) || !text(item.title, 128) || !text(item.tags, 256) || !text(item.collection, 64)) return false;
    if (item.provider == "klipy") return item.url.empty() && !item.slug.empty() && text(item.slug, 256) &&
        std::all_of(item.slug.begin(), item.slug.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; });
    const auto url = fs_chat_media::parse(item.url);
    return item.provider == "my" && item.slug.empty() && url && !fs_chat_media::site(url->host, "klipy.com") &&
        !fs_chat_media::site(url->host, "klipy.co") && fs_chat_media::provider(*url).has_value();
}
inline bool upsert(std::vector<Item>& items, Item item)
{
    if (!valid(item)) return false;
    auto old = std::find_if(items.begin(), items.end(), [&](const Item& existing) { return existing.id == item.id; });
    const auto duplicate = std::find_if(items.begin(), items.end(), [&](const Item& existing)
    { return item.provider == existing.provider && (item.provider == "klipy" ? item.slug == existing.slug : item.url == existing.url); });
    if (old != items.end() && duplicate != items.end() && old != duplicate) return false;
    if (old == items.end()) old = duplicate;
    if (old != items.end()) { item.id = old->id; *old = std::move(item); return true; }
    if (items.size() >= MaxBookmarks) return false;
    items.push_back(std::move(item)); return true;
}
inline std::vector<Item> filter(const std::vector<Item>& items, const std::string& query, const std::string& collection, bool favourites)
{
    std::vector<Item> result; const auto key = fs_chat_media::lower(query);
    for (const auto& item : items)
        if (item.provider == "my" && (!favourites || item.favourite) && (collection.empty() || item.collection == collection) &&
            (key.empty() || fs_chat_media::lower(item.title + " " + item.tags + " " + item.collection + " " + item.url).find(key) != std::string::npos)) result.push_back(item);
    return result;
}
struct Rect { int left, bottom, right, top; };
inline unsigned visiblePercent(Rect tile, const std::vector<Rect>& clips)
{
    const auto area = [](Rect r) -> std::uint64_t { return r.right > r.left && r.top > r.bottom ? std::uint64_t(std::int64_t(r.right) - r.left) * std::uint64_t(std::int64_t(r.top) - r.bottom) : 0; };
    const auto full = area(tile); if (!full) return 0;
    for (const auto clip : clips)
    { tile.left = (std::max)(tile.left, clip.left); tile.right = (std::min)(tile.right, clip.right); tile.top = (std::min)(tile.top, clip.top); tile.bottom = (std::max)(tile.bottom, clip.bottom); }
    return static_cast<unsigned>(static_cast<long double>(area(tile)) * 100 / full);
}
struct Identity
{
    std::string account, login, grid, editor, destination;
    unsigned generation = 0;
    bool operator==(const Identity& b) const
    { return std::tie(account, login, grid, editor, destination, generation) == std::tie(b.account, b.login, b.grid, b.editor, b.destination, b.generation); }
};
inline bool autoplay(int mode, unsigned visible, unsigned threshold, bool hovered, bool selected, bool reduced, bool paused)
{ return !paused && !reduced && visible > 0 && (mode == 0 ? visible >= std::clamp(threshold, 1u, 100u) : mode == 1 && (hovered || selected)); }
}
#endif
