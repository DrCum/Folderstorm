/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_FEATURES_H
#define FS_ASSISTANT_FEATURES_H
#include <array>
#include <string>
namespace fs_assistant {
struct FeatureOperation {const char* api;const char* op;const char* capability;const char* permission;bool readOnly;bool active;};
inline constexpr std::array<FeatureOperation, 54> featureOperations {{
    {"FSChat","conversations","social_chat","chat_read",true,false},
    {"FSChat","history","social_chat","chat_read",true,false},
    {"FSChat","search","social_chat","chat_read",true,false},
    {"FSChat","draftGet","social_chat","chat_read",true,false},
    {"FSChat","open","social_chat","chat_state",false,false},
    {"FSChat","draftSet","social_chat","chat_state",false,false},
    {"FSChat","markRead","social_chat","chat_state",false,false},
    {"FSChat","close","social_chat","chat_state",false,false},
    {"FSChat","sendIM","social_chat","chat_send_im",false,false},
    {"FSChat","sendGroup","social_chat","chat_send_group",false,false},
    {"FSChat","sendConference","social_chat","chat_send_conference",false,false},
    {"FSChat","sendNearby","social_chat","chat_send_nearby",false,false},
    {"FSContacts","friends","social_contacts","friends_read",true,false},
    {"FSContacts","search","social_contacts","resident_search",true,false},
    {"FSContacts","profile","social_contacts","profile_read",true,false},
    {"FSEvents","poll","social_events","",true,false},
    {"FSEvents","notifications","social_events","notifications_read",true,false},
    {"FSEvents","getNotification","social_events","notifications_read",true,false},
    {"FSEvents","reviewNotification","social_events","notification_review",false,true},
    {"FSWorld","location","world_observation","world_read",true,false},
    {"FSWorld","parcel","world_observation","world_read",true,false},
    {"FSWorld","avatars","world_observation","world_read",true,false},
    {"FSWorld","objects","world_observation","world_read",true,false},
    {"FSWorld","audio","world_observation","world_read",true,false},
    {"FSWorld","media","world_observation","world_read",true,false},
    {"FSGestures","list","gesture_tools","gesture_read",true,false},
    {"FSGestures","status","gesture_tools","gesture_read",true,false},
    {"FSGestures","play","gesture_tools","gesture_play",false,true},
    {"FSGestures","stop","gesture_tools","gesture_play",false,true},
    {"FSPose","status","pose_tools","pose_read",true,false},
    {"FSPose","joints","pose_tools","pose_read",true,false},
    {"FSPose","presets","pose_tools","pose_read",true,false},
    {"FSPose","start","pose_tools","pose_edit",false,true},
    {"FSPose","update","pose_tools","pose_edit",false,true},
    {"FSPose","undo","pose_tools","pose_edit",false,true},
    {"FSPose","reset","pose_tools","pose_edit",false,true},
    {"FSPose","stop","pose_tools","pose_edit",false,true},
    {"FSPose","load","pose_tools","pose_edit",false,true},
    {"FSPose","save","pose_tools","pose_edit",false,true},
    {"FSPose","sharedSessions","shared_pose_tools","pose_read",true,false},
    {"FSPose","sharedGrants","shared_pose_tools","pose_read",true,false},
    {"FSPose","sharedEdit","shared_pose_tools","pose_share",false,true},
    {"FSPose","startSharing","shared_pose_tools","pose_share",false,true},
    {"FSPose","stopSharing","shared_pose_tools","pose_share",false,true},
    {"FSContent","get","content_editing","read",true,false},
    {"FSContent","preview","content_editing","read",true,false},
    {"FSContent","saveNotecard","content_editing","content_edit",false,false},
    {"FSContent","saveScript","content_editing","content_edit",false,false},
    {"FSPreferences","schema","preferences_tools","preferences_read",true,false},
    {"FSPreferences","get","preferences_tools","preferences_read",true,false},
    {"FSPreferences","status","preferences_tools","preferences_read",true,false},
    {"FSPreferences","preview","preferences_tools","preferences_write",false,false},
    {"FSPreferences","accept","preferences_tools","preferences_write",false,false},
    {"FSPreferences","cancel","preferences_tools","preferences_write",false,false},
}};
inline const FeatureOperation* featureOperation(const std::string& api,const std::string& op)
{for(const auto& entry:featureOperations)if(api==entry.api && op==entry.op)return &entry;return nullptr;}
inline bool featureAPI(const std::string& api)
{for(const auto& entry:featureOperations)if(api==entry.api)return true;return false;}
}
#endif
