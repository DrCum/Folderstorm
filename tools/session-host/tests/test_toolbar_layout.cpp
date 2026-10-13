/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessiontoolbarlayout.h"
#include <cassert>
#include <iostream>
#ifdef NDEBUG
#error Toolbar checks require assertions in Release.
#endif
using namespace fs_session;
int main()
{
    const std::vector<QuickButtonSize> buttons{{100,28},{80,28},{75,40}};
    auto r=quickToolbarLayout(300,buttons);assert(r.visible==3 && !r.overflow && r.widths[0]==100);
    r=quickToolbarLayout(200,buttons);assert(r.visible==3 && !r.overflow && r.widths[0]==100 && r.widths[2]==40);
    r=quickToolbarLayout(130,buttons);assert(r.visible==3 && !r.overflow && r.widths[0]==28);
    r=quickToolbarLayout(90,buttons);assert(r.visible==1 && r.overflow && r.used<=90);
    r=quickToolbarLayout(10,buttons);assert(r.visible==0 && r.overflow && r.used==10);
    r=quickToolbarLayout(0,buttons);assert(!r.visible && r.used==0);
    r=quickToolbarLayout(300,{});assert(!r.visible && !r.overflow && r.used==0);
    for(int width=0;width<600;++width){r=quickToolbarLayout(width,buttons);assert(r.used<=width && r.visible<=buttons.size());}
    std::cout<<"Quick actions retain order, collapse labels, and fit bounded overflow.\n";
}
