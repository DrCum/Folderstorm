/**
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Copyright (c) 2026 Folderstorm contributors.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 * $/LicenseInfo$
 */

/**
 * @file fslinkreplacement.cpp
 */
#include "fslinkreplacement.h"

#include <algorithm>
#include <utility>

namespace FSLinkReplacement
{
void Reservations::setSession(const std::string& session)
{
    if (session != mSession)
    {
        mSession = session;
        mEntries.clear();
    }
}

bool Reservations::acquire(const std::string& session, const std::string& id,
                           const std::string& owner, std::string& new_id)
{
    if (session != mSession) return false;
    auto found = mEntries.find(id);
    if (found != mEntries.end())
    {
        new_id = found->second.new_id;
        return false;
    }
    if (mEntries.size() >= MAX_ENTRIES) return false;
    mEntries.emplace(id, Entry{owner, std::string(), false});
    return true;
}

void Reservations::created(const std::string& session, const std::string& id,
                           const std::string& owner, const std::string& new_id)
{
    if (session != mSession) return;
    auto found = mEntries.find(id);
    if (found == mEntries.end() || found->second.owner != owner ||
        found->second.callback_received) return;
    // The UUID-only AIS/UDP interface cannot distinguish a definite refusal
    // from a lost response after the server accepted creation. A null callback
    // must not release the reservation and make an unsafe retry possible.
    found->second.callback_received = true;
    found->second.new_id = new_id;
}

void Reservations::release(const std::string& session, const std::string& id,
                           const std::string& owner)
{
    if (session != mSession) return;
    auto found = mEntries.find(id);
    if (found != mEntries.end() && found->second.owner == owner) mEntries.erase(found);
}

Operation::Operation(std::shared_ptr<const FSLinkReplacementSelection> selection,
                     std::shared_ptr<Reservations> reservations, std::string id,
                     Backend backend, double deadline, Completion completion)
    : mSelection(std::move(selection)), mReservations(std::move(reservations)),
      mId(std::move(id)), mBackend(std::move(backend)), mDeadline(deadline),
      mCompletion(std::move(completion))
{
    for (const auto& candidate : mSelection->links)
    {
        Result result;
        result.candidate = candidate;
        if (!candidate.skip_error.empty())
        {
            result.status = "skipped";
            result.error = candidate.skip_error;
        }
        mResults.push_back(std::move(result));
        mPhases.push_back(candidate.skip_error.empty() ? Phase::Queued : Phase::Done);
    }
}

bool Operation::stopped() const
{
    return mBackend.now() >= mDeadline || !mBackend.allowed();
}

void Operation::finishItem(std::size_t index, const std::string& status,
                           const std::string& error)
{
    if (mPhases[index] == Phase::Done) return;
    if (mPhases[index] != Phase::Queued) --mActive;
    mPhases[index] = Phase::Done;
    mResults[index].status = status;
    mResults[index].error = error;
}

void Operation::stop()
{
    const bool timed_out = mBackend.now() >= mDeadline;
    for (std::size_t i = 0; i < mResults.size(); ++i)
    {
        if (mPhases[i] == Phase::Done) continue;
        const bool started = mPhases[i] != Phase::Queued;
        mResults[i].retry_safe = !started;
        finishItem(i, started ? "creation_unconfirmed" : "not_started",
                   timed_out ? "Replacement deadline expired; original was not moved by this operation"
                             : "Session or permission changed; original was not moved by this operation");
    }
    finish();
}

void Operation::finish()
{
    if (mDone) return;
    mDone = true;
    auto completion = std::move(mCompletion);
    completion(mResults);
}

bool Operation::tick()
{
    if (mDone) return true;
    if (stopped())
    {
        stop();
        return true;
    }
    for (std::size_t i = 0; i < mResults.size(); ++i)
    {
        if (mPhases[i] == Phase::Verifying) verify(i);
        if (mDone) return true;
    }
    for (std::size_t i = 0; i < mResults.size() && mActive < MAX_ACTIVE; ++i)
    {
        if (mPhases[i] != Phase::Queued) continue;
        if (stopped()) { stop(); return true; }
        const auto& candidate = mResults[i].candidate;
        const std::string error = mBackend.validate(candidate);
        if (!error.empty())
        {
            finishItem(i, "original_changed", error);
            continue;
        }
        if (!mReservations->acquire(mSelection->session_id, candidate.id,
                                    mId, mResults[i].new_id))
        {
            mResults[i].retry_safe = false;
            finishItem(i, "recovery_required", "A replacement for this link is pending or needs inspection before retrying");
            continue;
        }
        ++mActive;
        mPhases[i] = Phase::Creating;
        mResults[i].retry_safe = false;
        std::weak_ptr<Operation> weak = shared_from_this();
        const auto reservations = mReservations;
        const auto session = mSelection->session_id;
        const auto id = candidate.id;
        const auto owner = mId;
        mBackend.create(candidate, [weak, reservations, session, id, owner, i](const std::string& new_id)
        {
            // Even after the operation is gone, retain a known late-created UUID
            // for recovery. Never perform a destructive follow-up here.
            reservations->created(session, id, owner, new_id);
            if (auto self = weak.lock()) self->onCreated(i, new_id);
        });
    }
    if (std::all_of(mPhases.begin(), mPhases.end(), [](Phase phase) { return phase == Phase::Done; })) finish();
    return mDone;
}

void Operation::onCreated(std::size_t index, const std::string& id)
{
    if (mDone || mPhases[index] != Phase::Creating) return;
    mResults[index].new_id = id;
    if (stopped()) { stop(); return; }
    if (id.empty())
    {
        finishItem(index, "creation_unconfirmed", "Link creation returned no new inventory UUID; inspect inventory before retrying");
        return;
    }
    mPhases[index] = Phase::Verifying;
    verify(index);
    if (mPhases[index] == Phase::Verifying) mBackend.fetch(id);
}

void Operation::verify(std::size_t index)
{
    if (stopped()) { stop(); return; }
    Result& result = mResults[index];
    const Verification verification = mBackend.verify(result.candidate, result.new_id);
    if (verification == Verification::Waiting) return;
    if (verification == Verification::Invalid)
    {
        finishItem(index, "creation_unconfirmed", "Returned inventory item is not the expected replacement link");
        return;
    }
    const std::string error = mBackend.validate(result.candidate);
    if (!error.empty())
    {
        finishItem(index, "original_changed", error);
        return;
    }
    if (stopped()) { stop(); return; }
    if (!mBackend.submitTrash(result.candidate))
    {
        finishItem(index, "trash_not_submitted", "Replacement exists, but the viewer did not accept the original's move to Trash");
        return;
    }
    result.ok = true;
    result.original_preserved = false;
    result.retry_safe = true;
    mReservations->release(mSelection->session_id, result.candidate.id, mId);
    finishItem(index, "replacement_created_trash_submitted");
}
}
