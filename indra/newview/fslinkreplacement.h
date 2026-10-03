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
 * @file fslinkreplacement.h
 * @brief Callback-driven inventory link replacement, independently testable.
 */
#ifndef FS_LINK_REPLACEMENT_H
#define FS_LINK_REPLACEMENT_H

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct FSLinkReplacementCandidate
{
    std::string id, parent_id, source_id;
    std::string name, path, parent_name, parent_path, description, skip_error;
    int link_kind = 0;
};

struct FSLinkReplacementSelection
{
    std::string source_id, target_id, resolved_target_id, trash_id, session_id;
    std::string source_name, target_name, source_path, target_path;
    int target_kind = 0;
    bool no_op = false;
    std::vector<FSLinkReplacementCandidate> links;
};

namespace FSLinkReplacement
{
struct Result
{
    FSLinkReplacementCandidate candidate;
    std::string new_id, status, error;
    bool ok = false;
    // Describes this operation's actions, not unrelated inventory activity.
    bool original_preserved = true;
    bool retry_safe = true;
};

// Uncertain creation requests remain reserved for this login session. Do not
// expire them and allow a retry to create a second replacement accidentally.
class Reservations
{
public:
    struct Entry
    {
        std::string owner, new_id;
        bool callback_received = false;
    };
    void setSession(const std::string& session);
    bool acquire(const std::string& session, const std::string& id,
                 const std::string& owner, std::string& new_id);
    void created(const std::string& session, const std::string& id,
                 const std::string& owner, const std::string& new_id);
    void release(const std::string& session, const std::string& id,
                 const std::string& owner);
    static constexpr std::size_t MAX_ENTRIES = 4096;
private:
    std::string mSession;
    std::map<std::string, Entry> mEntries;
};

enum class Verification { Waiting, Invalid, Valid };
struct Backend
{
    std::function<double()> now;
    std::function<bool()> allowed;
    // Returns an explanation if original, target, parent, or locks changed.
    std::function<std::string(const FSLinkReplacementCandidate&)> validate;
    std::function<void(const FSLinkReplacementCandidate&,
                       std::function<void(const std::string&)>)> create;
    std::function<Verification(const FSLinkReplacementCandidate&,
                               const std::string&)> verify;
    std::function<void(const std::string&)> fetch;
    // True means the viewer accepted submission; it is not server acknowledgment.
    std::function<bool(const FSLinkReplacementCandidate&)> submitTrash;
};

class Operation : public std::enable_shared_from_this<Operation>
{
public:
    using Completion = std::function<void(const std::vector<Result>&)>;
    Operation(std::shared_ptr<const FSLinkReplacementSelection> selection,
              std::shared_ptr<Reservations> reservations, std::string id,
              Backend backend, double deadline, Completion completion);
    // Run on the viewer main thread. Returns true once terminal.
    bool tick();
    static constexpr std::size_t MAX_ACTIVE = 4;
private:
    enum class Phase { Queued, Creating, Verifying, Done };
    void onCreated(std::size_t index, const std::string& id);
    void verify(std::size_t index);
    void finishItem(std::size_t index, const std::string& status,
                    const std::string& error = std::string());
    bool stopped() const;
    void stop();
    void finish();

    std::shared_ptr<const FSLinkReplacementSelection> mSelection;
    std::shared_ptr<Reservations> mReservations;
    std::string mId;
    Backend mBackend;
    double mDeadline;
    Completion mCompletion;
    std::vector<Result> mResults;
    std::vector<Phase> mPhases;
    std::size_t mActive = 0;
    bool mDone = false;
};
}
#endif
