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

/** Bounded session-only inventory plans, history, and callback execution. */
#ifndef FS_INVENTORY_BULK_MODEL_H
#define FS_INVENTORY_BULK_MODEL_H
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace FSInventoryBulkModel
{
enum class Kind { Rename, Move };
// Display-only hygiene; material validation always uses the original strings.
std::string boundedLabel(std::string text, std::size_t limit = 256);
struct Snapshot
{
    std::string id, name, parent, path, parent_label, linked_id;
    int type = 0;
    std::uint64_t revision = 0;
    // Changes from sources other than this object's bulk AIS reconciliation.
    std::uint64_t external_revision = 0;
    bool folder = false, link = false, copyable = false, modifiable = false, ordinary = false;
    bool operator==(const Snapshot& other) const;
};
struct Row
{
    Snapshot before;
    std::string after_name, destination, destination_label, skip;
    int destination_type = 0;
    bool undo_eligible = false;
};
struct Plan
{
    std::string id, session, operation_id, inverse_of;
    Kind kind = Kind::Rename;
    double expires = 0;
    std::vector<Row> rows;
};
struct Outcome
{
    // A confirmed outcome is produced only by a successful authoritative
    // response plus a fetched server object matching the desired state.
    std::string status = "not_started", error;
    Snapshot after;
    bool accepted = false;
};
struct History
{
    std::string id, session, inverse_of;
    Kind kind = Kind::Rename;
    double started = 0;
    bool terminal = false, cancelled = false, visible = true;
    std::vector<Row> rows;
    std::vector<Outcome> results;
};

class Store
{
public:
    static constexpr std::size_t MAX_ROWS = 50, MAX_PLANS = 32, MAX_HISTORY = 100;
    static constexpr std::size_t MAX_BYTES = 4 * 1024 * 1024, MAX_ACTIVE = 4, MAX_TARGETS = 5000;
    static constexpr double PLAN_TTL = 600, HISTORY_TTL = 7200;
    void session(const std::string& session);
    bool prepare(std::shared_ptr<Plan> plan, double now, std::string& error);
    std::shared_ptr<Plan> plan(const std::string& id, double now);
    std::shared_ptr<History> claim(const std::string& plan_id, const std::string& operation_id,
                                  double now, std::string& error);
    std::shared_ptr<History> record(const std::string& id, double now);
    std::vector<std::shared_ptr<History>> history(double now);
    bool undoAvailable(const History& entry, std::size_t index) const;
    bool previewUndo(const std::string& operation_id, const std::vector<std::string>& ids,
                     const std::string& plan_id, double now, std::string& error);
    void submitted(History& entry, std::size_t index);
    void settle(History& entry, std::size_t index, const Outcome& outcome);
    void finish(History& entry);
    void clear();
    void stop(const std::string& operation_id);
    bool current(const History& entry) const;
private:
    void prune(double now);
    std::size_t bytes() const;
    std::string mSession;
    std::map<std::string, std::shared_ptr<Plan>> mPlans;
    std::map<std::string, std::shared_ptr<History>> mHistory, mActive;
    std::map<std::string, std::string> mTargets, mLastWriter;
};

struct Backend
{
    std::function<double()> now;
    std::function<bool()> allowed;
    std::function<std::string(const Row&)> validate;
    std::function<void(const Row&, bool inverse, std::function<bool()> alive,
                       std::function<void()> submitted, std::function<void(Outcome)>)> perform;
};

class Operation : public std::enable_shared_from_this<Operation>
{
public:
    Operation(std::shared_ptr<Store> store, std::shared_ptr<History> record,
              Backend backend, double deadline, std::function<void(const History&)> completed);
    bool tick();
    static constexpr std::size_t MAX_OUTSTANDING = 4;
private:
    void settle(std::size_t index, Outcome result);
    void finish();
    std::shared_ptr<Store> mStore;
    std::shared_ptr<History> mRecord;
    Backend mBackend;
    double mDeadline;
    std::function<void(const History&)> mCompleted;
    std::vector<int> mPhase; // 0 queued, 1 outstanding, 2 terminal
    std::size_t mOutstanding = 0;
    bool mDone = false;
};
}
#endif
