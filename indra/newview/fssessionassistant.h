/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_ASSISTANT_H
#define FS_SESSION_ASSISTANT_H
#include "fssessionchatmodel.h"
namespace fs_session
{
constexpr const char* AssistantAttentionId="00000000-0000-4000-8000-000000000016";
enum AssistantFlag : unsigned int {AssistantEnabled=1,AssistantReady=2,AssistantPaused=4,AssistantSessionOnly=8};
inline bool assistantInfoOwned(const Message& info,const Message& source)
{return source.state==State::Ready && info.state==State::Ready && !(info.flags&Error) && ChatKey{source.worker,source.generation,source.account,source.grid,{}}.owns(info);}
inline Message assistantAttention(const Message& info)
{
    auto item=info;item.eventType=EventType::Attention;item.topic=Topic::Notice;item.conversation=AssistantAttentionId;
    item.title="Local Assistant requests ("+std::to_string(info.unread)+")";
    if(info.title.rfind("Local Assistant: ",0)==0)item.title+=" · "+info.title.substr(17,100);
    return item;
}
inline bool assistantPauseNeeded(bool requested,bool acknowledged,bool failed,std::uint64_t generation)
{return generation!=0 && requested!=acknowledged && !failed;}
struct AssistantPreferenceDraft
{
    Message original;bool known=false,enabled=false;
    bool capture(const Message& info,const Message& source)
    {if(known || !assistantInfoOwned(info,source))return false;original=info;enabled=info.height!=0;known=true;return true;}
    bool edited() const {return known && enabled!=(original.height!=0);}
    bool applicable(const Message& source) const
    {return !edited() || (source.state==State::Ready && assistantInfoOwned(original,source) && !(original.width&AssistantSessionOnly));}
    Message request() const {auto value=original;value.kind=Kind::AssistantConfigure;value.width=enabled?1u:0u;return value;}
};
}
#endif
