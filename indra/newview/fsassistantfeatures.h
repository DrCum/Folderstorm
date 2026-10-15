/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_FEATURES_H
#define FS_ASSISTANT_FEATURES_H
#include <array>
#include <string>
#include <vector>
namespace fs_assistant {
struct FeatureOperation {const char* api;const char* op;const char* capability;const char* permission;bool readOnly;bool active;const char* receiptRead;};
inline constexpr std::array<FeatureOperation, 54> featureOperations {{
    {"FSChat","conversations","social_chat","chat_read",true,false,"chat_read"},
    {"FSChat","history","social_chat","chat_read",true,false,"chat_read"},
    {"FSChat","search","social_chat","chat_read",true,false,"chat_read"},
    {"FSChat","draftGet","social_chat","chat_read",true,false,"chat_read"},
    {"FSChat","open","social_chat","chat_state",false,false,"chat_read"},
    {"FSChat","draftSet","social_chat","chat_state",false,false,"chat_read"},
    {"FSChat","markRead","social_chat","chat_state",false,false,"chat_read"},
    {"FSChat","close","social_chat","chat_state",false,false,"chat_read"},
    {"FSChat","sendIM","social_chat","chat_send_im",false,false,"chat_read"},
    {"FSChat","sendGroup","social_chat","chat_send_group",false,false,"chat_read"},
    {"FSChat","sendConference","social_chat","chat_send_conference",false,false,"chat_read"},
    {"FSChat","sendNearby","social_chat","chat_send_nearby",false,false,"chat_read"},
    {"FSContacts","friends","social_contacts","friends_read",true,false,"friends_read"},
    {"FSContacts","search","social_contacts","resident_search",true,false,"resident_search"},
    {"FSContacts","profile","social_contacts","profile_read",true,false,"profile_read"},
    {"FSEvents","poll","social_events","",true,false,""},
    {"FSEvents","notifications","social_events","notifications_read",true,false,"notifications_read"},
    {"FSEvents","getNotification","social_events","notifications_read",true,false,"notifications_read"},
    {"FSEvents","reviewNotification","social_events","notification_review",false,true,"notifications_read"},
    {"FSWorld","location","world_observation","world_read",true,false,"world_read"},
    {"FSWorld","parcel","world_observation","world_read",true,false,"world_read"},
    {"FSWorld","avatars","world_observation","world_read",true,false,"world_read"},
    {"FSWorld","objects","world_observation","world_read",true,false,"world_read"},
    {"FSWorld","audio","world_observation","world_read",true,false,"world_read"},
    {"FSWorld","media","world_observation","world_read",true,false,"world_read"},
    {"FSGestures","list","gesture_tools","gesture_read",true,false,"gesture_read"},
    {"FSGestures","status","gesture_tools","gesture_read",true,false,"gesture_read"},
    {"FSGestures","play","gesture_tools","gesture_play",false,true,"gesture_read"},
    {"FSGestures","stop","gesture_tools","gesture_play",false,true,"gesture_read"},
    {"FSPose","status","pose_tools","pose_read",true,false,"pose_read"},
    {"FSPose","joints","pose_tools","pose_read",true,false,"pose_read"},
    {"FSPose","presets","pose_tools","pose_read",true,false,"pose_read"},
    {"FSPose","start","pose_tools","pose_edit",false,true,"pose_read"},
    {"FSPose","update","pose_tools","pose_edit",false,true,"pose_read"},
    {"FSPose","undo","pose_tools","pose_edit",false,true,"pose_read"},
    {"FSPose","reset","pose_tools","pose_edit",false,true,"pose_read"},
    {"FSPose","stop","pose_tools","pose_edit",false,true,"pose_read"},
    {"FSPose","load","pose_tools","pose_edit",false,true,"pose_read"},
    {"FSPose","save","pose_tools","pose_save",false,true,"pose_read"},
    {"FSPose","sharedSessions","shared_pose_tools","pose_read",true,false,"pose_read"},
    {"FSPose","sharedGrants","shared_pose_tools","pose_read",true,false,"pose_read"},
    {"FSPose","sharedEdit","shared_pose_tools","pose_share",false,true,"pose_read"},
    {"FSPose","startSharing","shared_pose_tools","pose_share",false,true,"pose_read"},
    {"FSPose","stopSharing","shared_pose_tools","pose_share",false,true,"pose_read"},
    {"FSContent","get","content_editing","read",true,false,"read"},
    {"FSContent","preview","content_editing","read",true,false,"read"},
    {"FSContent","saveNotecard","content_editing","content_edit",false,false,"read"},
    {"FSContent","saveScript","content_editing","content_edit",false,false,"read"},
    {"FSPreferences","schema","preferences_tools","preferences_read",true,false,"preferences_read"},
    {"FSPreferences","get","preferences_tools","preferences_read",true,false,"preferences_read"},
    {"FSPreferences","status","preferences_tools","preferences_read",true,false,"preferences_read"},
    {"FSPreferences","preview","preferences_tools","preferences_write",false,false,"preferences_read"},
    {"FSPreferences","accept","preferences_tools","preferences_write",false,false,"preferences_read"},
    {"FSPreferences","cancel","preferences_tools","preferences_write",false,false,"preferences_read"},
}};
inline const FeatureOperation* featureOperation(const std::string& api,const std::string& op)
{for(const auto& entry:featureOperations)if(api==entry.api && op==entry.op)return &entry;return nullptr;}
template<typename Lookup> bool receiptVisible(const std::vector<std::string>& reads,Lookup lookup)
{for(const auto& permission:reads)if(lookup(permission)!="allow")return false;return true;}
inline bool featureAPI(const std::string& api)
{for(const auto& entry:featureOperations)if(api==entry.api)return true;return false;}
}
#endif
