/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SESSION_CATEGORIES_H
#define FS_SESSION_CATEGORIES_H
#include "fssessionprotocol.h"
namespace fs_session
{
enum HostCategory : unsigned int { CategoryNearby=1, CategoryPrivate=2, CategoryGroup=4, CategoryConference=8,
    CategoryViewer=16, CategoryTeleport=32, CategoryPermission=64, CategoryInventory=128 };
constexpr unsigned int ChatCategories=31, AllCategories=255;
inline unsigned int conversationCategory(Topic topic)
{ return topic==Topic::Nearby?CategoryNearby:topic==Topic::Private?CategoryPrivate:topic==Topic::Group?CategoryGroup:
    topic==Topic::Conference?CategoryConference:CategoryViewer; }
inline unsigned int hostCategory(const Message& event)
{
    if(event.eventType!=EventType::Attention && event.eventType!=EventType::AttentionRemoved) return conversationCategory(event.topic);
    return event.title=="Teleport offer"?CategoryTeleport:event.title=="Permission request"?CategoryPermission:
        event.title=="Inventory offer"?CategoryInventory:CategoryViewer;
}
inline unsigned int legacyAttentionCategories(unsigned int mask)
{ return ((mask&1)?CategoryPrivate|CategoryGroup|CategoryConference|CategoryViewer:0u) |
    ((mask&2)?CategoryNearby:0u) | ((mask&4)?CategoryTeleport:0u) |
    ((mask&8)?CategoryPermission:0u) | ((mask&16)?CategoryInventory:0u); }
}
#endif
