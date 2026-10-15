/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_SESSION_H
#define FS_ASSISTANT_SESSION_H
#include <string>
#include <vector>
#include <algorithm>
namespace fs_assistant
{
inline std::string discoveryScope(const std::vector<std::string>& roots,char delimiter)
{
    if(roots.size()>16)return {};
    std::vector<std::string> seen;std::string result;
    for(const auto& root:roots)
    {
        if(root.empty() || std::find(seen.begin(),seen.end(),root)!=seen.end())continue;
        if(root.find(delimiter)!=std::string::npos || root.size()>32768)return {};
        if(!result.empty())result+=delimiter;
        result+=root;seen.push_back(root);
    }
    return result.size()<=32768?result:std::string{};
}
inline bool sessionMatches(bool managed,const std::string& current,const std::string& expected,bool supplied)
{
    return (!managed && !supplied) || (!current.empty() && !expected.empty() && current==expected);
}
inline bool needsActiveCharacter(const std::string& api,const std::string& op)
{
    return (api=="LLCamera" && op!="get") || (api=="LLInventory" && op=="snapshotUpload") ||
        (api=="LLWorkspace" && (op=="apply" || op=="previous"));
}
inline bool managedOperationSupported(const std::string& api,const std::string& op)
{
    // These native helpers schedule further copy/wear/default-permission/UI
    // callbacks without an execution context. Do not dispatch them in a
    // managed viewer until the whole native chain can honor revocation.
    return !(api=="LLAppearance" && (op=="wearOutfit" || op=="wearItems" || op=="detachItems")) &&
        !(api=="LLInventory" && op=="createItem");
}
}
#endif
