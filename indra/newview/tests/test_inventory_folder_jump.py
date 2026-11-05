"""Exercise the viewer folder picker's cached walk, matching, and commit-time validation."""
import unittest
import test_fsobjecttools_native as native


class FolderJump(unittest.TestCase):
    def test_search_and_revalidate(self):
        source = (native.ROOT / "fsfloaterfolderjump.cpp").read_text()
        fixture = r"""
#include <algorithm>
#include <cassert>
#include <map>
#include <set>
#include <string>
#include <vector>
using LLUUID = int;
namespace LLFolderType { enum { FT_TRASH = 14 }; }
struct Cat {
    int id,parent,type; std::string name;
    int getUUID() const {return id;}
    int getParentUUID() const {return parent;}
    int getPreferredType() const {return type;}
    const std::string& getName() const {return name;}
};
struct LLInventoryModel {
    using cat_array_t=std::vector<Cat*>;
    using item_array_t=std::vector<int>;
    std::map<int,Cat> nodes;
    std::map<int,cat_array_t> children;
    void add(int id,int parent,std::string name,int type=0) {
        nodes.emplace(id,Cat{id,parent,type,name});
        children[parent].push_back(&nodes.at(id));
    }
    Cat* getCategory(int id) {auto i=nodes.find(id);return i==nodes.end()?nullptr:&i->second;}
    int getRootFolderID() const {return 1;}
    void getDirectDescendentsOf(int id,cat_array_t*& cats,item_array_t*&) {cats=&children[id];}
} gInventory;
struct LLInventoryFilter {
    std::set<int> hidden;
    bool checkFolder(int id,bool fetch) const {assert(!fetch);return !hidden.count(id);}
};
struct LLTimer {int ticks=0; double getElapsedTimeF64(){return 0.0008 * ++ticks;}};
std::string utf8str_tolower(std::string s){for(auto& c:s)c=static_cast<char>(::tolower(c));return s;}
struct FSFloaterFolderJump {
    struct Folder {int id;std::string name,parent_path,folded_name,folded_path;bool top_level;};
    std::vector<Folder> mFolders; std::set<int> mSeen; size_t mNextFolder=0,mNextChild=0;
    void extendIndex(const LLInventoryFilter&);
    bool isReachable(const LLUUID&,const LLInventoryFilter&) const;
};
"""
        cases = r"""
int main() {
    LLInventoryFilter filter;
    gInventory.add(1,0,"Root");
    gInventory.add(2,1,"Wardrobe");gInventory.add(3,2,"Boots");gInventory.add(4,1,"Props");
    gInventory.add(5,4,"Boots");gInventory.add(6,1,"Trash",LLFolderType::FT_TRASH);gInventory.add(7,6,"Hidden");
    gInventory.add(8,1,"Filtered");gInventory.add(9,8,"Filtered child");filter.hidden.insert(8);
    for (int i=100;i<300;++i) gInventory.add(i,1,"Loose folder");
    FSFloaterFolderJump picker;
    picker.mSeen.insert(1);picker.mFolders.push_back({1,{},{},{},{},false});
    picker.extendIndex(filter);
    assert(picker.mNextChild>0 && picker.mNextChild<200); // a wide root yields mid-folder
    while(picker.mNextFolder<picker.mFolders.size()) picker.extendIndex(filter);
    assert(picker.mFolders.size()==205); // root, two named paths, 200 loose folders
    auto find=[&](int id){return std::find_if(picker.mFolders.begin(),picker.mFolders.end(),
        [id](const auto& f){return f.id==id;});};
    assert(find(3)->parent_path=="Wardrobe" && find(5)->parent_path=="Props");
    assert(find(9)==picker.mFolders.end() && find(7)==picker.mFolders.end());
    assert(matchRank("boots","wardrobe","ward boots",{"ward","boots"})==3);
    assert(matchRank("boots","props","ward boots",{"ward","boots"})<0);
    assert(matchRank("boots","wardrobe","boots",{"boots"})<matchRank("boots tall","","boots",{"boots"}));
    assert(picker.isReachable(3,filter));
    gInventory.nodes.at(2).parent=6;assert(!picker.isReachable(3,filter));
    gInventory.nodes.at(4).parent=5;assert(!picker.isReachable(5,filter)); // stale/corrupt ancestry
    assert(!picker.isReachable(72,filter));
}
"""
        starts = ("int matchRank(", "void FSFloaterFolderJump::extendIndex(",
                  "bool FSFloaterFolderJump::isReachable(")
        definitions = []
        for start in starts:
            i = source.index(start)
            signature = source[i:source.index("{", i)].strip()
            definitions.append(native.function(source, signature))
        native.NativeObjectToolsTests.compile_and_run(self, fixture + "".join(definitions) + cases)

    def test_bulk_choice_edits_only_the_same_frozen_move(self):
        bulk = (native.ROOT / "fsfloaterinventorybulk.cpp").read_text()
        fixture = r"""
#include <cassert>
#include <string>
using U64=unsigned long long;
struct LLUUID {
    int value;
    bool operator!=(LLUUID other)const{return value!=other.value;}
    std::string asString()const{return std::to_string(value);}
};
struct Agent{LLUUID session{1};LLUUID getSessionID()const{return session;}} gAgent;
bool gDisconnected=false;
struct Inventory {bool known=true;bool getCategory(LLUUID)const{return known;}} gInventory;
struct LLComboBox {
    std::string kind="move";const LLComboBox& getValue()const{return *this;}
    std::string asString()const{return kind;}
};
struct FSFloaterInventoryBulk {
    bool mBusy=false,visible=true;
    std::string mSelection="a",destination="old";
    U64 mGeneration=10;LLUUID mSession{1};LLComboBox combo;int invalidated=0;
    template<class T>T* getChild(const char*){return &combo;}
    bool isInVisibleChain()const{return visible;}
    void childSetValue(const char*,std::string target){destination=target;}
    void invalidatePreview(){++invalidated;++mGeneration;}
    void acceptDestination(const LLUUID&,U64,const LLUUID&);
};
"""
        cases = r"""
int main(){
    FSFloaterInventoryBulk view;
    auto choose=[&](){view.acceptDestination({42},10,{1});};
    ++view.mGeneration;choose();--view.mGeneration;
    gAgent.session={2};choose();gAgent.session={1};
    view.combo.kind="rename";choose();view.combo.kind="move";
    view.visible=false;choose();view.visible=true;
    view.mBusy=true;choose();view.mBusy=false;
    gInventory.known=false;choose();gInventory.known=true;
    assert(view.destination=="old" && !view.invalidated);
    choose();assert(view.destination=="42" && view.invalidated==1);
    choose();assert(view.invalidated==1);
}
"""
        native.NativeObjectToolsTests.compile_and_run(self, fixture + native.function(
            bulk, "void FSFloaterInventoryBulk::acceptDestination(const LLUUID& id, U64 generation, const LLUUID& session)") + cases)


if __name__ == "__main__":
    unittest.main()
