/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_AVATAR_PICKER_SEARCH_LEASE_H
#define FS_AVATAR_PICKER_SEARCH_LEASE_H
#include <memory>
namespace fs_assistant_social
{
// Viewer-main-thread transport admission, shared by MCP, host Contacts and the
// native picker. Only the actual HTTP coroutine owns this lease; discarding a
// result, closing a picker, revocation or relogin cannot release it early.
class AvatarPickerSearchLease
{
public:
    using Ptr=std::shared_ptr<AvatarPickerSearchLease>;
    static Ptr acquire()
    {
        static std::weak_ptr<AvatarPickerSearchLease> active;
        if(!active.expired())return {};
        Ptr lease(new AvatarPickerSearchLease);active=lease;return lease;
    }
private:
    AvatarPickerSearchLease()=default;
};
}
#endif
