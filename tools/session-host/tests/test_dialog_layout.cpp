#include "fssessiondialoglayout.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    auto fit=editorScrollLayout(500,260,500,260,16);
    assert(!fit.horizontal && !fit.vertical && fit.width==500 && fit.height==260);
    fit=editorScrollLayout(500,1000,520,280,16);
    assert(!fit.horizontal && fit.vertical && fit.width==504);
    fit=editorScrollLayout(500,1000,500,280,16);
    assert(fit.horizontal && fit.vertical && fit.width==484 && fit.height==264);
    fit=editorScrollLayout(500,130,520,280,16);
    assert(!fit.horizontal && !fit.vertical); // Short tab removes previous overflow.
    const auto thumb=scrollThumbLayout(300,16,0,999,250,500);
    assert(thumb.maximum==750 && thumb.length>=16 && thumb.start>=thumb.edge);
    assert(scrollPositionAt(thumb,thumb.edge-100,0)==0);
    assert(scrollPositionAt(thumb,1000,0)==750);
    assert(scrollPositionAt(thumb,thumb.start+5,5)>=490);
    const auto full=scrollThumbLayout(30,16,0,9,100,0);assert(full.maximum==0 && full.travel==0 && scrollPositionAt(full,10,0)==0);
    std::cout << "Editor overflow and scrollbar interaction geometry passed.\n";
}
