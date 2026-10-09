/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionprotocol.h"
#include <cassert>
#include <iostream>
using namespace fs_session;

Message sample()
{
    Message result;
    result.kind = Kind::Status; result.worker[0] = 42;
    result.generation = 7; result.sequence = 91; result.pid = 123;
    result.state = State::Ready; result.mode = Mode::Active;
    result.account = "01234567-89ab-cdef-0123-456789abcdef";
    result.grid = "secondlife"; result.name = "Alex \xc3\xa9";
    result.surface = 0x123456789abcdef0ULL; result.frames = 2000; result.maintenance = 2300;
    return result;
}
int main()
{
    const Message original = sample();
    Frame frame{};
    assert(encode(original, frame));
    assert(frame[0] == 'F' && frame[3] == '1' && frame[56] == 0xf0 && frame[63] == 0x12);
    Message output;
    assert(decode(frame, output));
    assert(output.worker == original.worker && output.generation == 7 && output.sequence == 91);
    assert(output.surface == original.surface && output.name == original.name && output.account == original.account);
    auto corrupt = frame;
    corrupt[4] = 99; assert(!decode(corrupt, output));
    corrupt = frame; put(corrupt, 4, Version - 1, 4); assert(!decode(corrupt, output));
    corrupt = frame; corrupt[12] = 128; assert(!decode(corrupt, output));
    corrupt = frame; corrupt[8] = 255; assert(!decode(corrupt, output));
    corrupt = frame; corrupt[92] = 1; assert(!decode(corrupt, output));
    corrupt = frame; std::fill(corrupt.begin() + 264, corrupt.begin() + 392, 'a'); assert(!decode(corrupt, output));
    corrupt = frame; corrupt[265] = 0; corrupt[266] = 'b'; assert(!decode(corrupt, output));
    corrupt = frame; corrupt[264] = 0xc0; corrupt[265] = 0x80; assert(!decode(corrupt, output));
    assert(output.name == original.name); // Invalid wire input never replaces a prior snapshot.
    auto invalid = original;
    invalid.name.assign(128, 'x'); assert(!encode(invalid, frame));
    invalid.name.assign(127, 'x'); assert(encode(invalid, frame));
    invalid.name = "a\nb"; assert(!encode(invalid, frame));
    invalid = original; invalid.worker.fill(0); assert(!encode(invalid, frame));
    invalid = original; invalid.sequence = 0; assert(!encode(invalid, frame));
    invalid = original; invalid.account = "not-an-account"; assert(!encode(invalid, frame));
    auto focused = original; focused.flags = ClientFocused | Embedded;
    assert(encode(focused, frame) && decode(frame, output) && output.flags == focused.flags);
    auto focus_request = original; focus_request.kind = Kind::Focus; focus_request.flags = 0;
    assert(encode(focus_request, frame) && decode(frame, output) && output.kind == Kind::Focus);
    assert(!validUtf8("\xed\xa0\x80") && !validUtf8("\xf4\x90\x80\x80") && !validUtf8("\xe2\x82"));
    std::string filename;
    assert(viewerFilename("Folderstorm-Release.exe\r\n", filename) && filename == "Folderstorm-Release.exe");
    assert(viewerFilename("firestorm-bin.EXE\n", filename));
    assert(!viewerFilename("../firestorm-bin.exe", filename));
    assert(!viewerFilename("C:\\viewer.exe", filename));
    assert(!viewerFilename("viewer.exe:stream.exe", filename));
    assert(!viewerFilename("viewer.exe\nother.exe", filename));
    assert(!viewerFilename(std::string(250, 'x') + ".exe", filename));

    Handoff handoff;
    assert(handoff.begin(0, 1, 7, 9));
    assert(!handoff.begin(0, 1, 7, 9)); // Serial switching, including rapid clicks.
    assert(handoff.worker() == 0 && handoff.mode() == Mode::Warm);
    auto reply = original; reply.mode = Mode::Warm;
    assert(!handoff.accept(1, reply));
    reply.generation = 8; assert(!handoff.accept(0, reply));
    reply.generation = 7; reply.flags = Embedded; assert(!handoff.accept(0, reply));
    reply.flags = Error; assert(!handoff.accept(0, reply));
    reply.flags = 0; assert(handoff.accept(0, reply));
    assert(handoff.step() == Handoff::Step::Promote && handoff.worker() == 1 && handoff.generation() == 9);
    reply.mode = Mode::Active; reply.generation = 9; reply.flags = Promoting;
    assert(!handoff.accept(1, reply)); // No ownership until the rendered-frame acknowledgment.
    reply.flags = 0; assert(handoff.accept(1, reply)); assert(handoff.step() == Handoff::Step::Idle);

    assert(handoff.begin(0, 1, 7, 9));
    reply.generation = 7; reply.mode = Mode::Warm; reply.state = State::Disconnected;
    assert(handoff.accept(0, reply)); // A disconnected old session cannot trap the healthy target.
    handoff.fail(); assert(handoff.step() == Handoff::Step::Rollback && handoff.worker() == 0);
    reply.mode = Mode::Active; reply.state = State::Ready; reply.generation = 9;
    assert(!handoff.accept(0, reply));
    reply.generation = 7; assert(handoff.accept(0, reply));
    assert(handoff.begin(-1, 1, 0, 9)); handoff.fail(); assert(handoff.step() == Handoff::Step::Idle);
    assert(handoff.begin(0, 1, 7, 9)); handoff.fail(); assert(handoff.step() == Handoff::Step::Idle);
    assert(!handoff.begin(0, 0, 7, 7));
    assert(!handoff.begin(0, 1, 7, 9, Mode::Active));
    assert(!handoff.begin(0, 1, 7, 9, static_cast<Mode>(99)));
    assert(handoff.begin(0, 1, 7, 9, Mode::Economy));
    reply = original; reply.mode = Mode::Warm;
    assert(!handoff.accept(0, reply)); // Chosen standby policy must be acknowledged.
    reply.mode = Mode::Economy; reply.flags = EconomyTrimmed;
    assert(encode(reply, frame) && decode(frame, output) && output.mode == Mode::Economy);
    assert(handoff.accept(0, reply));
    handoff.fail();
    assert(handoff.step() == Handoff::Step::Rollback && handoff.mode() == Mode::Active);
    reply.mode = Mode::Active; reply.flags = 0;
    assert(handoff.accept(0, reply));
    invalid = original; invalid.mode = static_cast<Mode>(3); assert(!encode(invalid, frame));

    auto candidate = original, other = original;
    candidate.account.clear();
    assert(loginCollision(candidate, other, candidate.name)); // Pending reservation, before authentication.
    other.grid = "another-grid"; assert(!loginCollision(candidate, other, candidate.name));
    other = original; candidate.name = "another name"; assert(!loginCollision(candidate, other, original.name));
    candidate.account = original.account; assert(loginCollision(candidate, other, original.name));
    other.state = State::Disconnected; assert(!loginCollision(candidate, other, {}));
    // Native credential userID uses first_last for agents (including Resident),
    // or account_name for account credentials. The same grid reserves all forms
    // while connecting; a disconnected/released reservation must be reusable.
    for (const std::string name : {"alex_resident", "alex_smith", "alex"})
    {
        candidate = original; candidate.name = name; candidate.account.clear();
        other = candidate; other.state = State::Connecting;
        assert(loginCollision(candidate, other, name));
        other.grid = "another-grid"; assert(!loginCollision(candidate, other, name));
        other.grid = candidate.grid; other.state = State::Disconnected;
        assert(!loginCollision(candidate, other, {}));
    }
    std::cout << "Session wire bounds/identity, serial handoff/rollback and login reservations passed.\n";
}
