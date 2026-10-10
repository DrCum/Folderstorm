/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_ATTENTION_NAV_H
#define FS_SESSION_ATTENTION_NAV_H
#include "fssessionusability.h"
namespace fs_session
{
inline unsigned int attentionCategoryMask(const Message& m)
{
    if(m.eventType!=EventType::Attention)return m.topic==Topic::Nearby?2u:1u;
    if(m.title=="Teleport offer")return 4u;
    if(m.title=="Permission request")return 8u;
    if(m.title=="Inventory offer")return 16u;
    return 0;
}
class AttentionNavigator
{
public:
    Message current;
    std::vector<Message> rows;
    void update(std::vector<Message> incoming,bool oldestFirst)
    {
        std::stable_sort(incoming.begin(),incoming.end(),[oldestFirst](const Message& a,const Message& b)
        {
            const auto rank=[](const Message& m){return m.eventType==EventType::Attention?0:1;};
            if(!oldestFirst && rank(a)!=rank(b))return rank(a)<rank(b);
            return std::tie(a.eventAt,a.account,a.grid,a.conversation,a.eventType)<std::tie(b.eventAt,b.account,b.grid,b.conversation,b.eventType);
        });
        rows.clear();for(const auto& m:incoming)if(std::none_of(rows.begin(),rows.end(),[&m](const Message& n){return AttentionBook::same(m,n);}))rows.push_back(m);
    }
    const Message* step(int direction)
    {
        if(rows.empty())return nullptr;
        auto found=std::find_if(rows.begin(),rows.end(),[this](const Message& m){return AttentionBook::same(m,current);});
        const auto n=static_cast<int>(rows.size());const int index=found==rows.end()?(direction>0?0:n-1):(static_cast<int>(found-rows.begin())+direction+n)%n;
        current=rows[static_cast<std::size_t>(index)];return &current;
    }
};
}
#endif
