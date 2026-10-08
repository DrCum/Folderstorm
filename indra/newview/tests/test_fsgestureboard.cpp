/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 * This library is free software under the GNU Lesser General Public License,
 * version 2.1 or later; distributed WITHOUT ANY WARRANTY.
 * $/LicenseInfo$
 */
#include "../fsgestureboardmodel.h"
#include "llsd.h"
#include "llsdutil.h"
#include "lluuid.h"
#include <cmath>
#include <iostream>
#include <limits>
using namespace FSGestureBoard;
namespace
{
int failures = 0;
void expect(bool value, const char* message) { if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; } }
std::string uuid() { return LLUUID::generateNewID().asString(); }
Collection fixture()
{
    Collection c; Board b; b.id = uuid(); b.name = "Party 🚗";
    Tile t; t.id = uuid(); t.item = uuid(); t.label = "Horn"; t.custom_color = true; t.color = {.9f, .5f, .1f}; b.tiles.push_back(t);
    t.id = uuid(); t.item = uuid(); t.label.clear(); t.custom_color = false; b.tiles.push_back(t);
    c.selected = b.id; c.boards.push_back(b); return c;
}
}
int main()
{
    auto source = fixture(); const auto data = encode(source); Collection parsed;
    expect(decode(data, parsed) && encode(parsed) == data, "Ordered Unicode labels/colors and theme/name defaults roundtrip");
    expect(!data["boards"][0]["tiles"][1].has("label") && !data["boards"][0]["tiles"][1].has("color"), "Defaults follow Inventory/skin rather than freezing presentation");
    for (const char* excluded : {"asset", "sound", "chat", "contents", "filters", "selection"})
        expect(!data["boards"][0]["tiles"][0].has(excluded), "Only references and tile presentation are serialized");
    auto reject = [&](LLSD bad)
    { Collection untouched = source; expect(!decode(bad, untouched) && encode(untouched) == data, "Rejected data cannot replace an existing collection"); };
    auto bad = data; bad["version"] = 3; reject(bad);
    bad = data; bad["selected"] = LLUUID::generateNewID(); reject(bad);
    bad = data; bad["boards"][0]["tiles"][0]["item"] = "uuid text"; reject(bad);
    bad = data; bad["boards"][0]["tiles"][0]["id"] = LLUUID::null; reject(bad);
    bad = data; bad["boards"][0]["tiles"].append(bad["boards"][0]["tiles"][0]); reject(bad);
    bad = data; bad["boards"][0]["tiles"][0]["chat"] = "/trigger"; reject(bad);
    for (double channel : {-1., 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    { bad = data; bad["boards"][0]["tiles"][0]["color"][0] = channel; reject(bad); }
    bad = data; bad["boards"][0]["tiles"][0]["color"][0] = "0.5"; reject(bad);
    auto bounded = source;
    while (bounded.boards.front().tiles.size() < MAX_TILES) { Tile t; t.id = uuid(); t.item = uuid(); bounded.boards.front().tiles.push_back(t); }
    expect(decode(encode(bounded), parsed), "Maximum tile count is accepted");
    Tile extra; extra.id = uuid(); extra.item = uuid(); bounded.boards.front().tiles.push_back(extra); reject(encode(bounded));
    bounded = source;
    while (bounded.boards.size() < MAX_BOARDS) { Board b; b.id = uuid(); b.name = "Board " + std::to_string(bounded.boards.size()); bounded.boards.push_back(b); }
    expect(decode(encode(bounded), parsed), "Maximum board count is accepted");
    Board extra_board; extra_board.id = uuid(); extra_board.name = "Too many"; bounded.boards.push_back(extra_board); reject(encode(bounded));
    expect(validText("😊 Bonjour 日本語") && validText(std::string(64, 'a')) && validText("", true), "Unicode and optional label reset accepted");
    expect(!validText(std::string(65, 'a')) && !validText("  ") && !validText("x\ny") && !validText(std::string("\xc0\x80", 2)) &&
           !validText(std::string("\xed\xa0\x80", 3)) && !validText(std::string("\xf4\x90\x80\x80", 4)), "Length, controls, overlong UTF-8, surrogates and out-of-range Unicode rejected");
    auto board = source.boards.front(); const auto first = board.tiles[0].id, second = board.tiles[1].id;
    expect(moveTile(board, first, 1) && board.tiles[0].id == second && board.tiles[1].id == first, "Reordering is independent of labels and references");
    expect(!moveTile(board, first, -1) && !moveTile(board, first, 2) && !moveTile(board, uuid(), 0), "Invalid moves cannot corrupt ordering");
    expect(encode(source) == data, "Editing/reordering a copy leaves the saved source unchanged");
    auto legacy = data; legacy["version"] = 1; legacy["boards"][0].erase("compact");
    expect(decode(legacy, parsed) && parsed.boards.front().compact && parsed.boards.front().tiles.front().width == 0,
           "Existing version-one boards migrate to compact defaults without losing references");
    auto sized = source; sized.boards.front().compact = false;
    sized.boards.front().tiles.front().width = 73; sized.boards.front().tiles.front().height = 37;
    expect(decode(encode(sized), parsed) && parsed.boards.front().tiles.front().width == 73 &&
           parsed.boards.front().tiles.front().height == 37 && !parsed.boards.front().compact,
           "Individual sizes and board density roundtrip");
    for (int width : {-1, 0, MIN_TILE_WIDTH - 1, MAX_TILE_WIDTH + 1})
    { bad = encode(sized); bad["boards"][0]["tiles"][0]["width"] = width; reject(bad); }
    for (int height : {-1, 0, MIN_TILE_HEIGHT - 1, MAX_TILE_HEIGHT + 1})
    { bad = encode(sized); bad["boards"][0]["tiles"][0]["height"] = height; reject(bad); }
    bad = encode(sized); bad["boards"][0]["tiles"][0].erase("height"); reject(bad);
    bad = encode(sized); bad["boards"][0]["tiles"][0]["width"] = 72.5; reject(bad);
    bad = data; bad["boards"][0]["compact"] = "yes"; reject(bad);
    bad = data; bad["boards"][0]["unknown"] = true; reject(bad);
    Board geometry; geometry.compact = true;
    for (int i = 0; i < MAX_TILES; ++i) { Tile t; t.id = uuid(); t.item = uuid(); geometry.tiles.push_back(t); }
    geometry.tiles[1].width = 180; geometry.tiles[1].height = 75;
    geometry.tiles[3].width = 48; geometry.tiles[3].height = 24;
    for (const auto& viewport : {std::array<int, 2>{204, 30}, {264, 126}, {600, 320}, {1, 1}})
    {
        const auto pages = paginate(geometry, viewport[0], viewport[1]);
        int next = 0;
        for (const auto& page : pages)
        {
            expect(!page.empty(), "No empty pages when tiles exist");
            for (size_t i = 0; i < page.size(); ++i)
            {
                const auto& p = page[i];
                expect(p.index == next++, "Every tile appears once in original order across pages");
                expect(p.x >= 0 && p.y >= 0 && p.width > 0 && p.height > 0 &&
                       p.x + p.width <= viewport[0] && p.y + p.height <= viewport[1], "Tiles stay inside the page");
                for (size_t j = 0; j < i; ++j)
                {
                    const auto& q = page[j];
                    expect(p.x >= q.x + q.width || q.x >= p.x + p.width || p.y >= q.y + q.height || q.y >= p.y + p.height,
                           "Mixed-size tiles never overlap");
                }
            }
        }
        expect(next == MAX_TILES, "Pagination retains all tiles even in tiny windows");
    }
    auto pages = paginate(sized.boards.front(), 264, 126);
    expect(pages.size() == 1 && pages[0][0].width == 73 && pages[0][0].height == 37,
           "Individual requested dimensions override regular board density");
    sized.boards.front().tiles.front().label = std::string(64, 'w');
    auto renamed = paginate(sized.boards.front(), 264, 126);
    expect(renamed[0][0].width == pages[0][0].width && renamed[0][0].height == pages[0][0].height,
           "Long labels never change the requested tile size");
    expect(paginate(Board{}, 204, 30).size() == 1, "Empty boards need no navigation");
    expect(foreground({1.f, 1.f, 1.f}) == 0.f && foreground({0.f, 0.f, 0.f}) == 1.f, "Light/dark custom fills choose readable foregrounds");
    expect(pendingMatches("account", "session", "account", "session", 7, 7, true, true, 29.f), "A ready permitted current-session play can complete within its deadline");
    expect(!pendingMatches("account", "session", "other", "session", 7, 7, true, true, 0.f) &&
           !pendingMatches("account", "session", "account", "next login", 7, 7, true, true, 0.f) &&
           !pendingMatches("account", "session", "account", "session", 7, 8, true, true, 0.f) &&
           !pendingMatches("account", "session", "account", "session", 7, 7, false, true, 0.f) &&
           !pendingMatches("account", "session", "account", "session", 7, 7, true, false, 0.f) &&
           !pendingMatches("account", "session", "account", "session", 7, 7, true, true, 30.f),
           "Account/login, cancellation/edit generations, target changes, restrictions and timeout all prevent delayed playback");
    expect(decode(LLSD::emptyMap(), parsed) && parsed.boards.empty(), "Initial account defaults are valid without inventing gestures");
    if (!failures) std::cout << "Gesture board model and cancellation checks passed\n";
    return failures ? 1 : 0;
}
