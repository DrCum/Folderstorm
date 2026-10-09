/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionpreview.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace fs_session;
int main()
{
    assert(previewPolicy(0, 0, 0) && previewPolicy(320, 180, 2));
    assert(previewPolicy(640, 360, 1) && previewPolicy(480, 270, 10));
    assert(!previewPolicy(0, 180, 2) && !previewPolicy(640, 360, 3));
    assert(!previewPolicy(1920, 1080, 2) && !previewSize({639, 360}));
    auto size = fittedPreview(1920, 1080, {320, 180});
    assert(size.width == 320 && size.height == 180);
    size = fittedPreview(1080, 1920, {640, 360});
    assert(size.width == 200 && size.height == 360);
    size = fittedPreview(2560, 720, {320, 180});
    assert(size.width == 320 && size.height == 90);
    size = fittedPreview((std::numeric_limits<std::uint32_t>::max)(), (std::numeric_limits<std::uint32_t>::max)(), {640, 360});
    assert(size.width == 360 && size.height == 360);
    assert(!previewSize(fittedPreview(0, 1080, {640, 360})));
    assert(!previewSize(fittedPreview(100000, 1, {640, 360})));
    Message session; session.kind = Kind::Status; session.worker[0] = 42;
    session.generation = 9; session.pid = 123; session.state = State::Ready; session.mode = Mode::Economy;
    session.account = "01234567-89ab-cdef-0123-456789abcdef"; session.grid = "secondlife";
    Message frame = session; frame.width = 320; frame.height = 180; frame.flags = PreviewFrame; frame.event = 1;
    Frame bytes{}; Message decoded;
    assert(encode(frame, bytes) && decode(bytes, decoded) && previewIdentity(decoded, session));
    auto bad = frame; ++bad.generation; assert(!previewIdentity(bad, session));
    bad = frame; ++bad.pid; assert(!previewIdentity(bad, session));
    bad = frame; ++bad.worker[0]; assert(!previewIdentity(bad, session));
    bad = frame; bad.account.clear(); assert(!previewIdentity(bad, session));
    bad = frame; bad.grid = "other"; assert(!previewIdentity(bad, session));
    bad = frame; bad.mode = Mode::Active; assert(!previewIdentity(bad, session));
    bad = frame; bad.state = State::Disconnected; assert(!previewIdentity(bad, session));
    bad = frame; bad.flags |= PreviewUnavailable; assert(!previewIdentity(bad, session));
    bad = frame; bad.flags = 0; assert(!previewIdentity(bad, session));
    bad = frame; bad.event = 0; assert(!previewIdentity(bad, session));
    bad = frame; bad.width = 10000; assert(!previewIdentity(bad, session));
    session.mode = Mode::Active; assert(!previewIdentity(frame, session));
    std::cout << "Bounded preview geometry, rates, wire metadata and session/owner guards passed.\n";
}
