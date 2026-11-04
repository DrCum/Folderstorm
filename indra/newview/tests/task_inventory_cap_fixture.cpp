/** SPDX-License-Identifier: LGPL-2.1-or-later */
// Run the production coroutine with a controlled HTTP suspension point.
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#define LL_WARNS() std::clog
#define LL_INFOS() std::clog
#define LL_DEBUGS() std::clog
#define LL_ENDL '\n'
using S16=short;
using U64=std::uint64_t;
struct LLUUID { std::string asString() const { return "same-object-id"; } };
std::ostream& operator<<(std::ostream& s, LLUUID id) { return s << id.asString(); }
struct LLSD {
    int status, serial;
    LLSD operator[](const char*) const { return *this; }
    int asInteger() const { return serial; }
};
namespace LLCore {
    struct HttpRequest { using ptr_t=std::shared_ptr<HttpRequest>; using policy_t=int; enum {DEFAULT_POLICY_ID}; };
    struct HttpStatus {
        int code;
        explicit operator bool() const { return code==200; }
        int getType() const { return code; }
        std::string toString() const { return std::to_string(code); }
    };
}
std::function<LLSD()> get;
namespace LLCoreHttpUtil {
    struct HttpCoroutineAdapter {
        using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;
        static constexpr const char* HTTP_RESULTS="http";
        HttpCoroutineAdapter(const char*,int) {}
        LLSD getAndSuspend(LLCore::HttpRequest::ptr_t,const std::string&) { return get(); }
        static LLCore::HttpStatus getStatusFromLLSD(LLSD value) { return {value.status}; }
    };
}
enum { INVENTORY_REQUEST_STOPPED, INVENTORY_XFER, INVENTORY_UPDATE_WAIT_TIME_OUTDATED };
struct Region { std::string getCapability(const char*) { return "region/cap"; } };
struct LLViewerObject {
    Region region,*mRegionp=&region;
    LLUUID mID;
    S16 mInventorySerialNum=0, mExpectedInventorySerialNum=0;
    int mInvRequestState=INVENTORY_REQUEST_STOPPED;
    bool mInventory=false;
    U64 mInventoryCapRequest=0;
    int received=0, retries=0;
    void loadTaskInvLLSD(LLSD,bool) { ++received; mInvRequestState=INVENTORY_REQUEST_STOPPED; }
    void fetchInventoryDelayed(int) { ++retries; }
    static void fetchInventoryFromCapCoro(const LLUUID);
};
struct {
    LLViewerObject* current=nullptr;
    LLViewerObject* findObject(LLUUID) { return current; }
} gObjectList;

#include "cap_fetch.inc"

int main() {
    LLViewerObject old, replacement;
    gObjectList.current=&old;
    int calls=0;
    get=[&]() -> LLSD {
        if (++calls==1) {
            // Re-entry into view creates another local object for the same prim.
            gObjectList.current=&replacement;
            LLViewerObject::fetchInventoryFromCapCoro(replacement.mID);
            return {200,12}; // late first reply, after replacement received serial 13
        }
        return {200,13};
    };
    LLViewerObject::fetchInventoryFromCapCoro(old.mID);
    assert(calls==2 && old.received==0);
    assert(replacement.received==1 && replacement.mInventorySerialNum==13 && !replacement.retries);

    for (int status : {304,503}) {
        LLViewerObject waiting;
        gObjectList.current=&old;
        get=[&]() -> LLSD {
            gObjectList.current=&waiting;
            waiting.mInvRequestState=INVENTORY_XFER;
            return {status,0};
        };
        LLViewerObject::fetchInventoryFromCapCoro(old.mID);
        assert(waiting.mInvRequestState==INVENTORY_XFER && !waiting.received && !waiting.retries);
    }
}
