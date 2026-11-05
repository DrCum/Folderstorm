"""Exercise the native switcher listing with favorite, workspace, and layout entries."""
import unittest
import test_fsobjecttools_native as native


class WorkspaceSwitchList(unittest.TestCase):
    def test_filter_keeps_distinct_targets_and_a_selectable_first_row(self):
        source = (native.ROOT / "fsfloaterworkspaces.cpp").read_text()
        fixture = r"""
#include <cassert>
#include <cctype>
#include <set>
#include <string>
#include <vector>
namespace FSWorkspaceQuickAccess {
struct Entry {std::string key,label; bool layout;};
std::vector<Entry> entries(){return {{"workspace:a","Studio",false},{"workspace:b","Beach",false},
                                     {"layout:c","Studio",true},{"layout:d","Desk",true}};}
std::vector<Entry> orderedFavorites(const std::vector<Entry>& e){return {e[2],e[1]};}
}
std::string utf8str_tolower(std::string s){for(auto& c:s)c=static_cast<char>(std::tolower(c));return s;}
struct FSFloaterWorkspaces {
    std::set<std::string> mFavorites;
    std::string mQuery;
    std::vector<std::string> rows;
    void addHeading(std::string s){rows.push_back(s);}
    std::string getString(std::string s){return s;}
    void addEntry(std::string key,std::string,bool){rows.push_back(key);}
    std::string populateList();
};
"""
        cases = r"""
int main(){
    FSFloaterWorkspaces w; w.mQuery="stu";
    assert(w.populateList()=="layout:c");
    assert((w.rows==std::vector<std::string>{"favorites_heading","layout:c","workspaces_heading","workspace:a"}));
    assert(w.mFavorites.count("workspace:b"));
    w.rows.clear();w.mQuery="desk";
    assert(w.populateList()=="layout:d" && w.rows.front()=="layouts_heading");
    w.rows.clear();w.mQuery="missing";assert(w.populateList().empty() && w.rows.empty());
}
"""
        native.NativeObjectToolsTests.compile_and_run(self, fixture + native.function(
            source, "std::string FSFloaterWorkspaces::populateList()") + cases)


if __name__ == "__main__":
    unittest.main()
