/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_SESSION_H
#define FS_ASSISTANT_SESSION_H
#include <string>
namespace fs_assistant
{
inline bool sessionMatches(bool managed,const std::string& current,const std::string& expected,bool supplied)
{
    return (!managed && !supplied) || (!current.empty() && !expected.empty() && current==expected);
}
inline bool needsActiveCharacter(const std::string& api,const std::string& op)
{
    return (api=="LLCamera" && op!="get") || (api=="LLInventory" && op=="snapshotUpload") ||
        (api=="LLWorkspace" && (op=="apply" || op=="previous"));
}
}
#endif
