/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionsound.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    WavSound sound; const auto bytes = builtinAlertTone(100);
    assert(WavSound::decode(bytes,sound) && sound.bits == 16 && sound.dataBytes == 7056);
    const auto original = sound.bytes;
    assert(sound.volume(100) == bytes && !sound.volume(50).empty() && sound.volume(101).empty());
    const auto mute = sound.volume(0);
    assert(std::all_of(mute.begin()+44,mute.end(),[](std::uint8_t n){return n == 0;}));
    auto eight = bytes; eight.resize(46); eight[4] = 38; eight[5] = eight[6] = eight[7] = 0;
    eight[28] = eight[24]; eight[29] = eight[25]; eight[30] = eight[26]; eight[31] = eight[27];
    eight[32] = 1; eight[34] = 8; eight[40] = 1; eight[41] = eight[42] = eight[43] = 0; eight[44] = 255; eight[45] = 0;
    WavSound pcm8; assert(WavSound::decode(eight,pcm8));
    assert(pcm8.volume(0)[44] == 128 && pcm8.volume(50)[44] == 191);
    eight.pop_back(); assert(!WavSound::decode(eight,pcm8)); // Odd chunk padding is required.
    auto malformed = bytes; malformed[16] = 255; assert(!WavSound::decode(malformed,sound) && sound.bytes == original);
    malformed = bytes; malformed[34] = 24; assert(!WavSound::decode(malformed,sound));
    malformed = bytes; malformed.pop_back(); assert(!WavSound::decode(malformed,sound));
    malformed = bytes; malformed[28] = 0; assert(!WavSound::decode(malformed,sound));
    malformed = bytes; malformed[20] = 3; assert(!WavSound::decode(malformed,sound)); // Compressed/float excluded.
    malformed.assign(WavSound::MaxBytes+1,0); assert(!WavSound::decode(malformed,sound));
    assert(!WavSound::decode({},sound));
    std::cout << "Bounded PCM WAV validation, transactional rejection and volume scaling passed.\n";
}
