/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_FRIENDS_PRESENTATION_H
#define FS_SESSION_FRIENDS_PRESENTATION_H
#include "fssessioncontacts.h"
#include <cstddef>

namespace fs_session
{
enum class FriendsRowKind { Resident, OnlineHeading, OfflineHeading };
struct FriendsRow
{
    FriendsRowKind kind=FriendsRowKind::Resident;
    std::size_t contact=0,count=0;
};
struct FriendsList
{
    std::vector<Contact> contacts;
    std::vector<FriendsRow> rows;
    const Contact* at(std::size_t row) const
    {
        if(row>=rows.size() || rows[row].kind!=FriendsRowKind::Resident || rows[row].contact>=contacts.size())return nullptr;
        return &contacts[rows[row].contact];
    }
    int find(const std::string& id) const
    {
        for(std::size_t i=0;i<rows.size();++i)if(const auto* c=at(i))if(c->id==id)return static_cast<int>(i);
        return -1;
    }
    int selectable(int start,int direction) const
    {
        if(direction!=1 && direction!=-1)return -1;
        for(int i=start;i>=0 && static_cast<std::size_t>(i)<rows.size();i+=direction)
            if(at(static_cast<std::size_t>(i)))return i;
        return -1;
    }
};
inline FriendsList presentFriends(std::vector<Contact> contacts,const std::string& filter={},bool onlineOnly=false,bool search=false)
{
    FriendsList result;
    result.contacts=sortedContacts(std::move(contacts),filter);
    if(!search && onlineOnly)
        result.contacts.erase(std::remove_if(result.contacts.begin(),result.contacts.end(),[](const Contact& c){return !c.online;}),result.contacts.end());
    const auto online=static_cast<std::size_t>(std::count_if(result.contacts.begin(),result.contacts.end(),[](const Contact& c){return c.online;}));
    for(std::size_t i=0;i<result.contacts.size();++i)
    {
        if(!search && (i==0 || i==online))
            result.rows.push_back({result.contacts[i].online?FriendsRowKind::OnlineHeading:FriendsRowKind::OfflineHeading,0,result.contacts[i].online?online:result.contacts.size()-online});
        result.rows.push_back({FriendsRowKind::Resident,i,0});
    }
    return result;
}
}
#endif
