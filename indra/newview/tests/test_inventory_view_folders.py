"""Exercise a custom inventory tab using two physical folders and their ancestor paths."""
import unittest
import test_fsobjecttools_native as native


class FolderViews(unittest.TestCase):
    def test_union_tracks_inventory_structure(self):
        source = (native.ROOT / "llinventoryfilter.cpp").read_text()
        fixture = r"""
#include <cassert>
#include <set>
#include <map>
struct LLUUID {
    int value;
    LLUUID(int n=0):value(n){}
    operator int() const {return value;}
    bool isNull() const {return value==0;}
};
struct Node {
    LLUUID id,parent;
    LLUUID getUUID() const {return id;}
    LLUUID getParentUUID() const {return parent;}
};
namespace LLFolderType {enum {FT_TRASH};}
using U32=unsigned int;
#define LL_PROFILE_ZONE_SCOPED
struct LLInventoryObserver {enum {STRUCTURE=1, ADD=2, REMOVE=4};};
struct Inventory {
    using changed_items_t=std::set<LLUUID>;
    changed_items_t changed;
    const changed_items_t& getChangedIDs() const{return changed;}
    std::map<int,Node> nodes;
    const Node* getObject(LLUUID i) const{auto it=nodes.find(i);return it==nodes.end()?nullptr:&it->second;}
    const Node* getCategory(LLUUID i) const{return getObject(i);}
    LLUUID findCategoryUUIDForType(int){return 2;}
    LLUUID getRootFolderID(){return 1;}
    bool isObjectDescendentOf(LLUUID i,LLUUID p){
        while(const auto* n=getObject(i)){if(n->parent==p)return true;i=n->parent;}
        return false;
    }
} gInventory;
struct LLInventoryFilter {
    std::set<LLUUID> mViewFolders,mLiveViewFolders,mViewAncestors;
    const auto& getViewFolders() const{return mViewFolders;}
    enum {FILTER_RESTART}; void setModified(int){}
    void refreshViewFolders(); bool passesViewFolders(const LLUUID&) const;
};
using LLInventoryModel=Inventory; using LLInventoryObject=Node;
struct LLInventoryPanel {
    enum {VIEWS_INITIALIZED=1}; int mViewsInitialized=0;
    LLInventoryFilter& filter;
    LLInventoryModel* getModel(){return &gInventory;}
    LLInventoryFilter& getFilter(){return filter;}
    void itemChanged(LLUUID,U32,const Node*){assert(false && "unopened tree touched");}
    void modelChanged(U32 mask);
};
"""
        cases = r"""
int main() {
    gInventory.nodes={{1,{1,0}},{2,{2,1}},{3,{3,1}},{4,{4,3}},{5,{5,4}},
                      {6,{6,3}},{7,{7,1}},{8,{8,7}},{9,{9,1}}};
    LLInventoryFilter f;
    assert(f.passesViewFolders(9));
    f.mViewFolders={4,7}; f.refreshViewFolders();
    assert(f.passesViewFolders(3)&&f.passesViewFolders(5)&&f.passesViewFolders(8));
    assert(!f.passesViewFolders(6)&&!f.passesViewFolders(9));
    LLInventoryPanel unopened{0,f};
    gInventory.nodes.at(4).parent=9; gInventory.changed={4};
    unopened.modelChanged(LLInventoryObserver::STRUCTURE);
    assert(!f.passesViewFolders(3)&&f.passesViewFolders(9)&&f.passesViewFolders(5));
    gInventory.nodes.at(4).parent=2; gInventory.nodes.erase(7); f.refreshViewFolders();
    assert(!f.passesViewFolders(5)&&!f.passesViewFolders(8)&&!f.passesViewFolders(9));
    f.mViewFolders.clear(); f.refreshViewFolders(); assert(f.passesViewFolders(9));
}
"""
        methods = ("void LLInventoryFilter::refreshViewFolders()",
                   "bool LLInventoryFilter::passesViewFolders(const LLUUID& id) const")
        native.NativeObjectToolsTests.compile_and_run(self, fixture + "".join(
            native.function(source, signature) for signature in methods) + native.function(
                (native.ROOT / "llinventorypanel.cpp").read_text(), "void LLInventoryPanel::modelChanged(U32 mask)") + cases)


if __name__ == "__main__":
    unittest.main()
