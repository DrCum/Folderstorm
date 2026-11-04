"""Run the favorites bar projection and stale-member check on real production methods."""
import unittest
import test_fsobjecttools_native as native


class FavoriteFolders(unittest.TestCase):
    def test_group_and_ungroup_without_changing_inventory_order(self):
        code = (native.ROOT / "llfavoritesbar.cpp").read_text()
        fixture = r"""
#include <cassert>
#include <map>
#include <string>
#include <vector>
struct LLUUID {
    int n;
    static const LLUUID null;
    bool notNull() const{return n!=0;}
    std::string asString() const{return std::to_string(n);}
    bool operator==(LLUUID o)const{return n==o.n;}
    bool operator<(LLUUID o)const{return n<o.n;}
};
const LLUUID LLUUID::null{0};
struct Item {
    LLUUID id; int type=5;
    LLUUID getUUID() const{return id;}
    int getType() const{return type;}
};
struct Value {std::string name;std::string asString() const{return name;}};
struct LLSD {
    std::map<std::string,Value> values;
    Value operator[](const std::string& key) const{
        auto i=values.find(key);return i==values.end()?Value{}:i->second;
    }
};
const char* FOLDER_SETTING="";
struct Settings {LLSD data;LLSD getLLSD(const char*) const{return data;}} gSavedPerAccountSettings;
namespace LLAssetType {enum {AT_LANDMARK=5};}
namespace LLFolderType {enum {FT_FAVORITE};}
struct Agent {LLUUID session{10};LLUUID getSessionID() const{return session;}} gAgent;
struct Inventory {
    Item* current=nullptr; bool favorite=true;
    Item* getItem(LLUUID id){return current && current->id==id?current:nullptr;}
    int findCategoryUUIDForType(int){return 1;}
    bool isObjectDescendentOf(LLUUID,int){return favorite;}
} gInventory;
struct LLFavoritesBarCtrl {
    using Items=std::vector<Item*>;
    Items mItems,mBarEntries;
    std::map<LLUUID,std::string> mFolderButtons;
    std::map<std::string,Items> mFolderContents;
    int mFirstDropDownItem=0;
    LLUUID itemBeforeOverflow() const;
    void rebuildFolderEntries();
};
"""
        cases = r"""
int main(){
    Item a{{1}},b{{2}},c{{3}},d{{4}};
    LLFavoritesBarCtrl bar;
    const LLFavoritesBarCtrl::Items order{&a,&b,&c,&d};
    bar.mItems=order;
    auto& names=gSavedPerAccountSettings.data.values;
    names={{"2",{"Clubs"}},{"3",{"Venues"}},{"4",{"Clubs"}}};
    bar.rebuildFolderEntries();
    assert((bar.mBarEntries==LLFavoritesBarCtrl::Items{&a,&b,&c}));
    assert((bar.mFolderContents.at("Clubs")==LLFavoritesBarCtrl::Items{&b,&d}));
    bar.mFirstDropDownItem=2; assert(bar.itemBeforeOverflow()==b.id); // visible: loose A, folder Clubs; C overflows
    bar.mFirstDropDownItem=0; assert(!bar.itemBeforeOverflow().notNull());
    names.erase("2");bar.rebuildFolderEntries();
    assert(bar.mBarEntries==order && bar.mItems==order && bar.mFolderButtons.at(d.id)=="Clubs");
    names.clear();bar.rebuildFolderEntries();assert(bar.mBarEntries==order && bar.mFolderContents.empty());
    gInventory.current=&a;assert(isCurrentFavorite({10},a.id));assert(!isCurrentFavorite({11},a.id));
    gInventory.favorite=false;assert(!isCurrentFavorite({10},a.id));
}
"""
        methods = ("bool isCurrentFavorite(const LLUUID& session, const LLUUID& id)", "void LLFavoritesBarCtrl::rebuildFolderEntries()",
                   "LLUUID LLFavoritesBarCtrl::itemBeforeOverflow() const")
        native.NativeObjectToolsTests.compile_and_run(self, fixture + "".join(
            native.function(code, sig) for sig in methods) + cases)


if __name__ == "__main__":
    unittest.main()
