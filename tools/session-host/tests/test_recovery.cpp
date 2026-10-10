#include "fssessionrecovery.h"
#include <cassert>
int main()
{
    using namespace fs_session;
    assert(recoveredChrome(2,true)==1);
    assert(recoveredChrome(0,true)==0);
    assert(recoveredChrome(0)==1);
    assert(revealHeight(2)==24 && revealHeight(1)==0);
    const ShellRect left{-1920,0,1920,1080,96};
    auto r=reachableRect({9000,5000,800,500,96},left,144);
    assert(r.valid() && r.x>=-1920 && r.x+r.width<=0 && r.y+r.height<=1080);
    r=reachableRect({},left,96);
    assert(r.valid() && r.x>=-1920 && r.x+r.width<=0);
}
