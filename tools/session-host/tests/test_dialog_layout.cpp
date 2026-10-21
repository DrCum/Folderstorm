#include "fssessiondialoglayout.h"
#include <cassert>
#include <iostream>
#include <limits>
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
    assert(scrollHit(thumb,300,0)==ScrollHit::LineUp);
    assert(scrollHit(thumb,300,299)==ScrollHit::LineDown);
    assert(scrollHit(thumb,300,thumb.edge)==ScrollHit::PageUp);
    assert(scrollHit(thumb,300,thumb.start)==ScrollHit::Thumb);
    assert(scrollHit(thumb,300,thumb.start+thumb.length-1)==ScrollHit::Thumb);
    assert(scrollHit(thumb,300,thumb.start+thumb.length)==ScrollHit::PageDown);
    assert(scrollHit(thumb,300,-1)==ScrollHit::None);
    assert(scrollHit(thumb,300,300)==ScrollHit::None);
    const auto full=scrollThumbLayout(30,16,0,9,100,0);assert(full.maximum==0 && full.travel==0 && scrollPositionAt(full,10,0)==0);
    assert(scrollHit(full,30,1)==ScrollHit::None);
    // An unknown page size must not invent an extra item beyond the last one.
    const auto noPage=scrollThumbLayout(190,17,0,23,0,23);
    assert(noPage.maximum==23 && scrollPositionAt(noPage,190,0)==23);
    const auto large=scrollThumbLayout(300,16,std::numeric_limits<int>::min(),std::numeric_limits<int>::max(),0,0);
    assert(scrollPositionAt(large,16,0)==std::numeric_limits<int>::min());
    assert(scrollPositionAt(large,1000,0)==std::numeric_limits<int>::max());
    assert(scrollPositionAt(large,std::numeric_limits<int>::min(),std::numeric_limits<int>::max())==std::numeric_limits<int>::min());
    // Dragging and painting share these boundaries even at short/scaled sizes.
    for(const int extent:{12,40,190,300,450})for(const int cross:{12,17,24,32})for(const unsigned int page:{0u,1u,8u,24u})
    {
        const auto g=scrollThumbLayout(extent,cross,0,23,page,6);
        assert(g.start>=g.edge && g.start+g.length<=extent-g.edge);
        for(int point=-1;point<=extent;++point)
        {const int position=scrollPositionAt(g,point,g.length/2);assert(position>=g.minimum && position<=g.maximum);}
    }
    std::cout << "Editor overflow and scrollbar interaction geometry passed.\n";
}
