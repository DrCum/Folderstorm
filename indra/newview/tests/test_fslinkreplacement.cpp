/** Standalone tests of the real callback-driven link replacement executor. */
#include "../fslinkreplacement.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace FSLinkReplacement;
namespace
{
void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Fixture
{
    double now = 1.0;
    bool allowed = true;
    bool accepts_trash = true;
    int replies = 0, fetches = 0;
    std::vector<std::string> created, trashed;
    std::map<std::string, std::function<void(const std::string&)>> callbacks;
    std::map<std::string, Verification> visibility;
    std::map<std::string, std::string> validation;
    std::vector<Result> results;
    std::shared_ptr<Reservations> reservations = std::make_shared<Reservations>();
    std::shared_ptr<FSLinkReplacementSelection> selection = std::make_shared<FSLinkReplacementSelection>();

    explicit Fixture(int count)
    {
        selection->session_id = "login";
        reservations->setSession("login");
        for (int i = 0; i < count; ++i)
        {
            FSLinkReplacementCandidate candidate;
            candidate.id = "old" + std::to_string(i);
            candidate.parent_id = "folder";
            candidate.source_id = "source";
            selection->links.push_back(candidate);
        }
    }

    std::shared_ptr<Operation> operation(const std::string& id = "operation", bool immediate = false)
    {
        Backend backend;
        backend.now = [this]() { return now; };
        backend.allowed = [this]() { return allowed; };
        backend.validate = [this](const FSLinkReplacementCandidate& candidate)
        { return validation[candidate.id]; };
        backend.create = [this, immediate](const FSLinkReplacementCandidate& candidate,
                                          std::function<void(const std::string&)> callback)
        {
            created.push_back(candidate.id);
            callbacks[candidate.id] = callback;
            if (immediate)
            {
                const std::string id = "new-" + candidate.id;
                visibility[id] = Verification::Valid;
                callback(id);
            }
        };
        backend.verify = [this](const FSLinkReplacementCandidate&, const std::string& id)
        {
            auto found = visibility.find(id);
            return found == visibility.end() ? Verification::Waiting : found->second;
        };
        backend.fetch = [this](const std::string&) { ++fetches; };
        backend.submitTrash = [this](const FSLinkReplacementCandidate& candidate)
        {
            trashed.push_back(candidate.id);
            return accepts_trash;
        };
        return std::make_shared<Operation>(selection, reservations, id, std::move(backend), 10.0,
            [this](const std::vector<Result>& outcome) { ++replies; results = outcome; });
    }

    void complete(const std::string& old_id, const std::string& new_id, Verification state = Verification::Valid)
    {
        if (!new_id.empty()) visibility[new_id] = state;
        callbacks.at(old_id)(new_id);
    }
};

void callbackOrdering()
{
    Fixture f(2);
    auto op = f.operation();
    expect(!op->tick(), "creation should await callbacks");
    expect(f.trashed.empty() && f.replies == 0, "originals must survive pending creation");
    f.complete("old1", "new1");
    f.complete("old0", "new0");
    f.complete("old0", "duplicate0");
    expect(op->tick(), "complete operation should finish");
    expect(f.replies == 1 && f.trashed.size() == 2, "duplicate callback must not repeat trash/reply");
    expect(f.results[0].candidate.id == "old0" && f.results[0].new_id == "new0", "results follow selection, not callback order");
    expect(f.results[0].ok && !f.results[0].original_preserved, "successful row must describe submitted trash");
    op->tick();
    expect(f.replies == 1, "terminal tick must not send another reply");
}

void missingAndWrongReplacements()
{
    Fixture f(3);
    auto op = f.operation();
    op->tick();
    f.complete("old0", "");
    f.complete("old1", "wrong-target", Verification::Invalid);
    f.complete("old2", "not-loaded", Verification::Waiting);
    expect(f.trashed.empty(), "null, wrong, and unavailable replacements must preserve originals");
    expect(f.fetches == 1, "unavailable UUID should be fetched once");
    f.now = 11.0;
    op->tick();
    expect(f.results[0].status == "creation_unconfirmed" && !f.results[0].retry_safe, "UUID-only null callback cannot prove that the server created nothing");
    expect(f.results[1].status == "creation_unconfirmed" && !f.results[1].retry_safe, "wrong UUID needs recovery");
    expect(f.results[2].status == "creation_unconfirmed" && f.results[2].new_id == "not-loaded", "timed-out lookup retains UUID");
    f.now = 1.0;
    auto retry = f.operation("retry-unconfirmed");
    retry->tick();
    expect(f.created.size() == 3 && f.results[0].status == "recovery_required", "null/invalid/unresolved callbacks must not permit duplicate creation on retry");
}

void delayedVisibility()
{
    Fixture f(1);
    auto op = f.operation();
    op->tick();
    f.complete("old0", "new0", Verification::Waiting);
    expect(f.trashed.empty(), "callback UUID alone is not verification");
    f.visibility["new0"] = Verification::Valid;
    op->tick();
    expect(f.trashed.size() == 1 && f.results[0].ok, "verified later lookup permits trash");
}

void concurrentAndUncertainAttempts()
{
    Fixture f(1);
    auto first = f.operation("first");
    first->tick();
    auto duplicate = f.operation("duplicate");
    expect(duplicate->tick(), "concurrent attempt should finish with recovery result");
    expect(f.created.size() == 1 && f.results[0].status == "recovery_required", "same original must not be created twice");
    f.now = 11.0;
    first->tick();
    expect(f.results[0].status == "creation_unconfirmed", "missing callback is uncertain");
    first.reset();
    f.complete("old0", "late-new");
    expect(f.trashed.empty(), "late callback after destruction must not trash original");
    f.now = 1.0;
    auto retry = f.operation("retry");
    retry->tick();
    expect(f.created.size() == 1 && f.results[0].new_id == "late-new", "uncertain attempt reservation records late UUID for recovery");
}

void cancellationAndSessions()
{
    Fixture f(1);
    auto op = f.operation();
    op->tick();
    f.allowed = false;
    f.complete("old0", "new0");
    expect(f.trashed.empty() && f.replies == 1, "revocation between create and trash must stop follow-up");
    f.allowed = true;
    f.reservations->setSession("another-login");
    f.complete("old0", "stale-callback");
    std::string known;
    expect(f.reservations->acquire("another-login", "old0", "new-owner", known), "prior login callbacks must not reserve a new login's items");

    Fixture expired(2);
    expired.now = 11.0;
    auto no_budget = expired.operation();
    no_budget->tick();
    expect(expired.created.empty() && expired.results[0].status == "not_started", "expired approval budget cannot start creation");
}

void originalChangesAndSilentMoveRefusal()
{
    Fixture changed(1);
    auto op = changed.operation();
    op->tick();
    changed.validation["old0"] = "Original changed or became protected";
    changed.complete("old0", "new0");
    op->tick();
    expect(changed.trashed.empty() && changed.results[0].new_id == "new0", "changed/protected original survives and new UUID is retained");

    Fixture refused(1);
    refused.accepts_trash = false;
    auto rejected = refused.operation();
    rejected->tick();
    refused.complete("old0", "new0");
    rejected->tick();
    expect(!refused.results[0].ok && refused.results[0].status == "trash_not_submitted", "silent move refusal must not report success");
}

void boundedQueueAndPartialResults()
{
    Fixture f(7);
    f.selection->links[1].skip_error = "Protected folder";
    auto op = f.operation();
    op->tick();
    expect(f.created.size() == Operation::MAX_ACTIVE, "creation concurrency must be bounded");
    f.complete("old0", "new0");
    f.complete("old2", "");
    op->tick();
    expect(f.created.size() == 6, "queue replenishes only freed slots");
    for (const auto& id : f.created)
        if (id != "old0" && id != "old2") f.complete(id, "new-" + id);
    op->tick();
    expect(f.results.size() == 7 && f.results[1].status == "skipped", "partial results retain every selected row");
    expect(!f.results[2].ok && f.results[0].ok, "mixed callback outcomes are reported independently");

    Fixture empty(0);
    auto zero = empty.operation();
    expect(zero->tick() && empty.replies == 1 && empty.results.empty(), "zero matches finish once without mutations");
    Fixture synchronous(8);
    auto immediate = synchronous.operation("immediate", true);
    expect(immediate->tick() && synchronous.trashed.size() == 8 && synchronous.replies == 1, "synchronous callbacks must not break accounting");
}
}

int main()
{
    try
    {
        callbackOrdering();
        missingAndWrongReplacements();
        delayedVisibility();
        concurrentAndUncertainAttempts();
        cancellationAndSessions();
        originalChangesAndSilentMoveRefusal();
        boundedQueueAndPartialResults();
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Link replacement async tests passed\n";
    return EXIT_SUCCESS;
}
