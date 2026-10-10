/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionpresentation.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    const AccountKey a{"01234567-89ab-cdef-0123-456789abcdef","secondlife"};
    PresentationStore store;
    assert(store.set(a,{"Driver",0x123456,2}));
    assert(store.appearance(a).alias == "Driver");
    auto other = a; other.grid = "another-grid"; assert(store.appearance(other).alias.empty());
    store.rectangles[0] = {-1600,-20,1200,800,144};
    store.pins.push_back({a,Topic::Nearby,""});
    const auto data = store.encode(); assert(!data.empty());
    PresentationStore decoded; assert(PresentationStore::decode(data,decoded));
    assert(decoded.encode() == data && decoded.rectangles[0].x == -1600);
    const auto original = decoded.encode();
    for (std::size_t i = 0; i < data.size(); ++i)
    { assert(!PresentationStore::decode(data.substr(0,i),decoded)); assert(decoded.encode() == original); }
    assert(!PresentationStore::decode(data+"x",decoded));
    assert(!PresentationStore::decode(std::string(65537,'x'),decoded));
    assert(!store.set(a,{std::string(65,'x'),0,0}));
    assert(!store.set(a,{"bad\nname",0,0}));
    assert(!store.set(a,{"",0x1000000,0}));
    auto invalid = store; invalid.bindings[1] = invalid.bindings[0]; assert(invalid.encode().empty());
    invalid = store; invalid.pins.push_back(invalid.pins.front()); assert(invalid.encode().empty());
    invalid = store; invalid.duration = 249; assert(invalid.encode().empty());
    invalid = store; invalid.rectangles[8] = invalid.rectangles[0]; assert(invalid.encode().empty());
    const auto fitted = fitShellRect({-2000,-100,1200,800,144},{0,0,1920,1080,96},192);
    assert(fitted.x == 0 && fitted.y == 0 && fitted.width == 1600 && fitted.height == 1066);
    const auto negative = fitShellRect({-1800,0,700,500,96},{-1920,0,1920,1080,96},96);
    assert(negative.x == -1800);
    std::cout << "Bounded account presentation persistence, transactional rejection and monitor fitting passed.\n";
}
