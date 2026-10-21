#include "fsupdatepolicy.h"
#include <cassert>
int main()
{
    using namespace fs_update;
    assert(packageVersion("7.2.5.123")=="7.2.5-123" && packageVersion("bad").empty());
    Barrier barrier{"7.2.5.1","7.2.5.2",std::string(40,'a')};
    assert(Barrier::decode(barrier.encode())->target==barrier.target);
    auto invalid=barrier;invalid.target=invalid.source;assert(!Barrier::decode(invalid.encode()));
    invalid.target="7.2.4.999";assert(!Barrier::decode(invalid.encode()));
    assert(!Barrier::decode(barrier.encode()+"extra"));
    Coordinator coordinator;assert(!coordinator.mayApply(true,true,true,true));
    coordinator.request();
    // Kept-logged-in and MCP processes, failed leases, stale installations and
    // changed versions all block application; repeated requests do not bypass it.
    assert(!coordinator.mayApply(false,true,true,true));
    assert(!coordinator.mayApply(true,false,true,true));
    assert(!coordinator.mayApply(true,true,false,true));
    assert(!coordinator.mayApply(true,true,true,false));
    coordinator.request();assert(coordinator.mayApply(true,true,true,true));
    coordinator.cancel();assert(!coordinator.mayApply(true,true,true,true));
    Coordinator accepted;accepted.request();accepted.applying();accepted.cancel();assert(accepted.phase==Phase::Applying);
}
