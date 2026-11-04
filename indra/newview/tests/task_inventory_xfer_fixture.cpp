/** SPDX-License-Identifier: LGPL-2.1-or-later */
// Compile the viewer's request, reply and completion handlers; substitute the
// simulator, transfer queue and file reader to control completion order.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct LLUUID {
    int value;
    LLUUID(int id=0) : value(id) {}
    operator int() const { return value; }
    static const LLUUID null;
};
const LLUUID LLUUID::null;
using S16 = short;
using S32 = int;
using U64 = unsigned long long;
enum class LLExtStat { NONE };
enum { INVENTORY_REQUEST_STOPPED, INVENTORY_REQUEST_PENDING, INVENTORY_XFER,
       INVENTORY_UPDATE_WAIT_TIME_DESYNC, INVENTORY_UPDATE_WAIT_TIME_OUTDATED, LL_PATH_CACHE,
       _PREHASH_InventoryData, _PREHASH_TaskID, _PREHASH_Serial, _PREHASH_Filename,
       _PREHASH_RequestTaskInventory, _PREHASH_AgentData, _PREHASH_AgentID, _PREHASH_SessionID, _PREHASH_LocalID };
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK
#define LL_DEBUGS() std::clog
#define LL_WARNS() std::clog
#define LL_INFOS() std::clog
#define LL_ENDL '\n'
#define CHECK(condition) do { if (!(condition)) { std::cerr << "Failed: " #condition << '\n'; std::exit(1); } } while (false)

template<typename T> struct LLPointer : std::shared_ptr<T> {
    LLPointer& operator=(T* value) { this->reset(value); return *this; }
};
namespace LLAssetType { enum { AT_CATEGORY }; }
struct LLInventoryObject {
    using object_list_t = std::list<LLPointer<LLInventoryObject>>;
    LLInventoryObject(LLUUID, LLUUID, int, const char*) {}
    virtual ~LLInventoryObject() = default;
};
struct LLViewerInventoryItem : LLInventoryObject { int getType() { return 0; } LLUUID getAssetUUID() { return {}; } };
struct LLFile { static void remove(const std::string&) {} };
struct Region { std::string getCapability(const char*) { return {}; } int getHost() { return 1; } };
struct { LLUUID getID() { return {}; } LLUUID getSessionID() { return {}; } } gAgent;
struct LLMessageSystem {
    int sent = 0; S16 serial = 1; std::string filename = "contents.tmp";
    void getUUIDFast(int,int,LLUUID& id) { id = 7; }
    void getS16Fast(int,int,S16& out) { out = serial; }
    void getStringFast(int,int,std::string& out) { out = filename; }
    void newMessageFast(int) {} void nextBlockFast(int) {} void addUUIDFast(int,LLUUID) {}
    void addU32Fast(int,int) {} void sendReliable(int) { ++sent; }
} messages, *gMessageSystem = &messages;
struct LLDir {
    static std::string getScrubbedFileName(const std::string& name) { return name; }
    std::string getExpandedFilename(int,const std::string& name) { return name; }
} directories, *gDirUtilp = &directories;
namespace boost { template<class... T> int bind(T...) { std::abort(); } }
struct LLCoros { static LLCoros& instance(){ static LLCoros c; return c; } void launch(const char*,int) {} };
struct LLXferManager {
    using Callback = void(*)(void**,S32,LLExtStat);
    enum { HIGH_PRIORITY };
    U64 next = 0;
    std::map<U64, std::pair<Callback,void**>> pending;
    U64 requestFile(std::string,std::string,int,int,bool,Callback cb,void** context,int) {
        pending[++next] = {cb,context}; return next;
    }
    bool synchronousAbort = false;
    void abortRequestById(U64 id,int error) { if (synchronousAbort) finish(id,error); }
    void finish(U64 id,int error) {
        auto [callback,context] = pending.at(id);
        pending.erase(id);
        callback(context,error,LLExtStat::NONE);
    }
} transfers, *gXferManager = &transfers;
struct LLViewerObject {
    Region region, *mRegionp = &region;
    LLUUID mID = 7; int mLocalID = 1;
    int mInvRequestState = INVENTORY_REQUEST_STOPPED;
    U64 mInvRequestXFerId = 0;
    S16 mInventorySerialNum = 0, mExpectedInventorySerialNum = 0;
    LLInventoryObject::object_list_t* mInventory = nullptr;
    std::list<LLUUID> mPendingInventoryItemsIDs;
    struct Listener { void inventoryChanged(LLViewerObject*,LLInventoryObject::object_list_t*,S16,void*) {} } listener;
    struct LLInventoryCallbackInfo { Listener* mListener; void* mInventoryData; } info{&listener, nullptr};
    using callback_list_t = std::list<LLInventoryCallbackInfo*>;
    callback_list_t mInventoryCallbacks{&info};
    bool mInventoryDirty = false, readable = true;
    int mInventoryServerGeneration = 0, mInventoryChangeGeneration = 0;
    bool mInventoryServerComplete = false;
    ~LLViewerObject() { delete mInventory; }
    bool isInventoryPending(); void requestInventory(); void fetchInventoryFromServer();
    static void fetchInventoryFromCapCoro(LLUUID) { std::abort(); }
    void fetchInventoryDelayed(int) { std::abort(); }
    static void processTaskInv(LLMessageSystem*,void**);
    static void processTaskInvFile(void**,S32,LLExtStat);
    bool loadTaskInvFile(const std::string&,bool complete) {
        if (!readable) return false;
        if (!mInventory) mInventory = new LLInventoryObject::object_list_t;
        doInventoryCallback(complete); return true;
    }
    void doInventoryCallback(bool from_simulator = false, bool complete = true);
} object;
struct { LLViewerObject* findObject(LLUUID id) { return id == object.mID ? &object : nullptr; } } gObjectList;

#include "callbacks.inc"

int main() {
    auto reply = []() { object.processTaskInv(&messages,nullptr); return transfers.next; };
    object.requestInventory();
    CHECK(messages.sent == 1);
    transfers.finish(reply(), -1); // remote abort or circuit loss
    object.requestInventory();
    CHECK(messages.sent == 2);
    CHECK(object.mInventoryServerGeneration == 0);

    object.readable = false; // transfer finished, but its cache file cannot be opened
    transfers.finish(reply(), 0);
    object.requestInventory();
    CHECK(messages.sent == 3 && object.mInventoryServerGeneration == 0);
    object.readable = true;

    // Replacements can have the same inventory serial. Late data or an abort
    // from the old request must neither publish Contents nor unlock the new one.
    for (int oldResult : {-1,0}) {
        const auto first = reply(), replacement = reply();
        transfers.finish(first, oldResult);
        CHECK(object.isInventoryPending() && object.mInvRequestXFerId == replacement);
        CHECK(object.mInventoryServerGeneration == 0);
        transfers.finish(replacement, -1);
        CHECK(!object.isInventoryPending());
    }
    transfers.synchronousAbort = true; // a queued transfer aborts inside processTaskInv
    reply(); const auto current = reply();
    object.doInventoryCallback(); // a local notification cannot consume the transfer
    CHECK(object.isInventoryPending() && object.mInvRequestXFerId == current);
    transfers.finish(current, 0);
    CHECK(!object.isInventoryPending() && object.mInventoryServerGeneration == 1);
    const auto previous = reply();
    messages.filename.clear(); reply(); // newer, authoritative empty Contents
    transfers.finish(previous, 0);
    CHECK(!object.isInventoryPending() && object.mInventoryServerGeneration == 2);
}
