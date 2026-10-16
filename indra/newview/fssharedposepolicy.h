/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SHARED_POSE_POLICY_H
#define FS_SHARED_POSE_POLICY_H
#include "fsposepolicy.h"
#include "fssharedposemodel.h"
#include <cstdint>
namespace fs_pose {
// IDs may return to their previous values before an idle/HTTP callback runs.
// This synchronous region epoch never returns to a captured value. Exhaustion
// leaves zero permanently invalid instead of permitting wraparound reuse.
class RegionGeneration
{
 std::uint64_t mValue=1;
public:
 std::uint64_t value() const{return mValue;}
 void advance(){if(mValue)++mValue;}
 bool matches(std::uint64_t expected) const{return expected && expected==mValue;}
};
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
// Only a motion started by receiving a remote pose may be released by the
// sharing lifecycle. Pre-existing or subsequently edited native motions belong
// to their manual owner, even if an incoming stream used the same motion ID.
struct ReceivedOverlay
{
 std::string generation;FSSharedPose::Pose expected;
 bool owned() const{return !generation.empty();}
 void detach(){generation.clear();expected.clear();}
 void observe(const std::string& beforeGeneration,const FSSharedPose::Pose& before,
              const std::string& afterGeneration,const FSSharedPose::Pose& after)
 {
  const bool retained=owned() && generation==beforeGeneration && sameSharedPose(expected,before);
  if(afterGeneration.empty() || (!beforeGeneration.empty() && !retained)){detach();return;}
  generation=afterGeneration;expected=after;
 }
 bool canRelease(const std::string& currentGeneration,const FSSharedPose::Pose& current) const
 {return owned() && generation==currentGeneration && sameSharedPose(expected,current);}
};
struct CoalescedPoseUpdate
{
 FSSharedPose::Pose pose;std::map<std::string,std::uint32_t> revisions;
};
inline CoalescedPoseUpdate coalescePoseUpdate(const FSSharedPose::Pose& previous,const std::map<std::string,std::uint32_t>& previousRevisions,
 const FSSharedPose::Pose& next,const std::map<std::string,std::uint32_t>& nextRevisions)
{
 CoalescedPoseUpdate result;result.pose=previous;
 for(const auto& joint:previous){const auto revision=previousRevisions.find(joint.first);if(revision!=previousRevisions.end())result.revisions[joint.first]=revision->second;}
 for(const auto& joint:next)
 {
  result.pose[joint.first]=joint.second;const auto revision=nextRevisions.find(joint.first);
  if(revision!=nextRevisions.end())result.revisions[joint.first]=revision->second;else result.revisions.erase(joint.first);
 }
 return result;
}
struct ObservedPoseFrame {bool accepted=false;FSSharedPose::Pose display;};
inline ObservedPoseFrame ingestObservedPose(const FSSharedPose::Pose& incoming,const std::map<std::string,std::uint32_t>& incomingRevisions,
 std::uint32_t incomingGlobal,bool snapshot,bool newSession,FSSharedPose::Pose& pose,std::map<std::string,std::uint32_t>& revisions,std::uint32_t& global)
{
 ObservedPoseFrame result;
 for(const auto& joint:incoming){const auto revision=incomingRevisions.find(joint.first);if(revision==incomingRevisions.end() || revision->second>incomingGlobal)return result;}
 if(newSession){pose.clear();revisions.clear();global=0;}
 if(snapshot)
 {
  for(auto it=pose.begin();it!=pose.end();)
  {
   if(incoming.count(it->first)){++it;continue;}
   const auto revision=revisions.find(it->first);
   if(revision!=revisions.end() && revision->second>incomingGlobal){result.display.emplace(*it);++it;}
   else{revisions.erase(it->first);it=pose.erase(it);}
  }
 }
 for(const auto& joint:incoming)
 {
  const auto revision=incomingRevisions.at(joint.first),cached=revisions.find(joint.first)==revisions.end()?0:revisions.at(joint.first);
  if(!pose.count(joint.first) || revision>cached){pose[joint.first]=joint.second;revisions[joint.first]=revision;}
  // Render the newest observed version, including an ACK that overtook this
  // broadcast. Equal or older frame content can never rewind the cache/scene.
  result.display[joint.first]=pose.at(joint.first);
 }
 global=std::max(global,incomingGlobal);result.accepted=true;return result;
}
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
