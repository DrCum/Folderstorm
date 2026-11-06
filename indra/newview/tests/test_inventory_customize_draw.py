"""Check Inventory draw cost and Customize visibility using production methods.

The UI boundary counts root lookups and supplies weak control handles. This is
not a native viewer, Windows or GPU benchmark.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest


VIEWER = Path(__file__).resolve().parents[1]


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


class InventoryCustomizeDraw(unittest.TestCase):
    def test_drawing_never_searches_inventory_rows(self):
        source = (VIEWER / "fsinventorycustomtabs.cpp").read_text()
        production = "\n".join(method(source, signature) for signature in (
            "FSInventoryCustomTabs::FSInventoryCustomTabs(LLPanelMainInventory* parent)",
            "FSInventoryCustomTabs* FSInventoryCustomTabs::instanceFor(LLPanelMainInventory* parent)",
            "bool FSInventoryCustomTabs::isCustomTab(const LLPanel* panel) const",
            "void FSInventoryCustomTabs::onParentDraw(LLPanelMainInventory* parent)",
        ))
        fixture = r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <set>
#include <string_view>

template<class T> struct LLHandle {
    std::weak_ptr<T*> target;
    LLHandle() = default;
    explicit LLHandle(const std::shared_ptr<T*>& value) : target(value) {}
    T* get() const { auto value = target.lock(); return value ? *value : nullptr; }
};
struct LLView {
    bool visible = false;
    std::shared_ptr<LLView*> target = std::make_shared<LLView*>(this);
    virtual ~LLView() { *target = nullptr; }
    LLHandle<LLView> getHandle() const { return LLHandle<LLView>(target); }
    void setVisible(bool value) { visible = value; }
};
struct LLPanel : LLView {};
struct LLInventoryPanel : LLPanel {};
struct LLTabContainer {};
struct LLMenuGL : LLView {};
LLView* gMenuHolder = nullptr;
struct LLViewerMenuHolderGL {
    struct child_registry_t { static int instance() { return 0; } };
};
struct LLUICtrlFactory {
    static LLUICtrlFactory* getInstance() { static LLUICtrlFactory value; return &value; }
    template<class T> T* createFromFile(const char*, LLView*, int) { return nullptr; }
};
constexpr const char* MENU_FILE = "menu_inventory_custom_tab.xml";
class FSInventoryCustomTabs;
struct LLPanelMainInventory : LLPanel {
    LLTabContainer tabs;
    LLTabContainer* mFilterTabs = &tabs;
    std::unique_ptr<FSInventoryCustomTabs> mCustomTabs;
    std::unique_ptr<LLView> controls = std::make_unique<LLView>();
    LLInventoryPanel builtIn, custom;
    LLPanel* active = &builtIn;
    bool single = false;
    int lookups = 0;
    bool isSingleFolderMode() const { return single; }
    LLPanel* getActivePanel() const { return active; }
    template<class T> T* findChild(std::string_view name) {
        ++lookups;
        assert(name == "customize_tab_panel");
        return dynamic_cast<T*>(controls.get());
    }
};
class FSInventoryCustomTabs {
public:
    LLPanelMainInventory* mParent = nullptr;
    LLTabContainer* mTabs = nullptr;
    LLHandle<LLView> mCustomizeTabPanel, mMenuHandle;
    std::set<LLInventoryPanel*> mPanels;
    int ensured = 0;
    explicit FSInventoryCustomTabs(LLPanelMainInventory* parent);
    static FSInventoryCustomTabs* instanceFor(LLPanelMainInventory* parent);
    static void onParentDraw(LLPanelMainInventory* parent);
    bool isCustomTab(const LLPanel* panel) const;
    void ensureRootOpenForActivePanel() { ++ensured; }
    void installAddTab() {}
    void installShowHiddenTab() {}
};
std::set<FSInventoryCustomTabs*>& allInstances() {
    static std::set<FSInventoryCustomTabs*> instances;
    return instances;
}
'''
        cases = r'''
void install(LLPanelMainInventory& panel) {
    panel.mCustomTabs = std::make_unique<FSInventoryCustomTabs>(&panel);
    panel.mCustomTabs->mPanels.insert(&panel.custom);
}
void draw(LLPanelMainInventory& panel, bool expected) {
    const int before = panel.lookups;
    const int ensured = panel.mCustomTabs->ensured;
    for (int frame = 0; frame != 120; ++frame) {
        FSInventoryCustomTabs::onParentDraw(&panel);
        if (panel.controls) assert(panel.controls->visible == expected);
    }
    assert(panel.lookups == before && "Inventory draw searched the loaded UI tree");
    assert(panel.mCustomTabs->ensured == ensured + 120);
}
int main() {
    LLPanelMainInventory first, second;
    install(first); install(second);
    draw(first, false);
    first.active = &first.custom;
    draw(first, true);
    assert(!second.controls->visible);
    draw(second, false);
    first.single = true;
    draw(first, false);
    first.single = false;
    draw(first, true);
    first.active = &first.builtIn;
    draw(first, false);

    // A removed or unsupported skin control must not be dereferenced or cause
    // draw-time retries of the expensive root search.
    first.controls.reset();
    draw(first, false);
    LLPanelMainInventory missing;
    missing.controls.reset();
    install(missing);
    missing.active = &missing.custom;
    draw(missing, false);

    FSInventoryCustomTabs noParent(nullptr);
    FSInventoryCustomTabs::onParentDraw(nullptr);
    assert(!noParent.mCustomizeTabPanel.get());
    std::cout << "Customize visibility, independent windows, expired/missing controls, and zero draw lookups passed\n";
}
'''
        with tempfile.TemporaryDirectory(prefix="inventory-customize-draw-") as directory:
            path = Path(directory) / "test.cpp"
            path.write_text(fixture + production + cases)
            executable = path.with_suffix("")
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
                str(path), "-o", str(executable),
            ], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
