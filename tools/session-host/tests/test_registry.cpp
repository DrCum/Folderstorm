/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssessionregistry.h"
#include <cassert>
#include <iostream>
using namespace fs_session;
int main()
{
    assert(!validSlot(-1) && !validSlot(MaxCharacters) && validSlot(MaxCharacters - 1));
    std::array<Message, MaxCharacters> sessions{};
    std::array<std::string, MaxCharacters> reservations{};
    for (int i = 0; i < MaxCharacters; ++i)
    {
        sessions[i].generation = static_cast<std::uint64_t>(i + 1);
        sessions[i].worker[0] = static_cast<std::uint8_t>(i + 1);
        sessions[i].kind = Kind::Status; sessions[i].state = State::Ready;
        sessions[i].grid = "secondlife"; sessions[i].account = "account" + std::to_string(i);
        sessions[i].name = reservations[i] = "user" + std::to_string(i);
    }
    auto peers = [&](int i) { return LoginPeer{&sessions[i], &reservations[i], true}; };
    for (int a = 0; a < MaxCharacters; ++a) for (int b = 0; b < MaxCharacters; ++b) if (a != b)
    {
        auto candidate = sessions[a]; candidate.name = sessions[b].name; candidate.account.clear();
        assert(loginCollisionAny(candidate, a, peers));
        candidate.name = "different"; candidate.account = sessions[b].account;
        assert(loginCollisionAny(candidate, a, peers));
        candidate.grid = "other"; assert(!loginCollisionAny(candidate, a, peers));
        assert(!loginCollisionAny(sessions[a], a, peers)); // Exclude self.
        Handoff h;
        auto modes = warmModes(); modes[a] = Mode::Active;
        assert(h.begin(a, b, sessions[a].generation, sessions[b].generation, Mode::Economy));
        assert(!h.begin(a, (b + 1) % MaxCharacters, sessions[a].generation, sessions[b].generation));
        auto reply = sessions[a]; reply.mode = Mode::Economy;
        assert(!h.accept(b, reply)); assert(h.accept(a, reply)); modes[a] = Mode::Economy;
        for (const auto mode : modes) assert(mode != Mode::Active);
        reply = sessions[b]; reply.mode = Mode::Active; ++reply.generation;
        assert(!h.accept(b, reply)); --reply.generation;
        assert(h.accept(b, reply)); modes[b] = Mode::Active;
        int owners = 0; for (const auto mode : modes) if (mode == Mode::Active) ++owners;
        assert(owners == 1 && h.step() == Handoff::Step::Idle);
        assert(h.begin(b, a, sessions[b].generation, sessions[a].generation));
        reply = sessions[b]; reply.mode = Mode::Warm; assert(h.accept(b, reply)); h.fail();
        assert(h.step() == Handoff::Step::Rollback && h.worker() == b);
        reply.mode = Mode::Active; ++reply.generation; assert(!h.accept(b, reply));
        --reply.generation; assert(h.accept(b, reply));
    }
    PeriodicPoller poller;
    // Permanently due Events cannot suppress the other due services.
    assert(poller.next(true, false, 30000, 0, 0, 0, 0) == Kind::Events);
    assert(poller.next(true, false, 30000, 0, 0, 0, 0) == Kind::Conversations);
    assert(poller.next(true, false, 30000, 0, 0, 0, 0) == Kind::WorkspaceInfo);
    assert(poller.next(true, false, 30000, 0, 0, 0, 0) == Kind::Poll);
    assert(poller.next(true, true, 30000, 0, 0, 0, 0) == Kind::WorkspaceInfo);
    assert(poller.next(false, false, 30000, 0, 0, 0, 0) == Kind::Poll);
    assert(poller.next(true, false, 30000, 30000, 30000, 30000, 30000) == Kind::Status);
    assert(poller.next(true, false, 100, 101, 101, 101, 101) == Kind::Status); // Clock underflow cannot flood.
    std::cout << MaxCharacters << "-slot all-pairs ownership/rollback, login reservations and fair periodic service passed.\n";
}
