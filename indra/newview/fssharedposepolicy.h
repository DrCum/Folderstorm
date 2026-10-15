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
}
#endif
