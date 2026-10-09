/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionhostoptions.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    HostOptions options, decoded;
    assert(!options.voice && !options.hosted && options.muteBackground);
    options.standby[1] = Mode::Economy; options.voice = options.hosted = true; options.chat = false;
    assert(HostOptions::decode(options.encode(), decoded));
    assert(decoded.standby[1] == Mode::Economy && decoded.hosted && decoded.voice && !decoded.chat);
    const auto before = decoded.encode();
    assert(!HostOptions::decode(options.encode() + "voice=0\n", decoded));
    assert(!HostOptions::decode("FSH1\nbackground1=0\n", decoded));
    assert(!HostOptions::decode(options.encode() + "password=1\n", decoded));
    assert(!HostOptions::decode(std::string(513, 'x'), decoded));
    assert(decoded.encode() == before); // Failed parsing cannot partly enable voice/hosting.
    std::cout << "Bounded host-choice parsing, defaults, duplicate/unknown refusal and transactional decoding passed.\n";
}
