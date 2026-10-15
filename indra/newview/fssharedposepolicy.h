/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SHARED_POSE_POLICY_H
#define FS_SHARED_POSE_POLICY_H
#include "fsposepolicy.h"
#include "fssharedposemodel.h"
namespace fs_pose {
inline FSSharedPose::Joint rotationJoint(const FSSharedPose::Joint& fresh,const Rotation& rotation)
{auto result=fresh;for(unsigned i=0;i<4;++i)result.transform[i]=static_cast<float>(rotation[i]);return result;}
inline bool sameSharedJoint(const FSSharedPose::Joint& a,const FSSharedPose::Joint& b)
{
 const Rotation qa{{a.transform[0],a.transform[1],a.transform[2],a.transform[3]}},qb{{b.transform[0],b.transform[1],b.transform[2],b.transform[3]}};
 if(!sameRotation(qa,qb))return false;
 for(unsigned i=4;i<10;++i)if(a.transform[i]!=b.transform[i])return false;
 return a.enabled==b.enabled && a.priority==b.priority && a.worldLocked==b.worldLocked && a.mirrored==b.mirrored;
}
inline bool rotationResult(const FSSharedPose::Joint& before,const FSSharedPose::Joint& after,const Rotation& requested,bool allowBefore=true)
{return (allowBefore && sameSharedJoint(before,after)) || sameSharedJoint(rotationJoint(before,requested),after);}
inline bool sameSharedPose(const FSSharedPose::Pose& a,const FSSharedPose::Pose& b)
{if(a.size()!=b.size())return false;for(const auto& joint:a){const auto other=b.find(joint.first);if(other==b.end() || !sameSharedJoint(joint.second,other->second))return false;}return true;}
// An old final ACK can settle its operation receipt without being a snapshot
// of the currently observed native session or grant. Keep those decisions
// separate; cache updates need the exact still-current private native proof.
struct SharedAcknowledgementSource
{
 std::string session,remoteGeneration,grantSession,grantGeneration,grantToken;
};
inline bool currentAcknowledgementSource(const SharedAcknowledgementSource& expected,const SharedAcknowledgementSource& current)
{
 return !expected.session.empty() && !expected.remoteGeneration.empty() && !expected.grantGeneration.empty() && !expected.grantToken.empty() &&
  expected.session==expected.grantSession && current.session==current.grantSession && expected.session==current.session &&
  expected.remoteGeneration==current.remoteGeneration && expected.grantGeneration==current.grantGeneration && expected.grantToken==current.grantToken;
}
inline unsigned updateAcknowledgedCache(const SharedAcknowledgementSource& expected,const SharedAcknowledgementSource& current,
 const FSSharedPose::JointSet& requested,const FSSharedPose::Pose& acknowledged,const std::map<std::string,std::uint32_t>& acknowledgedRevisions,
 FSSharedPose::Pose& pose,std::map<std::string,std::uint32_t>& revisions)
{
 if(!currentAcknowledgementSource(expected,current))return 0;
 unsigned updated=0;
 for(const auto& name:requested)
 {
  const auto joint=acknowledged.find(name);const auto live=pose.find(name);
  const auto revision=acknowledgedRevisions.find(name);const auto cached=revisions.find(name);
  // Never reinsert a removed joint or replace equal/newer observed content.
  // Different joints may advance independently of the session's global r.
  if(joint==acknowledged.end() || live==pose.end() || revision==acknowledgedRevisions.end() || cached==revisions.end() || revision->second<=cached->second)continue;
  live->second=joint->second;cached->second=revision->second;++updated;
 }
 return updated;
}

}
#endif
