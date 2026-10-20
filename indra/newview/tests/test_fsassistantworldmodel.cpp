#include "fsassistantworldmodel.h"
#include <cassert>
#include <limits>
int main()
{
    using namespace fs_assistant_world;
    assert(quaternion({0.,0.,0.,1.}));
    assert(!quaternion({0.,0.,0.,0.}));
    assert(!quaternion({0.,0.,0.,std::numeric_limits<double>::quiet_NaN()}));
    assert(!nextOwner(false,false)); assert(nextOwner(false,true)); assert(nextOwner(true,false));
    Tickets<int> tickets;
    tickets.put("first","alice/login/region",1.,4);
    assert(tickets.get("first","alice/login/region",2.)->value==4);
    assert(!tickets.get("first","bob/login/region",2.));
    assert(!tickets.get("first","alice/new-login/region",2.));
    auto* entry=tickets.get("first","alice/login/region",2.); entry->executed=true;
    tickets.put("first","bob/login/region",2.,8); // Cannot replace live approval.
    assert(tickets.get("first","alice/login/region",3.)->executed);
    for(int i=0;i<63;++i) tickets.put(std::to_string(i),"source",2.,i);
    tickets.put("overflow","source",2.,99); assert(!tickets.get("overflow","source",3.));
    assert(tickets.get("first","alice/login/region",120.));
    assert(!tickets.get("first","alice/login/region",121.));
    tickets.put("new","source",123.,7); assert(tickets.get("new","source",124.));
    tickets.clear(); assert(!tickets.get("new","source",124.));
}
