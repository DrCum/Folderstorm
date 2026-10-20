#include "fsnavigationmodel.h"
#include <cassert>
#include <limits>
int main()
{
    using namespace fs_navigation;
    assert(owns("assistant-1",true,"assistant-1"));
    assert(!owns("assistant-1",true,"native-follow"));
    assert(!owns("assistant-1",false,"assistant-1"));
    assert(!owns("",true,""));
    assert(canContinue(true,true,true,true,9,10));
    assert(!canContinue(false,true,true,true,9,10));
    assert(!canContinue(true,false,true,true,9,10));
    assert(!canContinue(true,true,false,true,9,10));
    assert(!canContinue(true,true,true,false,9,10));
    assert(!canContinue(true,true,true,true,10,10));
    assert(bounds(64,.25,30));
    assert(!bounds(64.01,.25,30));
    assert(!bounds(1,.24,30));
    assert(!bounds(1,4,31));
    assert(!bounds(std::numeric_limits<double>::quiet_NaN(),1,10));
}
