/**
 * @file llinventorycopypolicy.h
 * @brief Pure policy decisions for inventory copies containing no-copy items.
 */

#ifndef LL_INVENTORY_COPY_POLICY_H
#define LL_INVENTORY_COPY_POLICY_H

#include <string>

namespace LLInventoryCopyPolicy
{
enum class Policy
{
    INCLUDE_NO_COPY,
    STRICT,
    COPYABLE_ONLY,
    INVALID
};

enum class Action
{
    COPY,
    COPY_AND_CONFIRM_MOVES,
    REJECT
};

inline Policy parse(const std::string& value)
{
    if (value.empty() || value == "default" || value == "include_no_copy")
        return Policy::INCLUDE_NO_COPY;
    if (value == "strict") return Policy::STRICT;
    if (value == "copyable_only") return Policy::COPYABLE_ONLY;
    return Policy::INVALID;
}

inline Action decide(Policy policy, bool has_no_copy)
{
    if (policy == Policy::INVALID) return Action::REJECT;
    if (!has_no_copy) return Action::COPY;
    if (policy == Policy::STRICT) return Action::REJECT;
    if (policy == Policy::COPYABLE_ONLY) return Action::COPY;
    return Action::COPY_AND_CONFIRM_MOVES;
}
}

#endif
