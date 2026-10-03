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

#include "fsinventorybulkmodel.h"
#include <algorithm>
#include <set>
#include <utility>

namespace FSInventoryBulkModel
{
std::string boundedLabel(std::string text, std::size_t limit)
{
    for (char& c : text) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    if (text.size() > limit)
    {
        if (limit < 3) return std::string(limit, '.');
        std::size_t cut = limit - 3;
        while (cut && (static_cast<unsigned char>(text[cut]) & 0xc0) == 0x80) --cut;
        text.resize(cut); text += "...";
    }
    return text;
}
bool Snapshot::operator==(const Snapshot& o) const
{
    return id == o.id && name == o.name && parent == o.parent && type == o.type &&
        folder == o.folder && link == o.link && copyable == o.copyable && modifiable == o.modifiable &&
        ordinary == o.ordinary && linked_id == o.linked_id && revision == o.revision && external_revision == o.external_revision;
}

void Store::session(const std::string& value)
{
    if (mSession == value) return;
    clear();
    mTargets.clear();
    mActive.clear();
    mLastWriter.clear();
    mSession = value;
}

std::size_t rowBytes(const Row& row)
{
    const auto& b = row.before;
    return sizeof(Row) + b.id.size() + b.name.size() + b.parent.size() + b.path.size() +
        b.parent_label.size() + b.linked_id.size() + row.after_name.size() +
        row.destination.size() + row.destination_label.size() + row.skip.size();
}
std::size_t Store::bytes() const
{
    std::size_t total = 0;
    auto records = mHistory;
    records.insert(mActive.begin(), mActive.end());
    for (const auto& pair : records)
    {
        total += sizeof(History);
        for (const auto& row : pair.second->rows) total += rowBytes(row);
        // Reserve the bounded final snapshot and error capacity at claim,
        // rather than admitting more work while callbacks have not filled it.
        total += pair.second->results.size() * (sizeof(Outcome) + 3072);

    }
    return total;
}
void Store::prune(double now)
{
    for (auto it = mPlans.begin(); it != mPlans.end();)
        if (it->second->expires <= now || (!it->second->operation_id.empty() && !mHistory.count(it->second->operation_id) && !mActive.count(it->second->operation_id))) it = mPlans.erase(it); else ++it;
    for (auto it = mHistory.begin(); it != mHistory.end();)
    {
        if (it->second->terminal && it->second->started + HISTORY_TTL <= now)
        {
            it->second->visible = false;
            it = mHistory.erase(it);
        }
        else ++it;
    }
    while (mHistory.size() >= MAX_HISTORY || bytes() >= MAX_BYTES)
    {
        auto oldest = mHistory.end();
        for (auto it = mHistory.begin(); it != mHistory.end(); ++it)
            if (it->second->terminal && (oldest == mHistory.end() || it->second->started < oldest->second->started)) oldest = it;
        if (oldest == mHistory.end()) break;
        oldest->second->visible = false;
        mHistory.erase(oldest);
    }
}

bool Store::prepare(std::shared_ptr<Plan> value, double now, std::string& error)
{
    prune(now);
    if (value->session.empty() || value->session != mSession) { error = "Inventory session is unavailable"; return false; }
    if (value->rows.empty() || value->rows.size() > MAX_ROWS) { error = "A plan must contain 1 to 50 requested rows"; return false; }
    std::size_t pending = 0;
    for (const auto& pair : mPlans) pending += pair.second->operation_id.empty();
    if (pending >= MAX_PLANS) { error = "Too many prepared plans; wait for expiry or clear history"; return false; }
    std::set<std::string> seen;
    for (const auto& row : value->rows)
        if (row.before.id.empty() || !seen.insert(row.before.id).second)
        { error = "Each requested target must have a unique inventory UUID"; return false; }
    value->expires = now + PLAN_TTL;
    if (value->id.empty() || mPlans.count(value->id)) { error = "Plan ID is missing or already used"; return false; }
    const std::string id = value->id;
    mPlans.emplace(id, std::move(value));
    return true;
}
std::shared_ptr<Plan> Store::plan(const std::string& id, double now)
{
    prune(now);
    auto found = mPlans.find(id);
    return found == mPlans.end() ? nullptr : found->second;
}
std::shared_ptr<History> Store::record(const std::string& id, double now)
{
    prune(now);
    auto found = mHistory.find(id);
    return found == mHistory.end() ? nullptr : found->second;
}
std::vector<std::shared_ptr<History>> Store::history(double now)
{
    prune(now);
    std::vector<std::shared_ptr<History>> output;
    for (const auto& pair : mHistory) output.push_back(pair.second);
    std::sort(output.begin(), output.end(), [](const auto& a, const auto& b) { return a->started > b->started; });
    return output;
}
std::shared_ptr<History> Store::claim(const std::string& id, const std::string& operation_id,
                                    double now, std::string& error)
{
    auto prepared = plan(id, now);
    if (!prepared) { error = "Plan is missing, expired, or belongs to another session"; return nullptr; }
    if (!prepared->operation_id.empty())
    {
        auto existing = record(prepared->operation_id, now);
        if (!existing) error = "Plan was already consumed; its history is unavailable";
        return existing;
    }
    const std::size_t active = mActive.size();
    auto retained = mHistory; retained.insert(mActive.begin(), mActive.end());
    std::size_t retained_rows = 0;
    for (const auto& pair : retained) retained_rows += pair.second->rows.size();
    std::size_t required = sizeof(History);
    for (const auto& row : prepared->rows)
    {
        required += rowBytes(row) + sizeof(Outcome) + 3072;
        if (row.skip.empty() && mTargets.count(row.before.id))
        { error = "A target already has active or unconfirmed work; inspect history before retrying"; return nullptr; }
    }
    while ((retained_rows + prepared->rows.size() > MAX_TARGETS || bytes() + required > MAX_BYTES) && active < MAX_ACTIVE)
    {
        auto oldest = mHistory.end();
        for (auto it = mHistory.begin(); it != mHistory.end(); ++it)
            if (it->second->terminal && (oldest == mHistory.end() || it->second->started < oldest->second->started)) oldest = it;
        if (oldest == mHistory.end()) break;
        retained_rows -= oldest->second->rows.size();
        oldest->second->visible = false; mHistory.erase(oldest);
    }
    if (active >= MAX_ACTIVE || retained_rows + prepared->rows.size() > MAX_TARGETS || mTargets.size() + prepared->rows.size() > MAX_TARGETS || bytes() + required > MAX_BYTES)
    { error = "Bulk inventory execution capacity is busy"; return nullptr; }
    auto entry = std::make_shared<History>();
    entry->id = operation_id;
    entry->session = mSession;
    entry->inverse_of = prepared->inverse_of;
    entry->kind = prepared->kind;
    entry->started = now;
    entry->rows = prepared->rows;
    entry->results.resize(entry->rows.size());
    for (const auto& row : entry->rows)
        if (row.skip.empty()) mTargets[row.before.id] = entry->id;
    prepared->operation_id = operation_id;
    mHistory.emplace(operation_id, entry);
    mActive.emplace(operation_id, entry);
    return entry;
}
bool Store::current(const History& entry) const { return entry.session == mSession; }
bool Store::undoAvailable(const History& entry, std::size_t index) const
{
    if (!current(entry) || !entry.visible || !entry.terminal || !entry.inverse_of.empty() || index >= entry.rows.size()) return false;
    const auto& row = entry.rows[index];
    auto writer = mLastWriter.find(row.before.id);
    return row.undo_eligible && entry.results[index].status == "confirmed" &&
        entry.results[index].after.external_revision == row.before.external_revision &&
        !mTargets.count(row.before.id) && writer != mLastWriter.end() && writer->second == entry.id;
}
bool Store::previewUndo(const std::string& operation_id, const std::vector<std::string>& ids,
                       const std::string& plan_id, double now, std::string& error)
{
    auto entry = record(operation_id, now);
    if (!entry) { error = "History record is missing or expired"; return false; }
    std::set<std::string> selected(ids.begin(), ids.end());
    const bool explicit_selection = !ids.empty();
    if (ids.size() > MAX_ROWS || selected.size() != ids.size()) { error = "Undo selection must contain at most 50 unique UUIDs"; return false; }
    auto inverse = std::make_shared<Plan>();
    inverse->id = plan_id;
    inverse->session = mSession;
    inverse->kind = entry->kind;
    inverse->inverse_of = entry->id;
    for (std::size_t i = 0; i < entry->rows.size(); ++i)
    {
        const auto& original = entry->rows[i];
        if (explicit_selection && !selected.count(original.before.id)) continue;
        if (!undoAvailable(*entry, i))
        {
            if (explicit_selection) { error = "Selected rows are not confirmed, current, and eligible for undo"; return false; }
            continue;
        }
        selected.erase(original.before.id);
        Row row;
        row.before = entry->results[i].after;
        row.after_name = original.before.name;
        row.destination = original.before.parent;
        row.destination_label = original.before.parent_label;
        inverse->rows.push_back(row);
    }
    if (!selected.empty()) { error = "Undo selection contains an unknown target"; return false; }
    if (inverse->rows.empty()) { error = "There are no confirmed eligible rows to undo"; return false; }
    return prepare(inverse, now, error);
}
void Store::submitted(History& entry, std::size_t index)
{
    entry.results[index].status = "submitted";
    entry.results[index].accepted = true;
    mLastWriter[entry.rows[index].before.id] = entry.id;
    // Evicting a writer disables an old undo; it never authorizes one.
    while (mLastWriter.size() > MAX_TARGETS) mLastWriter.erase(mLastWriter.begin());
}
void Store::settle(History& entry, std::size_t index, const Outcome& outcome)
{
    if (!current(entry)) return;
    entry.results[index] = outcome;
    const auto& id = entry.rows[index].before.id;
    // Keep uncertain submitted targets reserved until their real callback
    // reconciles them or this login session ends. Clear is not a retry bypass.
    if (outcome.status != "unconfirmed" && outcome.status != "submitted")
    {
        auto target = mTargets.find(id);
        if (target != mTargets.end() && target->second == entry.id) mTargets.erase(target);
    }
}
void Store::finish(History& entry) { entry.terminal = true; mActive.erase(entry.id); }
void Store::clear()
{
    for (auto& pair : mHistory) pair.second->visible = false;
    mHistory.clear();
    mPlans.clear();
    mLastWriter.clear();
}
void Store::stop(const std::string& id)
{
    auto found = mActive.find(id);
    if (found != mActive.end()) found->second->cancelled = true;
}

Operation::Operation(std::shared_ptr<Store> store, std::shared_ptr<History> entry,
    Backend backend, double deadline, std::function<void(const History&)> completed)
    : mStore(std::move(store)), mRecord(std::move(entry)), mBackend(std::move(backend)),
      mDeadline(deadline), mCompleted(std::move(completed)), mPhase(mRecord->rows.size(), 0) {}
void Operation::finish()
{
    if (mDone) return;
    mDone = true;
    mStore->finish(*mRecord);
    auto callback = std::move(mCompleted);
    callback(*mRecord);
}
void Operation::settle(std::size_t index, Outcome result)
{
    if (!mStore->current(*mRecord)) return;
    if (mPhase[index] == 2 && !mDone) return;
    if (mPhase[index] != 2) { --mOutstanding; mPhase[index] = 2; }
    mStore->settle(*mRecord, index, result);
}
bool Operation::tick()
{
    if (mDone) return true;
    if (!mStore->current(*mRecord) || mRecord->cancelled || !mBackend.allowed() || mBackend.now() >= mDeadline)
    {
        for (std::size_t i = 0; i < mPhase.size(); ++i)
            if (mPhase[i] != 2)
            {
                Outcome result;
                result.status = mRecord->results[i].status == "submitted" ? "unconfirmed" : "cancelled_before_submission";
                result.accepted = mRecord->results[i].accepted;
                result.error = "Execution stopped or expired; submitted work may still complete";
                mStore->settle(*mRecord, i, result);
                mPhase[i] = 2;
            }
        finish();
        return true;
    }
    for (std::size_t i = 0; i < mPhase.size() && mOutstanding < MAX_OUTSTANDING; ++i)
    {
        if (mRecord->cancelled || !mBackend.allowed() || mBackend.now() >= mDeadline) break;
        if (mPhase[i]) continue;
        const Row& row = mRecord->rows[i];
        std::string error = row.skip.empty() ? mBackend.validate(row) : row.skip;
        if (!error.empty())
        {
            Outcome result;
            result.status = "skipped"; result.error = error;
            mStore->settle(*mRecord, i, result); mPhase[i] = 2;
            continue;
        }
        mPhase[i] = 1; ++mOutstanding;
        std::weak_ptr<Operation> weak = shared_from_this();
        auto store = mStore; auto entry = mRecord;
        auto delivered = std::make_shared<bool>(false);
        mBackend.perform(row, !mRecord->inverse_of.empty(),
            [weak]()
            {
                auto self = weak.lock();
                return self && !self->mDone && self->mStore->current(*self->mRecord) &&
                    !self->mRecord->cancelled && self->mBackend.allowed() && self->mBackend.now() < self->mDeadline;
            },
            [store, entry, i]() { if (store->current(*entry)) store->submitted(*entry, i); },
            [weak, store, entry, delivered, i](Outcome result)
            {
                if (*delivered) return;
                *delivered = true;
                if (auto self = weak.lock()) self->settle(i, std::move(result));
                else if (store->current(*entry)) store->settle(*entry, i, result);
            });
    }
    if (std::all_of(mPhase.begin(), mPhase.end(), [](int phase) { return phase == 2; })) finish();
    return mDone;
}
}
