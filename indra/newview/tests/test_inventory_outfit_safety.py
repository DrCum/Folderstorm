"""Run production inventory confirmation code with native UI/inventory boundaries.

These fixtures cover frozen selection and delayed replies, not native outfit
loading, Windows UI rendering or live RLVa acceptance.
"""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

from test_fsgestureboardsounds import VIEWER, function


class InventoryOutfitSafety(unittest.TestCase):
    def cpp(self, source):
        with tempfile.TemporaryDirectory(prefix="inventory-outfit-safety-") as tmp:
            cpp = Path(tmp) / "test.cpp"
            cpp.write_text(source)
            exe = cpp.with_suffix("")
            subprocess.run(shlex.split(os.environ.get("CXX", "c++")) + [
                "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                str(cpp), "-o", str(exe)], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)

    def test_delayed_confirmation_and_native_dispatch(self):
        source = (VIEWER / "llinventoryfunctions.cpp").read_text()
        available = function(source, "bool inventory_folder_outfit_available(")
        confirm = function(source, "void confirm_inventory_folder_outfit(")
        self.cpp(BOUNDARIES + available + confirm + r'''
void reset() {
    gInventory=LLInventoryModel{};gAgent=Agent{};gAgentWearables=Wearables{};
    LLGridManager::getInstance()->grid="grid";gDisconnected=false;
    LLApp::exiting=false;LLStartUp::state=STATE_STARTED;avatar=true;
    RlvActions::enabled=true;RlvFolderLocks::instance().locked.clear();
    LLAppearanceMgr::instance()=LLAppearanceMgr{};
    LLNotificationsUtil::prompts.clear();LLNotificationsUtil::callback={};
    gInventory.categories.emplace(LLUUID("a"),Category{LLUUID("a")});
    gInventory.categories.emplace(LLUUID("b"),Category{LLUUID("b")});
}
void reply(int option) {
    LLSD response;response.option=option;
    auto callback=std::move(LLNotificationsUtil::callback);
    assert(callback);callback(LLSD{},response);
}
int main() {
    reset();uuid_vec_t selection{LLUUID("a")};
    confirm_inventory_folder_outfit(selection,true);
    assert(LLNotificationsUtil::prompts.size()==1);
    assert(LLNotificationsUtil::prompts[0]=="ConfirmInventoryAddOutfit");
    assert(LLNotificationsUtil::payload["escape_button"].asString()=="Cancel");
    assert(LLAppearanceMgr::instance().writes.empty()); // Never send on prompt.
    selection={LLUUID("b")};reply(0); // Selection changes cannot retarget reply.
    auto& writes=LLAppearanceMgr::instance().writes;
    assert(writes.size()==1 && writes[0].id==LLUUID("a") && writes[0].append && !writes[0].copy);

    for(int cancel:{1,-1}) {
        reset();confirm_inventory_folder_outfit({LLUUID("a")},false);reply(cancel);
        assert(LLAppearanceMgr::instance().writes.empty());
    }
    reset();gInventory.categories[LLUUID("b")].parent=LLUUID("library");
    confirm_inventory_folder_outfit({LLUUID("a"),LLUUID("b")},false);
    assert(LLNotificationsUtil::prompts.size()==1);
    assert(LLNotificationsUtil::prompts[0]=="ConfirmInventoryReplaceOutfit");
    reply(0);
    assert(writes.size()==2 && !writes[0].append && !writes[0].copy && writes[1].copy);

    // Each identity/lifecycle/restriction change happens AFTER opening the prompt.
    const std::vector<std::function<void()>> invalidate={
        []{gAgent.account=LLUUID("other");}, []{gAgent.login=LLUUID("other-login");},
        []{LLGridManager::getInstance()->grid="other-grid";}, []{gDisconnected=true;},
        []{LLApp::exiting=true;}, []{LLStartUp::state=0;}, []{avatar=false;},
        []{gInventory.categories.erase(LLUUID("b"));},
        []{gInventory.categories[LLUUID("b")].parent=LLUUID("trash");},
        []{gInventory.categories[LLUUID("b")].parent=LLUUID("outside");},
        []{gInventory.categories[LLUUID("b")].parent=LLUUID("marketplace");},
        []{gInventory.categories[LLUUID("b")].type=LLFolderType::FT_TRASH;},
        []{gAgentWearables.busy=true;},
        []{RlvFolderLocks::instance().locked.insert(LLUUID("b"));},
        []{gInventory.categories[LLUUID("b")].canAdd=false;},
        []{gInventory.categories[LLUUID("b")].count=126;}
    };
    for(const auto& change:invalidate) {
        reset();confirm_inventory_folder_outfit({LLUUID("a"),LLUUID("b")},true);
        change();reply(0);assert(writes.empty()); // No partial first-folder submission.
    }
    reset();gInventory.categories[LLUUID("a")].type=LLFolderType::FT_OUTFIT;
    confirm_inventory_folder_outfit({LLUUID("a")},false);
    gInventory.categories[LLUUID("a")].canReplace=false;reply(0);assert(writes.empty());
    reset();gInventory.categories[LLUUID("a")].count=0;
    confirm_inventory_folder_outfit({LLUUID("a")},false);assert(!LLNotificationsUtil::callback);
    reset();confirm_inventory_folder_outfit({},true);assert(!LLNotificationsUtil::callback);
    confirm_inventory_folder_outfit({LLUUID("a"),LLUUID("missing")},true);
    assert(!LLNotificationsUtil::callback && LLNotificationsUtil::prompts.empty());
}
''')

    def test_context_menu_and_keyboard_guards(self):
        panel = function((VIEWER / "llinventorypanel.cpp").read_text(), "void LLInventoryPanel::doToSelected(")
        gallery = function((VIEWER / "llinventorygallerymenu.cpp").read_text(), "void LLInventoryGalleryContextMenu::doToSelected(")
        for handler in (panel, gallery):
            self.assertIn('action == "wear" && !gSavedSettings.getBOOL("FSInventoryAllowContextMenuWear")', handler)
            self.assertIn('confirm_inventory_folder_outfit(', handler)
        # Exercise the actual preference guard with stale open menus, in both handlers.
        for handler in (panel, gallery):
            guard = next(line.strip() for line in handler.splitlines() if 'action == "wear" &&' in line)
            self.cpp(r'''
#include <cassert>
#include <string>
struct Settings {bool allow=false;bool getBOOL(const char*){return allow;}} gSavedSettings;
int submissions=0;
void invoke(const std::string& action) {
''' + guard + r'''
    ++submissions;
}
int main(){invoke("wear");assert(submissions==0);invoke("wear_add");assert(submissions==1);
gSavedSettings.allow=true;invoke("wear");assert(submissions==2);}
''')
        menu = function((VIEWER / "llinventorybridge.cpp").read_text(), "void hide_context_entries(")
        self.assertIn('name == "Wearable And Object Wear"', menu)
        self.assertIn('FSInventoryAllowContextMenuWear', menu)
        # The shared alert opts in via payload; unrelated prompts keep their keys.
        keyboard = function((VIEWER / "lltoastalertpanel.cpp").read_text(), "bool LLToastAlertPanel::handleKeyHere(")
        self.cpp(r'''
#include <cassert>
#include <string>
using KEY=int;using MASK=int;
constexpr int KEY_ESCAPE=27,KEY_RETURN=13,KEY_RIGHT=100,KEY_LEFT=101,KEY_TAB=9,MASK_NONE=0,MASK_SHIFT=1;
struct LLSD {std::string text;LLSD& operator[](const char*){return *this;}std::string asString(){return text;}};
struct Notification {LLSD payload;LLSD& getPayload(){return payload;}};
struct LLButton {bool visible=true,enabled=true;int clicks=0;bool getVisible(){return visible;}
bool getEnabled(){return enabled;}void onCommit(){++clicks;}};
struct LLToastPanel {static void focusNextItem(bool){}static void focusPrevItem(bool){}};
struct LLToastAlertPanel {
Notification notice;Notification* mNotification=&notice;LLButton cancel,accept;bool hasCancel=true;
template<class T>T* findChild(const std::string& name){return name=="Cancel"&&hasCancel?&cancel:nullptr;}
LLButton* getDefaultButton(){return &cancel;}
bool handleKeyHere(KEY,MASK);
};
''' + keyboard + r'''
int main(){LLToastAlertPanel panel;panel.handleKeyHere(KEY_ESCAPE,MASK_NONE);
assert(panel.cancel.clicks==0);panel.notice.payload.text="Cancel";
panel.handleKeyHere(KEY_ESCAPE,MASK_NONE);assert(panel.cancel.clicks==1 && panel.accept.clicks==0);
panel.cancel.enabled=false;panel.handleKeyHere(KEY_ESCAPE,MASK_NONE);assert(panel.cancel.clicks==1);
panel.cancel.enabled=true;panel.handleKeyHere(KEY_RETURN,MASK_NONE);assert(panel.cancel.clicks==2);
panel.hasCancel=false;panel.handleKeyHere(KEY_ESCAPE,MASK_NONE);assert(panel.accept.clicks==0);}
''')

    def test_settings_and_unsuppressible_cancel_default(self):
        settings = ET.parse(VIEWER / "app_settings/settings.xml").getroot().find("map")
        entries = list(settings)
        setting = entries[next(i for i, node in enumerate(entries) if node.text == "FSInventoryAllowContextMenuWear") + 1]
        values = list(setting)
        fields = {values[i].text: values[i+1].text for i in range(0, len(values), 2)}
        self.assertEqual((fields["Persist"], fields["Type"], fields["Value"]), ("1", "Boolean", "0"))
        ui = ET.parse(VIEWER / "skins/default/xui/en/panel_preferences_UI.xml").getroot()
        control = ui.find('.//panel[@name="UI-interface-windows-tab"]/check_box[@name="FSInventoryAllowContextMenuWear"]')
        self.assertEqual(control.get("control_name"), "FSInventoryAllowContextMenuWear")
        notifications = ET.parse(VIEWER / "skins/default/xui/en/notifications.xml").getroot()
        for name in ("ConfirmInventoryAddOutfit", "ConfirmInventoryReplaceOutfit"):
            notice = notifications.find(f'notification[@name="{name}"]')
            self.assertIsNone(notice.find(".//ignore"))
            self.assertIsNone(notice.find("usetemplate"))
            buttons = notice.findall("form/button")
            self.assertEqual(len(buttons), 2)
            self.assertEqual([b.get("name") for b in buttons if b.get("default") == "true"], ["Cancel"])
            self.assertEqual(buttons[1].get("index"), "1")


BOUNDARIES = r'''
#include <cassert>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
using U32=unsigned;
struct LLUUID {
    std::string value;explicit LLUUID(std::string v=""):value(std::move(v)){}
    bool notNull()const{return !value.empty();}
    bool operator==(const LLUUID& rhs)const{return value==rhs.value;}
    bool operator!=(const LLUUID& rhs)const{return !(*this==rhs);}
    bool operator<(const LLUUID& rhs)const{return value<rhs.value;}
};
using uuid_vec_t=std::vector<LLUUID>;
struct LLSD {
    std::string text;int option=-1;std::map<std::string,LLSD> fields;
    LLSD& operator[](const std::string& name){return fields[name];}
    LLSD& operator=(const std::string& value){text=value;return *this;}
    std::string asString()const{return text;}
};
std::string llformat(const char* format,U32 value){char text[32];std::snprintf(text,sizeof(text),format,value);return text;}
namespace LLStringUtil {using format_map_t=std::map<std::string,std::string>;}
struct LLTrans {static std::string getString(const char*,const std::map<std::string,std::string>& args){return args.at("[COUNT]");}};
namespace LLFolderType {enum EType{FT_NONE,FT_OUTFIT,FT_TRASH};bool lookupIsProtectedType(EType type){return type==FT_TRASH;}}
namespace LLAssetType {enum EType{AT_CLOTHING,AT_BODYPART,AT_OBJECT,AT_GESTURE};}
struct Category {
    LLUUID id;LLUUID parent{"root"};LLFolderType::EType type=LLFolderType::FT_NONE;
    unsigned count=1;bool canAdd=true,canReplace=true;
    auto getPreferredType()const{return type;}std::string getName()const{return id.value;}
};
struct LLFindWearablesEx {LLFindWearablesEx(bool,bool){}};
struct LLIsOneOfTypes {explicit LLIsOneOfTypes(const std::vector<LLAssetType::EType>&){}};
struct LLInventoryModel {
    using cat_array_t=std::vector<Category>;using item_array_t=std::vector<int>;
    enum {EXCLUDE_TRASH};std::map<LLUUID,Category> categories;
    Category* getCategory(const LLUUID& id){auto found=categories.find(id);return found==categories.end()?nullptr:&found->second;}
    LLUUID findCategoryUUIDForType(LLFolderType::EType){return LLUUID("trash");}
    LLUUID getRootFolderID(){return LLUUID("root");}LLUUID getLibraryRootFolderID(){return LLUUID("library");}
    LLUUID getMarketplaceListingsUUID(){return LLUUID("marketplace");}
    bool isObjectDescendentOf(const LLUUID& id,const LLUUID& parent){auto* c=getCategory(id);return id==parent||(c&&c->parent==parent);}
    template<class Predicate>void collectDescendentsIf(const LLUUID& id,cat_array_t&,item_array_t& items,int,Predicate&){items.resize(categories.at(id).count);}
} gInventory;
struct Wearables {bool busy=false;bool isCOFChangeInProgress(){return busy;}} gAgentWearables;
namespace RlvActions {bool enabled=true;bool isRlvEnabled(){return enabled;}}
constexpr int RLV_LOCK_ADD=1;
struct RlvFolderLocks {
    std::set<LLUUID> locked;static RlvFolderLocks& instance(){static RlvFolderLocks locks;return locks;}
    bool isLockedFolder(const LLUUID& id,int){return locked.count(id);}
};
struct LLAppearanceMgr {
    struct Write {LLUUID id;bool copy,append;};std::vector<Write> writes;
    static LLAppearanceMgr& instance(){static LLAppearanceMgr manager;return manager;}
    bool getCanAddToCOF(const LLUUID& id){return gInventory.categories.at(id).canAdd;}
    bool getCanReplaceCOF(const LLUUID& id){return gInventory.categories.at(id).canReplace;}
    void wearInventoryCategory(Category* cat,bool copy,bool append){writes.push_back({cat->id,copy,append});}
};
struct Agent {LLUUID account{"account"},login{"login"};LLUUID getID(){return account;}LLUUID getSessionID(){return login;}} gAgent;
struct LLGridManager {std::string grid="grid";static LLGridManager* getInstance(){static LLGridManager manager;return &manager;}std::string getGrid(){return grid;}};
bool gDisconnected=false,avatar=true;bool isAgentAvatarValid(){return avatar;}
namespace LLApp {bool exiting=false;bool isExiting(){return exiting;}}
constexpr int STATE_STARTED=1;
namespace LLStartUp {int state=STATE_STARTED;int getStartupState(){return state;}}
struct Settings {U32 getU32(const char*){return 125;}} gSavedSettings;
struct LLNotificationsUtil {
    inline static std::vector<std::string> prompts;
    inline static LLSD payload;
    inline static std::function<bool(const LLSD&,const LLSD&)> callback;
    static void add(const std::string& name,const LLSD& = LLSD{}){prompts.push_back(name);}
    static void add(const std::string& name,const LLSD&,const LLSD& data,decltype(callback) cb){prompts.push_back(name);payload=data;callback=std::move(cb);}
    static int getSelectedOption(const LLSD&,const LLSD& response){return response.option;}
};
'''


if __name__ == "__main__":
    unittest.main()
