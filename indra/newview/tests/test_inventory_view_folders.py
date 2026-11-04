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
struct Inventory {
    std::map<int,Node> nodes;
    const Node* getObject(LLUUID i){auto it=nodes.find(i);return it==nodes.end()?nullptr:&it->second;}
    const Node* getCategory(LLUUID i){return getObject(i);}
    LLUUID findCategoryUUIDForType(int){return 2;}
    LLUUID getRootFolderID(){return 1;}
    bool isObjectDescendentOf(LLUUID i,LLUUID p){
        while(const auto* n=getObject(i)){if(n->parent==p)return true;i=n->parent;}
        return false;
    }
} gInventory;
struct LLInventoryFilter {
    std::set<LLUUID> mViewFolders,mLiveViewFolders,mViewAncestors;
    enum {FILTER_RESTART}; void setModified(int){}
    void refreshViewFolders(); bool passesViewFolders(const LLUUID&) const;
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
    gInventory.nodes.at(4).parent=9; f.refreshViewFolders();
    assert(!f.passesViewFolders(3)&&f.passesViewFolders(9)&&f.passesViewFolders(5));
    gInventory.nodes.at(4).parent=2; gInventory.nodes.erase(7); f.refreshViewFolders();
    assert(!f.passesViewFolders(5)&&!f.passesViewFolders(8)&&!f.passesViewFolders(9));
    f.mViewFolders.clear(); f.refreshViewFolders(); assert(f.passesViewFolders(9));
}
"""
        methods = ("void LLInventoryFilter::refreshViewFolders()",
                   "bool LLInventoryFilter::passesViewFolders(const LLUUID& id) const")
        native.NativeObjectToolsTests.compile_and_run(self, fixture + "".join(
            native.function(source, signature) for signature in methods) + cases)


if __name__ == "__main__":
    unittest.main()
