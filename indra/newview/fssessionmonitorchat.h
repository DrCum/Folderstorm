/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_MONITOR_CHAT_H
#define FS_SESSION_MONITOR_CHAT_H
#include "fssessionmonitormodel.h"
#include <optional>

namespace fs_session
{
// Every composer uses the same session-qualified conversation draft. A monitor
// owns only its selection; it never transfers drafts when its image swaps.
inline bool chatRequestAllowed(const ChatView& view, const Message& source, const Message& request)
{
    if (!monitorSource(source) || !view.identity.owns(source) || !view.identity.owns(request) ||
        (source.flags & ChatRestricted)) return false;
    const auto found = view.conversations.find(request.conversation);
    if (found == view.conversations.end() || found->second.restricted || found->second.topic != request.topic) return false;
    if (request.kind == Kind::MarkRead) return true;
    return (request.kind == Kind::SendChat || request.kind == Kind::Typing) && found->second.topic != Topic::Notice;
}
inline bool acknowledgeChatDraft(ChatView& view, const Message& request)
{
    if (request.kind != Kind::SendChat || !view.identity.owns(request)) return false;
    const auto found = view.conversations.find(request.conversation);
    if (found == view.conversations.end() || found->second.topic != request.topic || found->second.draft != request.text) return false;
    found->second.draft.clear(); return true;
}
class MonitorChat
{
public:
    ChatKey identity;
    bool bound = false;
    bool owns(const MonitorBinding& binding, const Message& source, const ChatView& view) const
    {
        return bound && binding.enabled && monitorSource(source) && binding.key.owns(source) &&
            identity.owns(source) && view.identity.owns(source);
    }
    bool bind(const MonitorBinding& binding, const Message* source, const ChatView* view)
    {
        if (!binding.enabled || !source || !view || !monitorSource(*source) ||
            !binding.key.owns(*source) || !view->identity.owns(*source))
        { const bool changed = bound; bound = false; identity = {}; return changed; }
        if (bound && identity.owns(*source)) return false;
        identity = monitorIdentity(*source); bound = true; return true;
    }
    bool select(const MonitorBinding& binding, const Message& source, const ChatView& view, const std::string& conversation)
    {
        if (!owns(binding,source,view) || !view.conversations.count(conversation)) return false;
        identity.conversation = conversation; return true;
    }
    std::optional<Message> request(Kind kind, const MonitorBinding& binding, const Message& source, const ChatView& view) const
    {
        if (!owns(binding,source,view)) return std::nullopt;
        const auto found = view.conversations.find(identity.conversation);
        if (found == view.conversations.end()) return std::nullopt;
        Message result; result.worker = identity.worker; result.generation = identity.generation;
        result.account = identity.account; result.grid = identity.grid; result.conversation = identity.conversation;
        result.kind = kind; result.topic = found->second.topic;
        if (!chatRequestAllowed(view,source,result)) return std::nullopt;
        return result;
    }
    bool draft(const MonitorBinding& binding, const Message& source, ChatView& view, std::string text) const
    {
        // Editors cap UTF-16 characters; a multibyte draft may exceed the send
        // limit. Retain it so the user can shorten it, rather than losing text.
        if (!request(Kind::SendChat,binding,source,view) || text.size() > 4092 || !validUtf8(text,true)) return false;
        view.conversations.at(identity.conversation).draft = std::move(text); return true;
    }
};
}
#endif
