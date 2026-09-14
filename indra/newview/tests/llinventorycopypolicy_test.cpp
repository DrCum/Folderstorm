#include "linden_common.h"
#include "../test/lltut.h"

#include "../llinventorycopypolicy.h"

namespace tut
{
struct inventory_copy_policy_test {};
using inventory_copy_policy_group = test_group<inventory_copy_policy_test>;
using inventory_copy_policy_object = inventory_copy_policy_group::object;
inventory_copy_policy_group inventory_copy_policy_tests("InventoryCopyPolicy");

template<> template<>
void inventory_copy_policy_object::test<1>()
{
    using namespace LLInventoryCopyPolicy;
    ensure("default policy", parse("") == Policy::INCLUDE_NO_COPY);
    ensure("named default policy", parse("default") == Policy::INCLUDE_NO_COPY);
    ensure("strict policy", parse("strict") == Policy::STRICT);
    ensure("copyable policy", parse("copyable_only") == Policy::COPYABLE_ONLY);
    ensure("invalid policy", parse("unknown") == Policy::INVALID);
}

template<> template<>
void inventory_copy_policy_object::test<2>()
{
    using namespace LLInventoryCopyPolicy;
    ensure("ordinary copy", decide(Policy::INCLUDE_NO_COPY, false) == Action::COPY);
    ensure("strict rejects", decide(Policy::STRICT, true) == Action::REJECT);
    ensure("copyable only proceeds",
           decide(Policy::COPYABLE_ONLY, true) == Action::COPY);
    ensure("default confirms",
           decide(Policy::INCLUDE_NO_COPY, true) ==
               Action::COPY_AND_CONFIRM_MOVES);
    ensure("invalid rejects", decide(Policy::INVALID, false) == Action::REJECT);
}
}
