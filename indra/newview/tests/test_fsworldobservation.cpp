/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fsworldobservation.h"
#include <limits>
#include <stdexcept>

static void check(bool value) { if (!value) throw std::runtime_error("World observation regression"); }
int main()
{
    using namespace fs_world;
    check(parcelCurrent(1,"region-b","region-b"));
    check(!parcelCurrent(1,"region-a","region-b"));
    check(!parcelCurrent(0,"region-b","region-b"));
    check(!parcelCurrent(1,"",""));
    for (const auto* op : {"location", "parcel", "avatars", "objects", "audio", "media"})
    {
        check(operation(op));
        check(visible(op, true, true, true));
        check(!visible(op, false, true, true));
        if (std::string(op) != "location")
        {
            check(!visible(op, true, false, true));
            check(!visible(op, true, true, false));
        }
    }
    for (const auto* op : {"requestTeleport", "startAutoPilot", "requestSit", "requestStand", "requestTouch", "edit", "build", "get"})
    {
        check(!operation(op));
        check(!visible(op, true, true, true));
    }
    check(bounds(1., 1) && bounds(MAX_RADIUS, MAX_ROWS));
    check(!bounds(0., 1) && !bounds(MAX_RADIUS + 1., 1));
    check(!bounds(DEFAULT_RADIUS, 0) && !bounds(DEFAULT_RADIUS, MAX_ROWS + 1));
    check(!bounds(std::numeric_limits<double>::quiet_NaN(), 1));
    check(!bounds(std::numeric_limits<double>::infinity(), 1));
    const Identity original{"account", "login", "grid", "region"};
    check(original == original);
    check(!(original == Identity{"other", "login", "grid", "region"}));
    check(!(original == Identity{"account", "replacement", "grid", "region"}));
    check(!(original == Identity{"account", "login", "other-grid", "region"}));
    check(!(original == Identity{"account", "login", "grid", "other-region"}));
    Epoch epoch;
    check(!epoch.containsSound(1., 2.));
    epoch.observe(original, 10.);
    check(epoch.generation() == 1 && epoch.audioSince() == 10.);
    check(!epoch.containsSound(9., 20.) && epoch.containsSound(10., 20.));
    check(!epoch.containsSound(21., 20.));
    check(!epoch.containsSound(std::numeric_limits<double>::quiet_NaN(), 20.));
    epoch.observe(original, 20.);
    check(epoch.generation() == 1 && epoch.audioSince() == 10.);
    epoch.observe(Identity{"account", "replacement", "grid", "region"}, 30.);
    check(epoch.generation() == 2 && !epoch.containsSound(20., 40.));
    epoch.observe(Identity{"account", "replacement", "grid", "other-region"}, 40.);
    check(epoch.generation() == 3 && !epoch.containsSound(30., 50.));
    epoch.observe(Identity{"account", "replacement", "grid", "region"}, 50.);
    check(epoch.generation() == 4 && !epoch.containsSound(10., 60.));
    Rows<int> first(2), second(2);
    first.add("c", 3); first.add("a", 1); first.add("b", 2);
    second.add("b", 2); second.add("c", 3); second.add("a", 1);
    check(first.values() == second.values());
    check(first.values().size() == 2 && first.values().count("a") && first.values().count("b"));
    check(first.truncated() && second.truncated());
    Rows<int> empty(2); check(empty.values().empty() && !empty.truncated());
    empty.add("a", 1); empty.add("a", 2);
    check(empty.values().size() == 1 && !empty.truncated());
    Rows<int> none(0); none.add("a", 1); check(none.values().empty() && none.truncated());
}
