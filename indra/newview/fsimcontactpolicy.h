/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_IM_CONTACT_POLICY_H
#define FS_IM_CONTACT_POLICY_H
namespace FSIMContactPolicy
{
// The NO_SUPPORT flag limits direct support contacts, not ordinary contacts
// or group/conference messaging. Native composers and workers share this rule.
inline bool canSend(bool direct, bool supportContact, bool noSupport)
{
    return !(direct && supportContact && noSupport);
}
}
#endif
