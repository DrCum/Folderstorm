/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsobjecttoolsmodel.h"
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
}
