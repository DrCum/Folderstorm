/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CONTENT_READ_POLICY_H
#define FS_CONTENT_READ_POLICY_H
namespace fs_content {
struct ReadAuthority
{
    bool notecard=false,agentInventory=false,library=false,copy=false,modify=false;
    bool restrictionsEnabled=false,viewNoteRestricted=false,viewScriptRestricted=false;
};
inline bool nativeContentReadable(const ReadAuthority& authority)
{
    return (authority.agentInventory || authority.library) && authority.copy &&
        (authority.notecard || authority.library || authority.modify) &&
        (!authority.restrictionsEnabled || !(authority.notecard?authority.viewNoteRestricted:authority.viewScriptRestricted));
}
}
#endif
