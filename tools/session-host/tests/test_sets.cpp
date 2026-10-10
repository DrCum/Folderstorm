#include "fssessionsets.h"
#include <cassert>
int main()
{
    using namespace fs_session;const AccountKey account{"11111111-1111-1111-1111-111111111111","grid"};
    SessionSet set;set.name="Main + alts";set.profiles={{0,Mode::Warm,account},{2,Mode::Economy,{}}};set.captureMonitors=true;set.monitors={{0,1,2,true,account,{0,0,500,400,96}}};
    SessionSetStore store,decoded;assert(store.put(set,false));assert(!store.put(set,false));assert(SessionSetStore::decode(store.encode(),decoded));assert(decoded.sets.at(set.name).profiles.size()==2);
    const auto bytes=store.encode();assert(!SessionSetStore::decode(bytes+"x",decoded));assert(!SessionSetStore::decode(bytes.substr(0,13),decoded));assert(!SessionSetStore::decode(std::string(70000,'x'),decoded));assert(decoded.sets.size()==1);
    auto invalid=set;invalid.profiles.push_back(invalid.profiles.front());assert(!store.put(invalid,true));invalid=set;invalid.profiles[0].slot=5;assert(!invalid.valid());invalid=set;invalid.captureMonitors=false;assert(!invalid.valid());
    invalid=set;invalid.monitors[0].rect={0,0,0,10,96};assert(!invalid.valid());invalid.monitors[0].rect={};assert(invalid.valid());
    SessionSetOpen op;const auto old=op.begin(set,100);assert(op.owns(old,200));op.cancel();assert(!op.owns(old,200));const auto next=op.begin(set,200);assert(next!=old && !op.owns(old,200));assert(!op.owns(next,400000));
    // A replacement confirmation first revokes polling, even if the user cancels it.
    op.cancel();const auto confirmation=op.token;assert(!op.active && !op.owns(next,201));
    op.begin(set,202);assert(op.token!=confirmation); // A late confirmation cannot replace newer intent.
    Message live;live.worker[0]=1;live.generation=3;live.account=account.account;live.grid=account.grid;op.workers[0]=live.worker;op.generations[0]=3;assert(op.accept(set.profiles[0],live));live.generation=4;assert(!op.accept(set.profiles[0],live));live.generation=3;live.account="22222222-2222-2222-2222-222222222222";assert(!op.accept(set.profiles[0],live));
    assert(op.sourceChanged(0,live.worker,live.generation,false));assert(!op.sourceChanged(0,live.worker,live.generation,true));
    op.skip(0,"existing controller unavailable");assert(op.finished[0] && !op.sourceChanged(0,live.worker,live.generation,false));assert(op.active && !op.finished[2] && op.monitorDone[0]);
}
