"""Production board model/selection regressions with portable UI and LLSD boundaries.

The LLSD fixture preserves typed maps/arrays and values, but does not test native
settings serialization, disk persistence, mouse delivery or rendering.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
from test_fsgestureboardsounds import VIEWER, function

UUID = r'''
#pragma once
#include <string>
using S32 = int;
struct LLUUID {
    std::string value;
    LLUUID() = default;
    explicit LLUUID(std::string v):value(std::move(v)){}
    bool isNull()const{return value.empty();}
    bool notNull()const{return !isNull();}
    std::string asString()const{return value;}
    bool operator==(const LLUUID& o)const{return value==o.value;}
    bool operator!=(const LLUUID& o)const{return !(*this==o);}
    bool operator<(const LLUUID& o)const{return value<o.value;}
    static LLUUID generateNewID(){static int next=0;return LLUUID("uuid-"+std::to_string(++next));}
    static const LLUUID null;
};
inline const LLUUID LLUUID::null;
'''
LLSD = r'''
#pragma once
#include "lluuid.h"
#include <map>
#include <vector>
struct LLSD {
    using Real=double;
    enum Type{Undefined,Map,Array,Integer,Float,Boolean,String,Uuid} type=Undefined;
    std::map<std::string,LLSD> map;
    std::vector<LLSD> array;
    std::string text;
    double number=0;
    LLSD()=default;
    LLSD(int v):type(Integer),number(v){}
    LLSD(double v):type(Float),number(v){}
    LLSD(bool v):type(Boolean),number(v){}
    LLSD(std::string v):type(String),text(std::move(v)){}
    LLSD(const char* v):LLSD(std::string(v)){}
    LLSD(const LLUUID& v):type(Uuid),text(v.value){}
    static LLSD emptyMap(){LLSD v;v.type=Map;return v;}
    static LLSD emptyArray(){LLSD v;v.type=Array;return v;}
    bool isMap()const{return type==Map;}bool isArray()const{return type==Array;}
    bool isInteger()const{return type==Integer;}bool isReal()const{return type==Float;}
    bool isBoolean()const{return type==Boolean;}bool isString()const{return type==String;}
    bool isUUID()const{return type==Uuid;}
    int asInteger()const{return static_cast<int>(number);}double asReal()const{return number;}
    bool asBoolean()const{return number!=0;}std::string asString()const{return text;}
    LLUUID asUUID()const{return LLUUID(text);}
    std::size_t size()const{return isMap()?map.size():isArray()?array.size():0;}
    bool has(const char* key)const{return map.count(key)!=0;}
    void erase(const char* key){map.erase(key);}
    LLSD& operator[](const char* key){type=Map;return map[key];}
    const LLSD& operator[](const char* key)const{static LLSD empty;auto it=map.find(key);return it==map.end()?empty:it->second;}
    LLSD& operator[](int index){type=Array;if(array.size()<=std::size_t(index))array.resize(std::size_t(index)+1);return array[std::size_t(index)];}
    const LLSD& operator[](int index)const{static LLSD empty;return std::size_t(index)<array.size()?array[std::size_t(index)]:empty;}
    void append(const LLSD& v){type=Array;array.push_back(v);}
    auto beginMap()const{return map.begin();}auto endMap()const{return map.end();}
    bool operator==(const LLSD& v)const{return type==v.type&&map==v.map&&array==v.array&&text==v.text&&number==v.number;}
    bool operator!=(const LLSD& v)const{return !(*this==v);}
};
'''


class GestureBoardCustomization(unittest.TestCase):
    def run_cpp(self, source=None):
        with tempfile.TemporaryDirectory(prefix="gesture-board-customization-") as temporary:
            directory = Path(temporary)
            (directory / "lluuid.h").write_text(UUID)
            (directory / "llsd.h").write_text(LLSD)
            (directory / "llsdutil.h").write_text('#include "llsd.h"\n')
            test = VIEWER / "tests/test_fsgestureboard.cpp"
            if source is not None:
                test = directory / "test.cpp"
                test.write_text(source)
            exe = directory / "test"
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                "-I" + str(directory), "-I" + str(VIEWER), str(test),
                str(VIEWER / "fsgestureboardmodel.cpp"), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=15)

    def test_model_migration_defaults_bulk_and_pagination(self):
        self.run_cpp()

    def test_selecting_never_plays_items(self):
        source = r'''
#include "fsgestureboardmodel.h"
#include "lluuid.h"
#include <cassert>
using namespace FSGestureBoard;
using MASK=unsigned;
constexpr MASK MASK_NONE=0, MASK_CONTROL=1, MASK_SHIFT=2;
struct FSGestureBoardController {
    Collection value;int plays=0;
    static auto& instance(){static FSGestureBoardController c;return c;}
    const Collection& data()const{return value;}
    void play(const std::string&,const std::string&,const LLUUID&){++plays;}
};
struct FSFloaterGestureBoard {
    bool live=true,mSelecting=false,mRebuildSelection=false;int closes=0;
    Selection mSelection;std::string mBoard="b";LLUUID mWindowID{"w"},mFocusReference;
    bool current()const{return live;}
    void closeEditors(){++closes;}
    void selectionMode(bool);void selectTile(const std::string&,MASK);void playTile(const std::string&);
};
'''
        native = (VIEWER / "fsfloatergestureboard.cpp").read_text()
        source += function(native, "Tile editorPresentation(")
        for signature in ("void FSFloaterGestureBoard::selectionMode(", "void FSFloaterGestureBoard::selectTile(", "void FSFloaterGestureBoard::playTile("):
            source += function(native, signature)
        source += r'''
int main(){
    auto& c=FSGestureBoardController::instance();Board b;b.id="b";
    for(const char* id:{"a","b","c","d"}){Tile t;t.id=id;t.item=std::string("item-")+id;b.tiles.push_back(t);}
    c.value.boards.push_back(b);FSFloaterGestureBoard w;
    w.playTile("a");assert(c.plays==1);
    w.selectTile("a",MASK_CONTROL);assert(w.mSelecting&&w.mSelection.ids==std::set<std::string>{"a"}&&w.closes==1&&c.plays==1);
    w.playTile("d");assert(w.mSelection.ids==std::set<std::string>({"a","d"})&&c.plays==1);
    w.selectTile("b",MASK_SHIFT);assert(w.mSelection.ids.size()==4&&c.plays==1);
    w.playTile("b");assert(!w.mSelection.ids.count("b")&&c.plays==1);
    w.live=false;w.playTile("a");w.selectTile("c",MASK_CONTROL);assert(c.plays==1&&!w.mSelection.ids.count("b"));
    w.live=true;w.selectionMode(false);assert(!w.mSelecting&&w.mSelection.ids.empty());
    w.playTile("c");assert(c.plays==2);
    c.value.defaults.width=112;c.value.defaults.height=36;
    b.defaults.width=160;b.defaults.height=50;Tile draft;
    auto shown=editorPresentation("global_defaults",c.value,&b,draft);
    assert(shown.width==100&&shown.height==28); // Global reset previews density, not the board override.
    shown=editorPresentation("defaults",c.value,&b,draft);assert(shown.width==112&&shown.height==36);
    shown=editorPresentation("tile",c.value,&b,draft);assert(shown.width==160&&shown.height==50);
}
'''
        self.run_cpp(source)


if __name__ == "__main__":
    unittest.main()
