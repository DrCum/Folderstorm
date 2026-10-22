/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#include "fsgestureboardmodel.h"
#include "llsd.h"
#include "lluuid.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <utility>
namespace FSGestureBoard
{
bool validText(const std::string& text, bool empty)
{
    if (text.empty()) return empty;
    if (text.size() > 256) return false;
    size_t count = 0;
    bool visible = false;
    for (size_t i = 0; i < text.size();)
    {
        unsigned char lead = static_cast<unsigned char>(text[i++]);
        unsigned value = lead;
        int more = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { value &= 0x1f; more = 1; }
        else if (lead >= 0xe0 && lead <= 0xef) { value &= 0x0f; more = 2; }
        else if (lead >= 0xf0 && lead <= 0xf4) { value &= 0x07; more = 3; }
        else if (lead >= 0x80) return false;
        for (int j = 0; j < more; ++j)
        {
            if (i >= text.size()) return false;
            unsigned char next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if ((more == 1 && value < 0x80) || (more == 2 && value < 0x800) ||
            (more == 3 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff) || value < 32 || (value >= 127 && value <= 159)) return false;
        if (++count > 64) return false;
        visible |= value != ' ';
    }
    return visible;
}
namespace
{
bool id(const LLSD& value) { return value.isUUID() && value.asUUID().notNull(); }
}
namespace
{
bool readStyle(const LLSD& row, Style& style)
{
    if (row.has("color"))
    {
        if (!row["color"].isArray() || row["color"].size() != 3) return false;
        for (int c = 0; c < 3; ++c)
        {
            if (!row["color"][c].isReal() && !row["color"][c].isInteger()) return false;
            const double value = row["color"][c].asReal();
            if (!std::isfinite(value) || value < 0. || value > 1.) return false;
            style.color[c] = static_cast<float>(value);
        }
        style.custom_color = true;
    }
    if (row.has("theme_color"))
    {
        if (!row["theme_color"].isBoolean()) return false;
        style.theme_color = row["theme_color"].asBoolean();
        if (style.custom_color && style.theme_color) return false;
    }
    if (row.has("width") || row.has("height"))
    {
        if (!row["width"].isInteger() || !row["height"].isInteger()) return false;
        style.width = row["width"].asInteger(); style.height = row["height"].asInteger();
        if (style.width < MIN_TILE_WIDTH || style.width > MAX_TILE_WIDTH ||
            style.height < MIN_TILE_HEIGHT || style.height > MAX_TILE_HEIGHT) return false;
    }
    return true;
}
bool readDefaults(const LLSD& row, Style& style)
{
    if (!row.isMap() || row.size() > 4) return false;
    for (auto it = row.beginMap(); it != row.endMap(); ++it)
        if (it->first != "color" && it->first != "theme_color" && it->first != "width" && it->first != "height") return false;
    return readStyle(row, style);
}
void writeStyle(LLSD& row, const Style& style)
{
    if (style.custom_color) { row["color"] = LLSD::emptyArray(); for (float c : style.color) row["color"].append(LLSD::Real(c)); }
    if (style.theme_color) row["theme_color"] = true;
    if (style.width || style.height) { row["width"] = style.width; row["height"] = style.height; }
}
void writeDefaults(LLSD& row, const Style& style)
{
    if (style.custom_color || style.theme_color || style.width || style.height)
    { row["defaults"] = LLSD::emptyMap(); writeStyle(row["defaults"], style); }
}
}
bool decode(const LLSD& data, Collection& result)
{
    Collection parsed;
    if (data.isMap() && data.size() == 0) { result = parsed; return true; }
    if (!data.isMap() || data.size() < 3 || data.size() > 4 || !data["version"].isInteger() ||
        data["version"].asInteger() < 1 || data["version"].asInteger() > VERSION ||
        !data["boards"].isArray() || data["boards"].size() > static_cast<size_t>(MAX_BOARDS) || !data["selected"].isUUID()) return false;
    const bool modern = data["version"].asInteger() >= 3;
    for (auto it = data.beginMap(); it != data.endMap(); ++it)
        if (it->first != "version" && it->first != "boards" && it->first != "selected" && (it->first != "defaults" || !modern)) return false;
    if (data.has("defaults") && !readDefaults(data["defaults"], parsed.defaults)) return false;
    parsed.selected = data["selected"].asUUID().asString();
    if (data["selected"].asUUID().isNull()) parsed.selected.clear();
    std::set<std::string> board_ids, tile_ids, names;
    for (S32 b = 0; b < static_cast<S32>(data["boards"].size()); ++b)
    {
        const auto& entry = data["boards"][b];
        if (!entry.isMap() || entry.size() < 3 || entry.size() > 5 || !id(entry["id"]) || !entry["name"].isString() ||
            !validText(entry["name"].asString()) || !entry["tiles"].isArray() || entry["tiles"].size() > static_cast<size_t>(MAX_TILES)) return false;
        Board board; board.id = entry["id"].asUUID().asString(); board.name = entry["name"].asString();
        if (entry.has("compact"))
        { if (!entry["compact"].isBoolean()) return false; board.compact = entry["compact"].asBoolean(); }
        for (auto it = entry.beginMap(); it != entry.endMap(); ++it)
            if (it->first != "id" && it->first != "name" && it->first != "tiles" && it->first != "compact" && (it->first != "defaults" || !modern)) return false;
        if (entry.has("defaults") && !readDefaults(entry["defaults"], board.defaults)) return false;
        if (!board_ids.insert(board.id).second || !names.insert(board.name).second) return false;
        std::set<std::string> items;
        for (S32 t = 0; t < static_cast<S32>(entry["tiles"].size()); ++t)
        {
            const auto& row = entry["tiles"][t];
            if (!row.isMap() || row.size() < 2 || row.size() > 7 || !id(row["id"]) || !id(row["item"])) return false;
            Tile tile; tile.id = row["id"].asUUID().asString(); tile.item = row["item"].asUUID().asString();
            if (!tile_ids.insert(tile.id).second || !items.insert(tile.item).second) return false;
            if (row.has("label"))
            { if (!row["label"].isString() || !validText(row["label"].asString(), true)) return false; tile.label = row["label"].asString(); }
            if (!readStyle(row, tile)) return false;
            for (auto it = row.beginMap(); it != row.endMap(); ++it)
                if (it->first != "id" && it->first != "item" && it->first != "label" && it->first != "color" &&
                    it->first != "width" && it->first != "height" && (it->first != "theme_color" || !modern)) return false;
            board.tiles.push_back(tile);
        }
        parsed.boards.push_back(std::move(board));
    }
    if ((!parsed.boards.empty() && !board_ids.count(parsed.selected)) || (parsed.boards.empty() && !parsed.selected.empty())) return false;
    result = std::move(parsed); return true;
}
LLSD encode(const Collection& collection)
{
    LLSD data; data["version"] = VERSION; data["selected"] = LLUUID(collection.selected); data["boards"] = LLSD::emptyArray();
    writeDefaults(data, collection.defaults);
    for (const auto& board : collection.boards)
    {
        LLSD entry; entry["id"] = LLUUID(board.id); entry["name"] = board.name; entry["tiles"] = LLSD::emptyArray();
        entry["compact"] = board.compact; writeDefaults(entry, board.defaults);
        for (const auto& tile : board.tiles)
        {
            LLSD row; row["id"] = LLUUID(tile.id); row["item"] = LLUUID(tile.item);
            if (!tile.label.empty()) row["label"] = tile.label;
            writeStyle(row, tile); entry["tiles"].append(row);
        }
        data["boards"].append(entry);
    }
    return data;
}
Tile presentation(const Collection& collection, const Board& board, const Tile& tile)
{
    Tile result = tile; result.custom_color = false; result.theme_color = true;
    result.width = board.compact ? 100 : 132; result.height = board.compact ? 28 : 44;
    for (const Style* style : {&collection.defaults, &board.defaults, static_cast<const Style*>(&tile)})
    {
        if (style->custom_color || style->theme_color)
        { result.custom_color = style->custom_color; result.theme_color = style->theme_color; result.color = style->color; }
        if (style->width && style->height) { result.width = style->width; result.height = style->height; }
    }
    return result;
}
bool applyStyle(Board& board, const std::set<std::string>& ids, const Style& style, bool color, bool size)
{
    if (ids.empty() || (!color && !size) || (style.custom_color && style.theme_color)) return false;
    for (float c : style.color) if (!std::isfinite(c) || c < 0.f || c > 1.f) return false;
    if (size && (style.width || style.height) &&
        (style.width < MIN_TILE_WIDTH || style.width > MAX_TILE_WIDTH || style.height < MIN_TILE_HEIGHT || style.height > MAX_TILE_HEIGHT)) return false;
    // Validate the complete selection before editing anything.
    size_t found = 0; for (const auto& tile : board.tiles) if (ids.count(tile.id)) ++found;
    if (found != ids.size()) return false;
    for (auto& tile : board.tiles) if (ids.count(tile.id))
    {
        if (color) { tile.custom_color = style.custom_color; tile.theme_color = style.theme_color; tile.color = style.color; }
        if (size) { tile.width = style.width; tile.height = style.height; }
    }
    return true;
}
void Selection::choose(const Board& board, const std::string& id, bool range)
{
    const auto target = std::find_if(board.tiles.begin(), board.tiles.end(), [&](const Tile& tile) { return tile.id == id; });
    if (target == board.tiles.end()) return;
    const auto start = std::find_if(board.tiles.begin(), board.tiles.end(), [&](const Tile& tile) { return tile.id == anchor; });
    if (range && start != board.tiles.end())
        for (auto it = std::min(start, target); it <= std::max(start, target); ++it) ids.insert(it->id);
    else { if (!ids.erase(id)) ids.insert(id); anchor = id; }
}
void Selection::retain(const Board& board)
{
    std::set<std::string> live; for (const auto& tile : board.tiles) live.insert(tile.id);
    for (auto it = ids.begin(); it != ids.end();) if (!live.count(*it)) it = ids.erase(it); else ++it;
    if (!live.count(anchor)) anchor.clear();
}
void Selection::all(const Board& board)
{ ids.clear(); for (const auto& tile : board.tiles) ids.insert(tile.id); anchor = board.tiles.empty() ? "" : board.tiles.front().id; }
std::vector<Page> paginate(const Board& board, int width, int height, const Style& defaults)
{
    std::vector<Page> pages(1); Collection collection; collection.defaults = defaults;
    width = std::max(1, width); height = std::max(1, height);
    int x = 0, y = 0, row_height = 0;
    constexpr int gap = 4;
    for (int i = 0; i < static_cast<int>(board.tiles.size()); ++i)
    {
        const auto tile = presentation(collection, board, board.tiles[i]);
        const int w = std::clamp(tile.width, 1, width);
        const int h = std::clamp(tile.height, 1, height);
        if (x && x + w > width) { x = 0; y += row_height + gap; row_height = 0; }
        if (y + h > height)
        { pages.emplace_back(); x = 0; y = 0; row_height = 0; }
        pages.back().push_back({i, x, y, w, h});
        x += w + gap; row_height = std::max(row_height, h);
    }
    return pages;
}
Frame layoutFrame(int width, int height, int header_height, bool multiple_pages, bool selecting)
{
    const int controls_top = header_height + 4;
    const int grid_top = controls_top + 22 + 4 + (selecting ? 26 : 0);
    const int grid_width = std::max(1, width - 16);
    const int grid_height = std::max(1, height - grid_top - 6 - (multiple_pages ? 26 : 0));
    Frame frame;
    frame.boards = {8, controls_top, std::max(1, width - 108), 22};
    frame.status = {width - 98, controls_top + 2, 10, 18};
    frame.add = {width - 86, controls_top, 24, 22};
    frame.stop = {width - 60, controls_top, 24, 22};
    frame.options = {width - 32, controls_top, 24, 22};
    frame.selection = {8, controls_top + 26, grid_width, 22};
    frame.selected_count = {0, 2, std::max(1, grid_width - 152), 18};
    frame.edit_selected = {grid_width - 148, 0, 96, 22};
    frame.done_selecting = {grid_width - 48, 0, 48, 22};
    frame.grid = {8, grid_top, grid_width, grid_height};
    frame.empty = {12, grid_top + 4, std::max(1, grid_width - 8), std::min(24, grid_height - 4)};
    frame.pagination = {8, height - 28, grid_width, 22};
    frame.previous = {0, 0, 24, 22};
    frame.number = {30, 2, std::max(1, grid_width - 60), 18};
    frame.next = {grid_width - 24, 0, 24, 22};
    return frame;
}
Board* findBoard(Collection& collection, const std::string& id)
{ for (auto& board : collection.boards) if (board.id == id) return &board; return nullptr; }
const Board* findBoard(const Collection& collection, const std::string& id)
{ for (const auto& board : collection.boards) if (board.id == id) return &board; return nullptr; }
Tile* findTile(Board& board, const std::string& id)
{ for (auto& tile : board.tiles) if (tile.id == id) return &tile; return nullptr; }
const Tile* findTile(const Board& board, const std::string& id)
{ for (const auto& tile : board.tiles) if (tile.id == id) return &tile; return nullptr; }
bool sameBoard(const Board& left, const Board& right)
{
    if (left.id != right.id || left.name != right.name || left.compact != right.compact || !sameStyle(left.defaults, right.defaults) || left.tiles.size() != right.tiles.size()) return false;
    for (size_t i = 0; i < left.tiles.size(); ++i)
    {
        const auto& a = left.tiles[i]; const auto& b = right.tiles[i];
        if (a.id != b.id || a.item != b.item || a.label != b.label || !sameStyle(a, b)) return false;
    }
    return true;
}
bool moveTile(Board& board, const std::string& id, int index)
{
    const auto it = std::find_if(board.tiles.begin(), board.tiles.end(), [&](const Tile& tile) { return tile.id == id; });
    if (it == board.tiles.end() || index < 0 || index >= static_cast<int>(board.tiles.size())) return false;
    Tile tile = *it; board.tiles.erase(it); board.tiles.insert(board.tiles.begin() + index, std::move(tile)); return true;
}
float foreground(const std::array<float, 3>& background)
{
    double luminance = 0.; const double weight[] = {.2126, .7152, .0722};
    for (int i = 0; i < 3; ++i)
    { double c = std::clamp(static_cast<double>(background[i]), 0., 1.); luminance += weight[i] * (c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4)); }
    return (luminance + .05) / .05 >= 1.05 / (luminance + .05) ? 0.f : 1.f;
}
bool pendingMatches(const std::string& account, const std::string& session, const std::string& current_account,
                    const std::string& current_session, unsigned long generation, unsigned long current_generation,
                    bool same_target, bool permitted, float elapsed)
{
    return !account.empty() && !session.empty() && account == current_account && session == current_session &&
        generation == current_generation && same_target && permitted && std::isfinite(elapsed) && elapsed >= 0.f && elapsed < 30.f;
}
}
