"""Exercise production gesture-board playback against portable native boundaries.

Runs the actual resolution, availability, playback, stop and pending-load methods;
this does not simulate the server, native UI or audible audio output.
Run: python3 -m unittest discover -s indra/newview/tests -p test_fsgestureboardsounds.py -v
"""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
VIEWER = ROOT / "indra/newview"


def function(source, signature):
    begin = source.index(signature)
    start = source.index("{", begin)
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end] + "\n"


BOUNDARY = r'''
#include "fsgestureboardmodel.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
struct LLUUID {
    std::string value;
    LLUUID() = default;
    explicit LLUUID(std::string v): value(std::move(v)) {}
    std::string asString() const { return value; }
    bool isNull() const { return value.empty(); }
    bool notNull() const { return !isNull(); }
    bool operator==(const LLUUID& other) const { return value == other.value; }
    bool operator!=(const LLUUID& other) const { return !(*this == other); }
    bool operator<(const LLUUID& other) const { return value < other.value; }
};
namespace LLAssetType { enum EType { AT_NONE, AT_SOUND, AT_GESTURE, AT_TEXTURE, AT_LINK }; }
namespace LLFolderType { constexpr int FT_TRASH = 1; }
struct Item {
    LLAssetType::EType type = LLAssetType::AT_SOUND;
    LLUUID asset{"sound-asset"};
    std::string name = "Inventory name";
    bool getIsLinkType() const { return type == LLAssetType::AT_LINK; }
    LLAssetType::EType getType() const { return type; }
    LLUUID getAssetUUID() const { return asset; }
    std::string getName() const { return name; }
};
struct Inventory {
    bool usable = true;
    std::map<LLUUID, Item> items;
    std::map<LLUUID, LLUUID> links;
    std::set<LLUUID> trashed;
    bool isInventoryUsable() const { return usable; }
    const Item* getItem(const LLUUID& id) const {
        auto found = items.find(id); return found == items.end() ? nullptr : &found->second;
    }
    LLUUID getLinkedItemID(const LLUUID& id) const {
        auto found = links.find(id); return found == links.end() ? id : found->second;
    }
    LLUUID findCategoryUUIDForType(int) const { return LLUUID("trash"); }
    bool isObjectDescendentOf(const LLUUID& id, const LLUUID&) const { return trashed.count(id); }
} gInventory;
bool gDisconnected = false;
namespace LLApp { bool quitting = false; bool isQuitting() { return quitting; } }
constexpr int STATE_STARTED = 1;
namespace LLStartUp { int state = STATE_STARTED; int getStartupState() { return state; } }
struct Agent {
    LLUUID account{"account"}, session{"session"};
    bool region = true;
    LLUUID getID() const { return account; }
    LLUUID getSessionID() const { return session; }
    void* getRegion() const { return region ? const_cast<Agent*>(this) : nullptr; }
} gAgent;
struct Settings {
    bool exists = true, gestures = true;
    bool controlExists(const char*) const { return exists; }
    bool getBOOL(const char*) const { return gestures; }
} gSavedPerAccountSettings;
namespace RlvActions { bool gestures = true; bool canPlayGestures() { return gestures; } }
float now = 0.f;
struct LLTimer {
    float started = now;
    void reset() { started = now; }
    float getElapsedTimeF32() const { return now - started; }
};
struct LLGestureMgr {
    std::map<LLUUID, bool*> loaded;
    std::set<LLUUID> playing;
    int plays = 0, activates = 0, stops = 0;
    static LLGestureMgr& instance() { static LLGestureMgr result; return result; }
    bool isGesturePlaying(const LLUUID& id) const { return playing.count(id); }
    bool isGestureActive(const LLUUID& id) const { return loaded.count(id); }
    const auto& getActiveGestures() const { return loaded; }
    void playGesture(const LLUUID& id) { playing.insert(id); ++plays; }
    void stopGesture(const LLUUID& id) { playing.erase(id); ++stops; }
    void activateGestureWithAsset(const LLUUID& id, const LLUUID&, bool, bool) {
        loaded[id] = nullptr; ++activates;
    }
};
std::vector<LLUUID> sounds;
void send_sound_trigger(const LLUUID& asset, float gain) { assert(gain == 1.f); sounds.push_back(asset); }
class FSGestureBoardController {
public:
    bool available() const;
    bool permitted(LLAssetType::EType type = LLAssetType::AT_GESTURE) const;
    bool resolve(const LLUUID&, LLUUID&, LLUUID&, std::string&, std::string&, LLAssetType::EType* = nullptr) const;
    std::string tileState(const FSGestureBoard::Tile&) const;
    bool play(const std::string&, const std::string&, const LLUUID& = LLUUID());
    void setStatus(const std::string&);
    void cancelPending();
    void cancelPendingForWindow(const LLUUID&);
    void cancelPendingForBoard(const std::string&);
    void stopBoard(const std::string&);
    void poll();
    bool valid() const { return mValid && available(); }
    // Settings persistence isn't part of this transport-boundary harness.
    void refresh() {}
    struct Pending {
        std::string board, tile;
        LLUUID reference, item, asset, account, session, owner;
        unsigned long generation = 0;
        LLTimer timer;
    };
    FSGestureBoard::Collection mData;
    LLUUID mAccount{"account"}, mSession{"session"};
    bool mValid = true;
    unsigned long mRevision = 0, mGeneration = 0;
    std::string mStatus;
    std::map<LLUUID, Pending> mPending;
    std::map<LLUUID, std::string> mStarted;
    LLTimer mPollTimer;
};
'''

CHECKS = r'''
int main() {
    using namespace FSGestureBoard;
    const LLUUID sound("sound"), link("link"), gesture("gesture");
    gInventory.items[sound] = Item{};
    gInventory.items[gesture] = Item{LLAssetType::AT_GESTURE, LLUUID("gesture-asset"), "Gesture"};
    gInventory.items[link] = Item{LLAssetType::AT_LINK, LLUUID(), "Linked sound"};
    gInventory.links[link] = sound;
    FSGestureBoardController c;
    Tile st; st.id = "sound-tile"; st.item = "link";
    Tile gt; gt.id = "gesture-tile"; gt.item = "gesture";
    Board board; board.id = "board"; board.tiles = {st, gt};
    c.mData.selected = board.id; c.mData.boards.push_back(board);
    LLUUID item, asset; std::string name, reason;
    LLAssetType::EType type = LLAssetType::AT_NONE;
    assert(c.resolve(link, item, asset, name, reason, &type));
    assert(item == sound && asset == LLUUID("sound-asset") && name == "Linked sound" && type == LLAssetType::AT_SOUND);
    assert(c.resolve(sound, item, asset, name, reason, &type));
    assert(item == sound && name == "Inventory name" && type == LLAssetType::AT_SOUND);
    assert(c.tileState(st) == "sound_ready");
    assert(c.play("board", st.id) && sounds.size() == 1 && sounds.back() == asset);
    assert(c.mPending.empty() && c.mStarted.empty());
    auto& manager = LLGestureMgr::instance();
    assert(manager.plays == 0 && manager.activates == 0);
    // Replaying an explicit click sends once; polling never replays a sound.
    assert(c.play("board", st.id) && sounds.size() == 2);
    c.mData.boards.front().tiles.front().item = "sound";
    assert(c.play("board", st.id) && sounds.size() == 3 && sounds.back() == asset);
    c.mData.boards.front().tiles.front().item = "link";
    now += 1.f; c.poll(); assert(sounds.size() == 3);
    c.stopBoard("board"); assert(sounds.size() == 3 && manager.stops == 0);
    assert(!c.play("other-board", st.id) && !c.play("board", "removed"));
    gSavedPerAccountSettings.gestures = false; RlvActions::gestures = false;
    assert(c.play("board", st.id) && !c.play("board", gt.id));
    assert(c.tileState(gt) == "blocked");
    assert(!c.permitted(LLAssetType::AT_TEXTURE));
    const auto before = sounds.size();
    gDisconnected = true; assert(!c.play("board", st.id)); gDisconnected = false;
    gAgent.region = false; assert(!c.play("board", st.id)); gAgent.region = true;
    LLApp::quitting = true; assert(!c.play("board", st.id)); LLApp::quitting = false;
    LLStartUp::state = 0; assert(!c.play("board", st.id)); LLStartUp::state = STATE_STARTED;
    gAgent.session = LLUUID(); assert(!c.play("board", st.id)); gAgent.session = LLUUID("session");
    gInventory.usable = false; assert(!c.play("board", st.id) && c.mStatus == "inventory_not_ready"); gInventory.usable = true;
    for (const auto& id : {link, sound}) {
        gInventory.trashed.insert(id);
        assert(!c.play("board", st.id)); gInventory.trashed.clear();
    }
    gInventory.items[sound].asset = LLUUID(); assert(!c.play("board", st.id));
    gInventory.items[sound].asset = LLUUID("sound-asset");
    gInventory.links[link] = LLUUID("missing"); assert(!c.play("board", st.id));
    gInventory.links[link] = sound;
    gInventory.items[sound].type = LLAssetType::AT_TEXTURE; assert(!c.play("board", st.id));
    gInventory.items[sound].type = LLAssetType::AT_LINK; assert(!c.play("board", st.id));
    gInventory.items[sound].type = LLAssetType::AT_SOUND;
    assert(sounds.size() == before);
    // Normal gesture activation/loading and click-to-stop stay intact.
    gSavedPerAccountSettings.gestures = true; RlvActions::gestures = true;
    assert(c.play("board", gt.id) && manager.activates == 1 && c.mPending.size() == 1);
    assert(c.play("board", gt.id) && manager.activates == 1);
    bool loaded = true; manager.loaded[gesture] = &loaded;
    now += 1.f; c.poll();
    assert(manager.plays == 1 && c.mPending.empty() && c.mStarted.count(gesture));
    assert(c.play("board", gt.id) && manager.stops == 1 && c.mStarted.empty());
    assert(c.play("board", gt.id) && manager.plays == 2);
    manager.playing.insert(LLUUID("unrelated")); c.stopBoard("board");
    assert(!manager.playing.count(gesture) && manager.playing.count(LLUUID("unrelated")));
    // A loading gesture must not become a sound implicitly when a link changes.
    manager.loaded.clear();
    assert(c.play("board", gt.id) && c.mPending.size() == 1);
    gInventory.items[gesture].type = LLAssetType::AT_SOUND;
    manager.loaded[gesture] = &loaded;
    now += 1.f; c.poll();
    assert(c.mPending.empty() && manager.plays == 2 && sounds.size() == before);
    gInventory.items[gesture].type = LLAssetType::AT_GESTURE;
    manager.loaded.clear(); assert(c.play("board", gt.id));
    gAgent.session = LLUUID("new-login"); now += 1.f; c.poll();
    assert(c.mPending.empty() && manager.plays == 2);
}
'''


class GestureBoardSounds(unittest.TestCase):
    def test_native_playback_boundaries(self):
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not shutil.which(compiler[0]):
            self.skipTest("C++17 compiler required for native-boundary harness")
        controller = (VIEWER / "fsgestureboardcontroller.cpp").read_text()
        model = (VIEWER / "fsgestureboardmodel.cpp").read_text()
        methods = [
            "bool FSGestureBoardController::available(", "bool FSGestureBoardController::permitted(",
            "bool FSGestureBoardController::resolve(", "std::string FSGestureBoardController::tileState(",
            "bool FSGestureBoardController::play(", "void FSGestureBoardController::stopBoard(",
            "void FSGestureBoardController::poll(", "void FSGestureBoardController::cancelPending(",
            "void FSGestureBoardController::setStatus(",
            "void FSGestureBoardController::cancelPendingForWindow(", "void FSGestureBoardController::cancelPendingForBoard(",
        ]
        helpers = ["Board* findBoard(", "const Board* findBoard(", "Tile* findTile(",
                   "const Tile* findTile(", "bool pendingMatches("]
        source = (BOUNDARY + "\nnamespace FSGestureBoard {\n" +
                  "".join(function(model, sig) for sig in helpers) + "}\n" +
                  "".join(function(controller, sig) for sig in methods) + CHECKS)
        with tempfile.TemporaryDirectory(prefix="gesture-board-sounds-") as directory:
            cpp, exe = Path(directory) / "test.cpp", Path(directory) / "test"
            cpp.write_text(source)
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                                      "-I", str(VIEWER), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_picker_and_editor_resources(self):
        source = (VIEWER / "fsfloatergestureboard.cpp").read_text()
        picker = function(source, "bool FSFloaterGesturePicker::postBuild(")
        self.assertIn("LLInventoryType::IT_SOUND", picker)
        self.assertIn("LLInventoryType::IT_GESTURE", picker)
        drag = function(source, "bool FSFloaterGestureBoard::handleDragAndDrop(")
        self.assertIn("DAD_SOUND", drag)
        self.assertIn("SOURCE_AGENT", drag)
        self.assertIn("SOURCE_LIBRARY", drag)
        roots = [ET.parse(VIEWER / "skins/default/xui/en" / name).getroot() for name in (
            "floater_gesture_board.xml", "floater_gesture_board_picker.xml", "floater_gesture_board_editor.xml")]
        names = {node.get("name") for root in roots for node in root.iter()}
        for name in re.findall(r'getChild<[^>]+>\("([^"]+)"\)', source):
            self.assertIn(name, names)
        strings = {node.get("name") for root in roots for node in root.findall("string")}
        self.assertIn("sound_played", strings)
        self.assertIn("tile_sound_ready", strings)
        # The shared collection format continues to store item references only.
        model = (VIEWER / "fsgestureboardmodel.cpp").read_text()
        encode = function(model, "LLSD encode(")
        self.assertIn('row["item"]', encode)
        self.assertNotIn('row["asset"]', encode)
        self.assertNotIn('row["sound"]', encode)


if __name__ == "__main__":
    unittest.main()
