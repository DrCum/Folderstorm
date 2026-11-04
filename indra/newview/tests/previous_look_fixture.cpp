/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct LLUUID { int value; operator int() const{return value;} static const LLUUID null; };
const LLUUID LLUUID::null{0};
namespace LLAssetType {enum EType { AT_BODYPART,AT_CLOTHING,AT_OBJECT,AT_GESTURE };}
namespace LLFolderType {enum {FT_TRASH};}
struct LLViewerInventoryItem {
    LLAssetType::EType type; std::string desc;
    LLViewerInventoryItem(LLAssetType::EType t) : type(t) {}
    LLViewerInventoryItem(const LLViewerInventoryItem* item) : type(item->type),desc(item->desc){}
    auto getActualType()const{return type;} void setDescription(std::string s){desc=s;}
};
template<class T> struct LLPointer : std::shared_ptr<T> {LLPointer(T* t):std::shared_ptr<T>(t){}};
struct LLInventoryModel {using item_array_t=std::vector<LLPointer<LLViewerInventoryItem>>;};
struct {std::map<int,LLViewerInventoryItem> items;
    LLViewerInventoryItem* getItem(LLUUID i){auto it=items.find(i);return it==items.end()?nullptr:&it->second;}
    LLUUID findCategoryUUIDForType(int){return {2};} LLUUID getRootFolderID(){return {1};}
    bool isObjectDescendentOf(LLUUID i,LLUUID parent){return parent==1 || i==8;}
} gInventory;
bool gDisconnected=false;
bool isAgentAvatarValid(){return true;}
struct {LLUUID login{1}; LLUUID getSessionID(){return login;} bool getRegion(){return true;}} gAgent;
struct {bool busy=false; bool isCOFChangeInProgress(){return busy;} void notifyLoadingStarted(){busy=true;}} gAgentWearables;
constexpr int RLV_LOCK_ANY=1;
struct RlvActions {static bool isRlvEnabled(){return true;}};
struct {bool locked=false; bool hasLockedAttachmentPoint(int){return locked;}} gRlvAttachmentLocks;
struct {bool hasLockedWearableType(int){return false;}} gRlvWearableLocks;
struct RlvFolderLocks {static RlvFolderLocks instance(){return {};}bool hasLockedFolder(int){return false;}};
struct LLSD {LLSD& with(const char*,const std::string&){return *this;}};
struct LLNotificationsUtil {static inline std::string last; static void add(std::string s,LLSD={}){last=s;}};
struct LLAppearanceMgr {
    struct Entry {LLUUID id;LLAssetType::EType type;std::string name,order;};
    std::vector<Entry> mPreviousLook;LLUUID mLookSession{1},mLookBase{4}; bool mOutfitLocked=false;
    bool hasPreviousLook()const;void restorePreviousLook();bool assistantAppearanceIntent(){return true;}
    int dispatches=0;
    using Items=LLInventoryModel::item_array_t;
    void updateCOF(const Items&,const Items& clothes,const Items&,const Items&,bool append,LLUUID base) {
        assert(!append && base==4 && clothes.size()==1 && clothes.front()->desc=="400"); ++dispatches;
    }
};
#include "previous_look.inc"
int main(){
 LLAppearanceMgr a; a.mPreviousLook={{{3},LLAssetType::AT_CLOTHING,"shirt","400"}};
 gAgent.login={2};a.restorePreviousLook();assert(!a.dispatches && !gAgentWearables.busy);
 gAgent.login={1};gRlvAttachmentLocks.locked=true;a.restorePreviousLook();assert(LLNotificationsUtil::last=="PreviousLookRestricted");
 gRlvAttachmentLocks.locked=false;a.restorePreviousLook();assert(LLNotificationsUtil::last=="PreviousLookMissing" && !a.mPreviousLook.empty());
 gInventory.items.emplace(3,LLAssetType::AT_CLOTHING);
 a.restorePreviousLook();assert(a.dispatches==1 && a.mPreviousLook.empty() && gAgentWearables.busy);
 assert(gInventory.items.at(3).desc.empty());
}
