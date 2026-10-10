/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionhostoptions.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    HostOptions options, decoded;
    assert(!options.voice && !options.hosted && options.muteBackground && !options.cinematic);
    options.standby[1] = Mode::Economy; options.voice = options.hosted = true; options.chat = false;
    assert(HostOptions::decode(options.encode(), decoded));
    assert(decoded.standby[1] == Mode::Economy && decoded.hosted && decoded.voice && !decoded.chat);
    assert(decoded.previewSize == 0 && decoded.previewRate == 1);
    options.previewSize = 2; options.previewRate = 3; options.cinematic = true;
    assert(HostOptions::decode(options.encode(), decoded) && decoded.previewSize == 2 && decoded.previewRate == 3 && decoded.cinematic);
    options.chrome = 2;
    assert(HostOptions::decode(options.encode(), decoded) && decoded.chrome == 2);
    auto badChrome = options.encode(); badChrome.replace(badChrome.find("chrome=2"), 8, "chrome=3");
    assert(!HostOptions::decode(badChrome, decoded));
    const auto before = decoded.encode();
    assert(!HostOptions::decode(options.encode() + "voice=0\n", decoded));
    assert(!HostOptions::decode("FSH1\nbackground1=0\n", decoded));
    assert(!HostOptions::decode(options.encode() + "password=1\n", decoded));
    assert(!HostOptions::decode(std::string(513, 'x'), decoded));
    assert(decoded.encode() == before); // Failed parsing cannot partly enable voice/hosting.
    auto invalid = options.encode(); invalid.replace(invalid.find("preview_size=2"), 14, "preview_size=3");
    assert(!HostOptions::decode(invalid, decoded) && decoded.encode() == before);
    assert(HostOptions::decode("FSH1\nbackground1=1\nbackground2=2\nvoice=0\nmute=1\nhost=0\nchat=1\n", decoded));
    assert(decoded.previewSize == 0 && decoded.previewRate == 1 && !decoded.cinematic && decoded.chrome == 0); // Prior saved choices stay compatible.
    std::cout << "Bounded host-choice parsing, defaults, duplicate/unknown refusal and transactional decoding passed.\n";
}
