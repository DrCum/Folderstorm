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
    std::cout << "Editor overflow and scrollbar interaction geometry passed.\n";
}
