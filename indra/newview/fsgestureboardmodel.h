/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#ifndef FS_GESTURE_BOARD_MODEL_H
#define FS_GESTURE_BOARD_MODEL_H
#include <array>
#include <set>
#include <string>
#include <vector>
class LLSD;
namespace FSGestureBoard
{
constexpr int VERSION = 3, MAX_BOARDS = 16, MAX_TILES = 1024;
constexpr int MIN_TILE_WIDTH = 48, MAX_TILE_WIDTH = 320, MIN_TILE_HEIGHT = 24, MAX_TILE_HEIGHT = 160;
struct Style
{
    bool custom_color = false;
    std::array<float, 3> color{.3f, .3f, .3f};
    int width = 0, height = 0; // Both zero inherits the parent size/density.
    bool theme_color = false; // Explicit theme color, even with a custom parent.
};
struct Tile : Style { std::string id, item, label; };
struct Board { std::string id, name; std::vector<Tile> tiles; bool compact = true; Style defaults; };
struct Collection { std::string selected; std::vector<Board> boards; Style defaults; };
inline bool sameStyle(const Style& a, const Style& b)
{ return a.custom_color == b.custom_color && a.color == b.color && a.width == b.width && a.height == b.height && a.theme_color == b.theme_color; }
// Resolve each property separately: tile > board > all boards > skin/density.
Tile presentation(const Collection& collection, const Board& board, const Tile& tile);
bool applyStyle(Board& board, const std::set<std::string>& ids, const Style& style, bool color, bool size);
struct Selection
{
    std::set<std::string> ids;
    std::string anchor;
    void choose(const Board& board, const std::string& id, bool range);
    void retain(const Board& board);
    void all(const Board& board);
    void clear() { ids.clear(); anchor.clear(); }
};
struct Placement { int index, x, y, width, height; }; // Top-left coordinates.
using Page = std::vector<Placement>;
struct Rect { int x, y, width, height; }; // Top-left within the owning view.
struct Frame
{
    Rect boards, status, add, stop, options, grid, empty, pagination;
    Rect selection, selected_count, edit_selected, done_selecting;
    Rect previous, number, next; // Local to pagination.
};
// Uses final floater dimensions, after native header stretch/saved-rect restore.
Frame layoutFrame(int width, int height, int header_height, bool multiple_pages, bool selecting = false);
// Fixed presentation sizes, independent of gesture names. Only a viewport
// smaller than a tile may constrain its rendered size; its saved size is kept.
std::vector<Page> paginate(const Board& board, int width, int height, const Style& defaults = Style{});
bool validText(const std::string& text, bool empty = false);
bool decode(const LLSD& data, Collection& result);
LLSD encode(const Collection& collection);
Board* findBoard(Collection& collection, const std::string& id);
const Board* findBoard(const Collection& collection, const std::string& id);
Tile* findTile(Board& board, const std::string& id);
const Tile* findTile(const Board& board, const std::string& id);
bool sameBoard(const Board& left, const Board& right);
bool moveTile(Board& board, const std::string& id, int index);
float foreground(const std::array<float, 3>& background);
// Pure cancellation gate used by the delayed native playback path.
bool pendingMatches(const std::string& account, const std::string& session,
                    const std::string& current_account, const std::string& current_session,
                    unsigned long generation, unsigned long current_generation,
                    bool same_target, bool permitted, float elapsed);
}
#endif
