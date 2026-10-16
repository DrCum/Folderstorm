/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "../fssquirtypolicy.h"
#include <cassert>
#include <iostream>

using namespace FSSquirtyPolicy;

int main()
{
    // Both normal and autoresponse paths reuse this classification.
    const IncomingIM resident{true, false, false, true, false, true};
    assert(eligibleIM(resident));
    IncomingIM region = resident;
    region.regionMessage = true;
    assert(!eligibleIM(region));
    auto excluded = resident;
    excluded.live = false;
    assert(!eligibleIM(excluded));
    excluded = resident;
    excluded.muted = true;
    assert(!eligibleIM(excluded));
    excluded = resident;
    excluded.addressedToAgent = false;
    assert(!eligibleIM(excluded));
    excluded = resident;
    excluded.simulatorNotice = true;
    assert(!eligibleIM(excluded));
    excluded = resident;
    excluded.receiveAllowed = false;
    assert(!eligibleIM(excluded));

    // A region message must neither squirt nor reset a resident's silence gap.
    Activity routing;
    assert(routing.incoming("sender", 0, 120, Mode::All, false, false));
    if (eligibleIM(region))
        assert(!routing.incoming("sender", 60, 120, Mode::All, false, false));
    assert(eligibleIM(resident) && routing.incoming("sender", 120, 120, Mode::All, false, false));

    assert(!allows(Mode::Off, true));
    assert(allows(Mode::All, false));
    assert(allows(Mode::Custom, true));
    assert(!allows(Mode::Custom, false));
    assert(validToken(std::string(43, 'a')));
    assert(validToken(std::string(32, '_')));
    assert(!validToken(""));
    assert(!validToken(std::string(31, 'a')));
    assert(!validToken(std::string(257, 'a')));
    assert(!validToken(std::string(32, 'a') + "\r\nInjected: header"));

    Activity activity;
    assert(activity.incoming("sam", 0, 120, Mode::All, false, false));
    assert(!activity.incoming("sam", 60, 120, Mode::All, false, false));
    assert(!activity.incoming("sam", 150, 120, Mode::All, false, false));
    assert(activity.incoming("sam", 270, 120, Mode::All, false, false));
    assert(activity.incoming("alex", 271, 120, Mode::Custom, true, false));
    assert(!activity.incoming("other", 272, 120, Mode::Custom, false, false));
    // A full hour of conversation must not notify based on time since squirt.
    for (int t = 300; t <= 3900; t += 60)
        assert(!activity.incoming("sam", t, 120, Mode::All, false, false));
    assert(!activity.incoming("sam", 4019, 120, Mode::All, false, false));
    assert(activity.incoming("sam", 4139, 120, Mode::All, false, false));
    assert(activity.incoming("sam", 4139, 0, Mode::All, false, false));
    assert(!activity.incoming("sam", 4260, 120, Mode::All, false, true));
    assert(!activity.incoming("sam", 4261, 120, Mode::All, false, false));
    assert(!activity.incoming("sam", 4381, 120, Mode::Off, false, false));
    assert(!activity.incoming("sam", 4382, 120, Mode::All, false, false));

    // Unknown and initial online states establish a quiet baseline.
    assert(!activity.presence("new", true, Mode::All, false, false));
    activity.seedPresence("sam", true);
    activity.seedPresence("alex", false);
    assert(!activity.presence("sam", true, Mode::All, false, false));
    assert(activity.presence("alex", true, Mode::Custom, true, false));
    assert(!activity.presence("alex", true, Mode::All, false, false));
    assert(!activity.presence("alex", false, Mode::All, false, false));
    assert(!activity.presence("alex", true, Mode::All, false, true));
    assert(!activity.presence("alex", true, Mode::All, false, false));
    assert(!activity.presence("new", false, Mode::Off, false, false));
    assert(!activity.presence("new", true, Mode::Custom, false, false));
    activity.reset();
    assert(activity.incoming("sam", 0, 120, Mode::All, false, false));
    assert(!activity.presence("sam", true, Mode::All, false, false));
    std::cout << "Squirty resident/region eligibility, inactivity, modes and presence passed\n";
}
