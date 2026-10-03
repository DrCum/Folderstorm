/** Viewer-independent permission decisions for local assistant requests. */
#ifndef FS_ASSISTANT_POLICY_H
#define FS_ASSISTANT_POLICY_H

#include <string>
#include <utility>
#include <vector>

namespace fs_assistant
{
enum class Decision { Allow, Ask, Deny };

inline std::vector<std::string> snapshot_classes(const std::string& destination)
{
    return destination == "texture" ? std::vector<std::string>{"camera", "create", "edit"}
                                    : std::vector<std::string>{"camera", "edit"};
}

// A price can require approval, but can never override a denied permission.
inline Decision decide(const std::vector<std::string>& levels, int cost = 0)
{
    bool ask = cost > 0;
    for (const std::string& level : levels)
    {
        if (level == "deny") return Decision::Deny;
        if (level == "ask") ask = true;
        if (level != "allow" && level != "ask") return Decision::Deny;
    }
    return ask ? Decision::Ask : Decision::Allow;
}

// The same terminal gate is used by the bridge's queue and its obsolete UI
// callbacks. Revocation finishes a request before the dialog is dismissed.
class ApprovalGate
{
public:
    enum class Outcome { Allow, Denied, Revoked, TimedOut, AlreadyFinished };
    Outcome finish(bool user_allowed, const std::vector<std::string>& levels,
                   bool enabled, bool expired)
    {
        if (mFinished) return Outcome::AlreadyFinished;
        mFinished = true;
        if (expired) return Outcome::TimedOut;
        if (decide(levels) == Decision::Deny) return Outcome::Revoked;
        if (!enabled || !user_allowed) return Outcome::Denied;
        return Outcome::Allow;
    }
    bool finished() const { return mFinished; }
private:
    bool mFinished = false;
};

// A dispatched asynchronous operation cannot resume after a revoked permission
// is turned back on before its next callback. State observes settings signals.
class ExecutionGate
{
public:
    explicit ExecutionGate(std::vector<std::string> classes) : mClasses(std::move(classes)) {}
    const std::vector<std::string>& classes() const { return mClasses; }
    void observe(const std::vector<std::string>& levels)
    {
        if (decide(levels) == Decision::Deny) mCancelled = true;
    }
    bool allowed() const { return !mCancelled; }
private:
    std::vector<std::string> mClasses;
    bool mCancelled = false;
};
}

#endif
