#include "fsassistantpolicy.h"

#include <iostream>
#include <stdexcept>

namespace
{
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

// A terminal request has one response; obsolete UI callbacks share its gate.
// The bridge uses this same production gate before posting its Event API event.
struct Request
{
    fs_assistant::ApprovalGate gate;
    std::vector<std::string> levels{"ask", "allow"};
    bool enabled = true;
    bool expired = false;
    int events = 0;
    int responses = 0;
    fs_assistant::ApprovalGate::Outcome answer(bool allow)
    {
        const auto result = gate.finish(allow, levels, enabled, expired);
        if (result != fs_assistant::ApprovalGate::Outcome::AlreadyFinished) ++responses;
        if (result == fs_assistant::ApprovalGate::Outcome::Allow) ++events;
        return result;
    }
};
}

int main()
{
    using fs_assistant::Decision;
    using Outcome = fs_assistant::ApprovalGate::Outcome;
    try
    {
        check(fs_assistant::snapshot_classes("thumbnail") == std::vector<std::string>({"camera", "edit"}), "thumbnail class set");
        check(fs_assistant::snapshot_classes("texture") == std::vector<std::string>({"camera", "create", "edit"}), "texture class set");
        const std::vector<std::string> levels{"allow", "ask", "deny"};
        for (int count : {2, 3})
        {
            for (int cost : {0, 10})
            {
                int allowed = 0, asked = 0, denied = 0;
                for (const auto& a : levels)
                    for (const auto& b : levels)
                        for (const auto& c : count == 2 ? std::vector<std::string>{"allow"} : levels)
                        {
                            const std::vector<std::string> configured = count == 2 ? std::vector<std::string>{a, b} : std::vector<std::string>{a, b, c};
                            const auto decision = fs_assistant::decide(configured, cost);
                            if (decision == Decision::Allow) ++allowed;
                            if (decision == Decision::Ask) ++asked;
                            if (decision == Decision::Deny) ++denied;
                            for (int index = 0; index < count; ++index)
                            {
                                auto revoked = configured;
                                revoked[index] = "deny";
                                check(fs_assistant::decide(revoked, cost) == Decision::Deny, "each snapshot permission independently denies");
                            }
                        }
                check(denied == (count == 2 ? 5 : 19), "Deny wins regardless of cost or Ask");
                check(allowed == (cost == 0 ? 1 : 0), "only all Allow/free proceeds without approval");
                check(asked == (count == 2 ? (cost == 0 ? 3 : 4) : (cost == 0 ? 7 : 8)), "compound action needs one approval");
            }
        }
        check(fs_assistant::decide({"allow", "invalid"}) == Decision::Deny, "unknown permission fails closed");

        Request revoked;
        revoked.levels[1] = "deny";
        check(revoked.answer(false) == Outcome::Revoked, "revocation invalidates even a secondary allowed class");
        check(revoked.answer(true) == Outcome::AlreadyFinished, "obsolete approval callback suppressed");
        check(revoked.events == 0 && revoked.responses == 1, "revocation has no event and exactly one response");

        Request changed_without_signal;
        changed_without_signal.levels[0] = "deny";
        check(changed_without_signal.answer(true) == Outcome::Revoked && changed_without_signal.events == 0, "acceptance independently rechecks latest permissions");

        Request disabled;
        disabled.enabled = false;
        disabled.answer(false);
        disabled.enabled = true;
        disabled.answer(true);
        check(disabled.events == 0 && disabled.responses == 1, "re-enable cannot revive disabled request");

        Request timed_out;
        timed_out.expired = true;
        check(timed_out.answer(true) == Outcome::TimedOut, "late Yes times out even before ticker runs");
        timed_out.answer(true);
        check(timed_out.events == 0 && timed_out.responses == 1, "timeout suppresses old callbacks");

        Request accepted;
        accepted.levels[0] = "allow";
        accepted.answer(true);
        accepted.answer(true);
        check(accepted.events == 1 && accepted.responses == 1, "Ask to Allow does not duplicate acceptance");

        Request denied;
        denied.answer(false);
        denied.answer(true);
        check(denied.events == 0 && denied.responses == 1, "denied dialog cannot execute through an old Yes");

        fs_assistant::ExecutionGate execution({"links"});
        execution.observe({"allow"});
        check(execution.allowed(), "unrelated settings signal keeps required Allow operation active");
        execution.observe({"deny"});
        execution.observe({"allow"});
        int destructive_callbacks = 0;
        if (execution.allowed()) ++destructive_callbacks;
        check(destructive_callbacks == 0, "Deny then Allow between callbacks stays revoked");
        std::cout << "assistant permission and approval checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
