#include "fsworkspacecontext.h"
#include "llsd.h"
#include <algorithm>
#include <iostream>
#include <list>

using uuid_vec_t = std::vector<LLUUID>;

struct LLScrollListItem
{
    LLSD row;
    bool selected = false;
    bool getEnabled() const { return true; }
    LLSD getValue() const { return row["value"]; }
    LLSD getAltValue() const { return row["alt_value"]; }
    LLUUID getUUID() const { return getValue().asUUID(); }
};

struct LLScrollListCtrl
{
    using item_list = std::vector<LLScrollListItem*>;
    std::list<LLScrollListItem> rows;
    item_list mItemList;
    bool mAllowMultipleSelection = true, mCommitOnSelectionChange = false;
    void deleteAllItems() { mItemList.clear(); rows.clear(); }
    void addElement(const LLSD& row) { rows.push_back({row}); mItemList.push_back(&rows.back()); }
    void deselectAllItems(bool) { for (auto* item : mItemList) item->selected = false; }
    void deselectItem(LLScrollListItem* item) { item->selected = false; }
    void selectItem(LLScrollListItem* item, int, bool single = true)
    {
        if (single) deselectAllItems(true);
        item->selected = true;
    }
    item_list getAllSelected() const
    {
        item_list selected;
        for (auto* item : mItemList) if (item->selected) selected.push_back(item);
        return selected;
    }
    void commitIfChanged() {}
    bool setSelectedByValue(const LLSD& value, bool selected);
    S32 selectMultiple(uuid_vec_t ids);
};

struct LLComboBox
{
    LLSD value;
    void setValue(const LLSD& next) { value = next; }
    LLSD getValue() const { return value; }
    bool setSelectedByValue(const LLSD&, bool) { return true; }
    void add(const std::string&, const std::string&) {}
    void setEnabled(bool) {}
};
struct LLCheckBoxCtrl
{
    bool checked = false;
    void set(bool value) { checked = value; }
    bool get() const { return checked; }
};
struct LLView
{
    template<class T> T* getChild(const std::string& name)
    {
        static std::map<std::string, T> controls;
        return &controls[name];
    }
};

namespace FSWorkspaceContext
{
    std::vector<HUD> attachedHUDs()
    {
        return {{"first", "HUD", 31}, {"saved", "Renamed HUD", 32}, {"third", "HUD", 31}};
    }
}

#include "workspace_hud_adapters.inc"

int main()
{
    LLView view;
    FSWorkspaceContext::Options saved;
    saved.huds = {{"saved", "HUD", 35}, {"unworn", "HUD", 31}};
    FSWorkspaceContextUI::setOptions(&view, saved);
    auto restored = FSWorkspaceContextUI::options(&view);
    for (int pass = 0; pass != 2; ++pass)
    {
        if (restored.huds.size() != saved.huds.size())
        {
            std::cerr << "Expected saved and unworn, got";
            for (const auto& hud : restored.huds) std::cerr << ' ' << hud.item;
            std::cerr << '\n';
            return 1;
        }
        for (std::size_t i = 0; i != saved.huds.size(); ++i)
        {
            if (restored.huds[i].item != saved.huds[i].item ||
                restored.huds[i].name != saved.huds[i].name ||
                restored.huds[i].point != saved.huds[i].point) return 2;
        }
        FSWorkspaceContextUI::setOptions(&view, restored); // Refresh HUDs uses this round trip.
        restored = FSWorkspaceContextUI::options(&view);
    }
    FSWorkspaceContextUI::setOptions(&view, {});
    return FSWorkspaceContextUI::options(&view).huds.empty() ? 0 : 3;
}
