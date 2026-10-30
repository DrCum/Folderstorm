/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsobjecttoolsmodel.h"
#include "fsassistantsocialmodel.h"
#include <cassert>
#include <string>
int main()
{
    fs_object_tools::Journal<std::string> stream(3,8);
    stream.append("a",2);stream.append("b",2);stream.append("c",2);
    assert(!stream.gap(0));stream.append("d",2);
    assert(stream.gap(0) && !stream.gap(1) && stream.entries().front().value=="b");
    stream.append("oversize",9);assert(stream.gap(4) && stream.entries().empty());
    stream.append("e",2);assert(stream.gap(4) && !stream.gap(5));
    stream.clear();assert(stream.latest()==0 && !stream.gap(0));

    fs_object_tools::Receipts<std::string> ledger(2);
    auto* late=ledger.admit("late","args-a","submitted");assert(late);late->pending=1;late->terminal=true;
    auto* done=ledger.admit("done","args-b","done");assert(done);done->terminal=true;
    for(int i=0;i<200;++i)assert(ledger.find("late")==late); // Polling never evicts.
    assert(ledger.admit("third","args-c","new"));assert(!ledger.find("done") && ledger.find("late")==late);
    assert(!ledger.admit("fourth","args-d","new"));
    assert(ledger.admit("late","args-a","ignored")==late && late->value=="submitted");
    assert(!ledger.admit("late","changed-args","new"));
    late->value="late-confirmed-id";assert(ledger.find("late")->value=="late-confirmed-id");
    late->pending=0;assert(ledger.admit("fourth","args-d","new"));assert(!ledger.find("late"));
    assert(!ledger.admit("late","args-a","must not replay"));
    const std::set<std::string> before{"old"};
    auto match=fs_object_tools::correlateInsertion(before,{{"old","asset",10},{"new","asset",10},{"other","different",10}},"asset",10);
    assert(match.id=="new" && !match.ambiguous);
    match=fs_object_tools::correlateInsertion(before,{{"a","asset",10},{"b","asset",10}},"asset",10);
    assert(match.id.empty() && match.ambiguous);
    match=fs_object_tools::correlateInsertion(before,{{"old","asset",10},{"wrong-type","asset",5}},"asset",10);
    assert(match.id.empty() && !match.ambiguous);
    fs_object_tools::Acquisition ackFirst;ackFirst.acknowledge(true);assert(!ackFirst.confirmed());
    ackFirst.observe("result");assert(ackFirst.confirmed());ackFirst.observe("result");assert(ackFirst.confirmed());
    assert(!ackFirst.sourceRemovalObserved());ackFirst.observeSourceKill();assert(ackFirst.sourceRemovalObserved());
    fs_object_tools::Acquisition inventoryFirst;inventoryFirst.observe("result");assert(!inventoryFirst.confirmed());
    inventoryFirst.acknowledge(true);assert(inventoryFirst.confirmed());
    inventoryFirst.observe("other");assert(inventoryFirst.ambiguous() && !inventoryFirst.confirmed() && inventoryFirst.item().empty());
    fs_object_tools::Acquisition rejected;rejected.acknowledge(false);rejected.observe("late");assert(rejected.rejected() && !rejected.confirmed());
    fs_assistant_social::PrivacyIngressFence privacy;privacy.advance(0,10.);
    assert(!privacy.allowsNearby(9.) && !privacy.allowsNearby(10.) && privacy.allowsNearby(10.1));
    privacy.advance(0,20.);assert(!privacy.allowsNearby(10.1) && privacy.allowsNearby(20.1));
    fs_object_tools::ExecutionVersions<std::string> versions;
    const auto oldRead=versions.current("script");assert(versions.advance("script"));
    assert(oldRead!=versions.current("script"));
    for(int i=0;i<10000;++i)assert(versions.current("read-only-"+std::to_string(i))==0);
    // Queries do not consume the version store's mutation capacity.
    for(int i=0;i<4095;++i)assert(versions.advance("write-"+std::to_string(i)));
    assert(!versions.advance("capacity") && versions.advance("script"));
    fs_object_tools::RunningQueries<std::string> queries;
    using Admission=fs_object_tools::RunningQueries<std::string>::Admission;
    assert(queries.begin("script",0)==Admission::Send);
    assert(queries.begin("script",0)==Admission::Join);
    // A caller timeout/revocation does not drain a native no-nonce reply.
    assert(queries.pending("script") && queries.begin("script",1)==Admission::Blocked);
    std::uint64_t version=99;assert(queries.drain("script",version) && version==0);
    assert(!queries.pending("script") && queries.begin("script",1)==Admission::Send);
    assert(queries.drain("script",version) && version==1 && !queries.drain("script",version));
}
