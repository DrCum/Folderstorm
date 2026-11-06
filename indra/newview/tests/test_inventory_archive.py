"""Exercise the viewer's archive filter against physical inventory ancestry."""
import unittest
import test_fsobjecttools_native as native


class ArchiveTest(unittest.TestCase):
    def test_archive_visibility(self):
        source = (native.ROOT / 'llinventoryfilter.cpp').read_text()
        fixture = r"""
#include <cassert>
#include <set>
#include <map>
using LLUUID=int;using S32=int;
struct LLInventoryObject {int id,parent; int getUUID() const{return id;} int getParentUUID() const{return parent;}};
namespace LLFolderType {enum {FT_TRASH};}
struct Inventory {
 std::map<int,LLInventoryObject> objects;
 const LLInventoryObject* getObject(int i){auto it=objects.find(i);return it==objects.end()?nullptr:&it->second;}
 const LLInventoryObject* getCategory(int i){return getObject(i);}
 int findCategoryUUIDForType(int){return 2;}
 bool isObjectDescendentOf(int i,int p){while(auto* o=getObject(i)){if(o->parent==p)return true;i=o->parent;}return false;}
} gInventory;
struct LLInventoryFilter {std::set<int> mArchivedFolders;bool passesArchive(const LLUUID&) const;};
"""
        cases = r"""
int main(){
 gInventory.objects={{1,{1,0}},{2,{2,1}},{3,{3,1}},{4,{4,3}},{5,{5,4}},{6,{6,1}}};
 LLInventoryFilter f;assert(f.passesArchive(5));
 f.mArchivedFolders.insert(3);assert(!f.passesArchive(3)&&!f.passesArchive(5)&&f.passesArchive(6));
 gInventory.objects.at(3).parent=2;assert(f.passesArchive(5)); // recover from Trash
 gInventory.objects.at(3).parent=1;f.mArchivedFolders.clear();assert(f.passesArchive(5));
}
"""
        native.NativeObjectToolsTests.compile_and_run(self,fixture + native.function(source,
            'bool LLInventoryFilter::passesArchive(const LLUUID& id) const') + cases)


if __name__ == '__main__':
    unittest.main()
