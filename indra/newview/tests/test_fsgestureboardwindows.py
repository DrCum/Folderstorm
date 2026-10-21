"""Focused multi-board source/controller regressions; no viewer or server build."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from test_fsgestureboardsounds import BOUNDARY, function, VIEWER


class GestureBoardWindows(unittest.TestCase):
    def run_cpp(self, source):
        with tempfile.TemporaryDirectory(prefix="gesture-board-windows-") as temporary:
            cpp = Path(temporary) / "test.cpp"
            cpp.write_text(source)
            exe = cpp.with_suffix("")
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I"+str(VIEWER), str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_two_board_playback_and_cancellation(self):
        controller = (VIEWER / "fsgestureboardcontroller.cpp").read_text()
        model = (VIEWER / "fsgestureboardmodel.cpp").read_text()
        methods = ["bool FSGestureBoardController::available(", "bool FSGestureBoardController::permitted(",
                   "bool FSGestureBoardController::resolve(", "bool FSGestureBoardController::play(",
                   "void FSGestureBoardController::stopBoard(", "void FSGestureBoardController::poll(",
                   "void FSGestureBoardController::cancelPending(", "void FSGestureBoardController::setStatus(",
                   "void FSGestureBoardController::cancelPendingForWindow(", "void FSGestureBoardController::cancelPendingForBoard("]
        helpers = ["Board* findBoard(", "const Board* findBoard(", "Tile* findTile(",
                   "const Tile* findTile(", "bool pendingMatches(", "bool sameBoard("]
        source = BOUNDARY + "\nnamespace FSGestureBoard {\n"
        source += "".join(function(model, signature) for signature in helpers) + "}\n"
        source += "".join(function(controller, signature) for signature in methods)
        source += r'''
int main(){
    using namespace FSGestureBoard;
    const LLUUID a("a"),b("b"),a2("a2"),wa("window-a"),wb("window-b"),wc("window-c");
    for(const auto& id:{a,b,a2})gInventory.items[id]=Item{LLAssetType::AT_GESTURE,LLUUID(id.asString()+"-asset"),id.asString()};
    Tile ta;ta.id="ta";ta.item="a";Tile tb;tb.id="tb";tb.item="b";Tile tc;tc.id="tc";tc.item="a2";
    Board ba;ba.id="board-a";ba.name="A";ba.tiles={ta,tc};Board bb;bb.id="board-b";bb.name="B";bb.tiles={tb};
    FSGestureBoardController c;c.mData.selected=ba.id;c.mData.boards={ba,bb};
    auto& manager=LLGestureMgr::instance();
    assert(c.play(bb.id,tb.id,wb)); // Playback need not belong to the global last selection.
    assert(c.play(ba.id,ta.id,wa)&&c.play(ba.id,tc.id,wc)&&c.mPending.size()==3);
    const auto generation=c.mGeneration;c.cancelPendingForWindow(wa);
    assert(!c.mPending.count(a)&&c.mPending.count(b)&&c.mPending.count(a2)&&c.mGeneration==generation);
    bool loaded=true;manager.loaded[b]=&loaded;now+=1.f;c.poll();
    assert(manager.playing.count(b)&&c.mStarted.at(b)==bb.id&&c.mPending.count(a2));
    c.stopBoard(ba.id);assert(c.mPending.empty()&&manager.playing.count(b));
    // One Stop affects this board; unrelated native playback is retained.
    manager.loaded[a]=&loaded;assert(c.play(ba.id,ta.id,wa));
    manager.playing.insert(LLUUID("external"));c.stopBoard(ba.id);
    assert(!manager.playing.count(a)&&manager.playing.count(b)&&manager.playing.count(LLUUID("external")));
    c.stopBoard(bb.id);assert(!manager.playing.count(b)&&manager.playing.count(LLUUID("external")));
    // Board edit/delete cancellation does not invalidate another board's request.
    manager.loaded.clear();assert(c.play(ba.id,ta.id,wa)&&c.play(bb.id,tb.id,wb));
    c.cancelPendingForBoard(ba.id);assert(!c.mPending.count(a)&&c.mPending.count(b));
    c.mData.selected=ba.id;manager.loaded[b]=&loaded;now+=1.f;c.poll();assert(manager.playing.count(b));
    // Existing login/restriction guards still cancel delayed work across windows.
    manager.loaded.clear();assert(c.play(ba.id,ta.id,wa));
    RlvActions::gestures=false;now+=1.f;c.poll();assert(c.mPending.empty()&&!manager.playing.count(a));
    RlvActions::gestures=true;assert(c.play(ba.id,ta.id,wa));gAgent.session=LLUUID("other-login");
    now+=1.f;c.poll();assert(c.mPending.empty()&&c.mStarted.empty());gAgent.session=LLUUID("session");
    // Every persisted board field participates in edit invalidation.
    assert(sameBoard(ba,ba));
    for(int changed=0;changed<10;++changed){auto copy=ba;
        switch(changed){case 0:copy.name="renamed";break;case 1:copy.compact=false;break;
        case 2:copy.tiles[0].label="label";break;case 3:copy.tiles[0].item="replacement";break;
        case 4:copy.tiles[0].custom_color=true;break;case 5:copy.tiles[0].color[0]=1.f;break;
        case 6:copy.tiles[0].width=100;break;case 7:copy.tiles[0].height=28;break;
        case 8:std::swap(copy.tiles[0],copy.tiles[1]);break;case 9:copy.tiles.pop_back();break;}
        assert(!sameBoard(ba,copy));}
}
'''
        self.run_cpp(source)

    def test_window_selection_editor_ownership_and_stale_keys(self):
        native = (VIEWER / "fsfloatergestureboard.cpp").read_text()
        controller = (VIEWER / "fsgestureboardcontroller.cpp").read_text()
        model = (VIEWER / "fsgestureboardmodel.cpp").read_text()
        source = r'''
#include "fsgestureboardmodel.h"
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>
struct LLUUID{std::string value;LLUUID()=default;explicit LLUUID(std::string v):value(std::move(v)){}
    bool operator==(const LLUUID& o)const{return value==o.value;}bool operator!=(const LLUUID& o)const{return !(*this==o);}
    bool notNull()const{return !value.empty();}void setNull(){value.clear();}};
struct LLSD{
    bool map=false,encoded=false;std::string value;std::map<std::string,LLSD> fields;FSGestureBoard::Collection decoded;
    LLSD()=default;LLSD(std::string v):value(std::move(v)){}LLSD(const char* v):value(v){}LLSD(const LLUUID& v):value(v.value){}
    LLSD& operator[](const char* key){map=true;return fields[key];}
    const LLSD& operator[](const char* key)const{auto it=fields.find(key);static LLSD empty;return it==fields.end()?empty:it->second;}
    bool isMap()const{return map;}std::string asString()const{return value;}LLUUID asUUID()const{return LLUUID(value);}
    bool operator==(const LLSD& other)const{return map==other.map&&value==other.value&&fields==other.fields;}
    bool operator!=(const LLSD& other)const{return !(*this==other);}
};
namespace FSGestureBoard {
// Codec is a deterministic test boundary; production methods below still make
// the actual snapshot, account/session and invalidation decisions.
bool reject=false;
LLSD encode(const Collection& c){LLSD raw;raw["selected"]=c.selected;
    std::string names;for(const auto& b:c.boards){names+=b.id+":"+b.name+std::to_string(b.compact)+";";
        for(const auto& t:b.tiles){names+=t.id+t.item+t.label+std::to_string(t.custom_color)+std::to_string(t.width)+std::to_string(t.height);
            for(float color:t.color)names+=std::to_string(color);}}
    raw["boards"]=names;raw.decoded=c;raw.encoded=true;return raw;}
bool decode(const LLSD& raw,Collection& c){if(reject||!raw.encoded)return false;c=raw.decoded;return true;}
'''+function(model,"Board* findBoard(")+function(model,"const Board* findBoard(")+function(model,"bool sameBoard(")+r'''
}
struct Settings{int writes=0;void setLLSD(const char*,const LLSD&){++writes;}}gSavedPerAccountSettings;
struct FSGestureBoardController{
    FSGestureBoard::Collection mData;LLSD mRaw;int mRevision=0;std::vector<LLUUID> cancelled;
    LLUUID mAccount{"account"},mSession{"session"};std::string status;std::vector<std::string> cancelledBoards;
    bool available()const{return true;}void setStatus(std::string value){status=std::move(value);}
    bool commit(const FSGestureBoard::Collection&,const LLSD&,const LLUUID&,const LLUUID&);
    void cancelPendingForBoard(const std::string& id){cancelledBoards.push_back(id);}
    static FSGestureBoardController& instance(){static FSGestureBoardController c;return c;}
    void refresh(){}bool valid()const{return true;}const auto& data()const{return mData;}
    bool selectBoard(const std::string&);bool matchesSnapshot(const LLSD&)const;
    void cancelPendingForWindow(const LLUUID& id){cancelled.push_back(id);}
};
''' + function(controller,"bool FSGestureBoardController::selectBoard(") + function(controller,"bool FSGestureBoardController::matchesSnapshot(") + function(controller,"bool FSGestureBoardController::commit(") + r'''
using namespace FSGestureBoard;
struct FakeFloater{LLSD key;int closes=0;virtual ~FakeFloater()=default;const LLSD& getKey(){return key;}
    void closeFloater(bool){++closes;}};
struct Menu{void hide(){}};struct MenuHandle{Menu* get(){return nullptr;}};
struct FSFloaterGestureBoard:FakeFloater{
    LLUUID mWindowID;std::string mBoard;bool live=true;int refreshes=0;MenuHandle mMenu;
    const LLUUID& windowID()const{return mWindowID;}const std::string& selectedBoard()const{return mBoard;}
    bool current()const{return live;}void refresh(bool){++refreshes;}
    void closeEditors();void selectBoard(const std::string&);
};
FakeFloater picker,editor;std::vector<FSFloaterGestureBoard*> windows;
struct LLFloaterReg{
    template<class T>static T* findTypedInstance(const char*,const LLSD& key){
        for(auto* w:windows){if(w->key==key)return static_cast<T*>(w);}
        return nullptr;}
    static FakeFloater* findInstance(const char* name){return std::string(name)=="gesture_board_picker"?&picker:&editor;}
};
''' + function(native,"FSFloaterGestureBoard* ownerWindow(") + function(native,"void FSFloaterGestureBoard::closeEditors(") + function(native,"void FSFloaterGestureBoard::selectBoard(") + r'''
struct Swatch{void closeFloaterColorPicker(){}};using LLColorSwatchCtrl=Swatch;
struct FSFloaterGestureTileEditor{
    LLSD mKey;LLUUID mToken;
    template<class T>T* getChild(const char*){static T widget;return &widget;}
    void onClose(bool);
};
''' + function(native,"void FSFloaterGestureTileEditor::onClose(") + r'''
int main(){
    auto& c=FSGestureBoardController::instance();Board ba;ba.id="a";ba.name="A";Board bb;bb.id="b";bb.name="B";
    c.mData.boards={ba,bb};c.mData.selected=ba.id;c.mRaw=encode(c.mData);const auto original=c.mRaw;
    FSFloaterGestureBoard a,b;a.key=LLSD();b.key=LLSD("secondary");a.mWindowID=LLUUID("wa");b.mWindowID=LLUUID("wb");
    a.mBoard=ba.id;b.mBoard=bb.id;windows={&a,&b};
    picker.key["owner"]=b.mWindowID;editor.key["owner"]=b.mWindowID;
    a.selectBoard(bb.id);assert(a.mBoard==bb.id&&b.mBoard==bb.id&&picker.closes==0&&editor.closes==0);
    assert(c.mData.selected==bb.id&&c.matchesSnapshot(original)); // Selection alone does not invalidate a draft.
    auto changed=original;changed["boards"]="edited";assert(!c.matchesSnapshot(changed));
    a.selectBoard(ba.id);assert(b.mBoard==bb.id);
    LLSD key;key["window"]=b.key;key["owner"]=b.mWindowID;key["board"]=bb.id;
    assert(ownerWindow(key)==&b);b.live=false;assert(!ownerWindow(key));b.live=true;
    b.mWindowID=LLUUID("replacement-window");assert(!ownerWindow(key));b.mWindowID=LLUUID("wb");
    b.selectBoard(ba.id);assert(!ownerWindow(key)&&picker.closes==1&&editor.closes==1);
    assert(a.mBoard==ba.id);a.selectBoard("missing");assert(a.mBoard==ba.id);
    // Closing an already-closed editor cannot close an unrelated Add picker
    // merely because both editor-token fields are null.
    FSFloaterGestureTileEditor e;e.mKey["owner"]=a.mWindowID;
    picker.key=LLSD();picker.key["owner"]=b.mWindowID;const auto before=picker.closes;
    e.onClose(false);assert(picker.closes==before);
    e.mToken=LLUUID("editor");picker.key["editor_token"]=e.mToken;e.onClose(false);assert(picker.closes==before+1);
    // Execute actual commit(): cancel only edited/deleted boards. Stale edits,
    // wrong logins and invalid data must neither write nor cancel anything.
    auto draft=c.mData;draft.boards.front().name="renamed";
    assert(c.commit(draft,c.mRaw,c.mAccount,c.mSession));assert(c.cancelledBoards==std::vector<std::string>{"a"});
    const auto writes=gSavedPerAccountSettings.writes;c.cancelledBoards.clear();
    assert(!c.commit(draft,original,c.mAccount,c.mSession));
    assert(!c.commit(draft,c.mRaw,LLUUID("other"),c.mSession));
    assert(!c.commit(draft,c.mRaw,c.mAccount,LLUUID("other-session")));
    reject=true;assert(!c.commit(draft,c.mRaw,c.mAccount,c.mSession));reject=false;
    assert(gSavedPerAccountSettings.writes==writes&&c.cancelledBoards.empty());
    auto added=c.mData;Board third;third.id="third";third.name="Third";added.boards.push_back(third);
    assert(c.commit(added,c.mRaw,c.mAccount,c.mSession)&&c.cancelledBoards.empty());
    auto removed=c.mData;removed.boards.erase(removed.boards.begin()+1);
    assert(c.commit(removed,c.mRaw,c.mAccount,c.mSession));assert(c.cancelledBoards==std::vector<std::string>{"b"});
}
'''
        self.run_cpp(source)

    def test_ui_wiring_and_sound_badge(self):
        source = (VIEWER / "fsfloatergestureboard.cpp").read_text()
        root = ET.parse(VIEWER / "skins/default/xui/en/floater_gesture_board.xml").getroot()
        self.assertEqual(root.get("single_instance"), "false")
        self.assertEqual(root.get("reuse_instance"), "false")
        strings = {entry.get("name") for entry in root.findall("string")}
        self.assertTrue({"board_window", "board_title", "sound_item", "windows_full"} <= strings)
        action = function(source, "void FSFloaterGestureBoard::boardAction(")
        self.assertIn('key["window"] = LLUUID::generateNewID()', action)
        self.assertIn('LLFloaterReg::showInstance("gesture_board", key)', action)
        rebuild = function(source, "void FSFloaterGestureBoard::rebuild(")
        self.assertIn("findBoard(data, mBoard)", rebuild)
        self.assertNotIn("data.selected", rebuild)
        badge = function(source, "void setSound(")
        self.assertIn('"Inv_Sound"', badge)
        self.assertIn("LLFontGL::LEFT", badge)
        self.assertIn("mCustomColor ? mOutline", badge)
        self.assertIn("setSound(type == LLAssetType::AT_SOUND)", source)
        self.assertIn("button->setSound(type == LLAssetType::AT_SOUND)", source)
        textures = ET.parse(VIEWER / "skins/default/textures/textures.xml").getroot()
        self.assertTrue(any(t.get("name") == "Inv_Sound" for t in textures))
        refresh = function(source, "void FSFloaterGestureBoard::refresh(")
        self.assertIn("next != mBoard", refresh) # Empty collections must not repeatedly close New-board editors.
        self.assertIn("!controller.matchesSnapshot(mDisplayed)", refresh)


if __name__ == "__main__":
    unittest.main()
