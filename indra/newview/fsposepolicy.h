/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_POSE_POLICY_H
#define FS_POSE_POLICY_H
#include <array>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace fs_pose {
using Rotation=std::array<double,4>;
using Rotations=std::map<std::string,Rotation>;
inline constexpr std::size_t jointLimit=64,presetLimit=65536;
inline bool jointName(const std::string& name)
{return !name.empty() && name.size()<=64 && name!="mPelvis" && name!="mRoot" && name!="mAvatarRoot" &&
 name.front()=='m' && name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")==std::string::npos;}
inline bool normalize(Rotation& rotation)
{
 double norm=0.;for(double value:rotation){if(!std::isfinite(value) || std::abs(value)>1.001)return false;norm+=value*value;}
 if(std::abs(norm-1.)>.002)return false;
 norm=std::sqrt(norm);for(auto& value:rotation)value/=norm;
 // q and -q represent the same rotation: freeze one representation.
 if(rotation[3]<0)for(auto& value:rotation)value=-value;
 return true;
}
// The blender seeds an unanimated channel from the current LLJoint value.
// Removing a state alone therefore cannot undo our rotation. Restore only a
// channel still equal to our last proposed full-weight rotation (q == -q).
inline bool sameRotation(const Rotation& a,const Rotation& b)
{
 double direct=0.,opposite=0.;
 for(unsigned i=0;i<4;++i){if(!std::isfinite(a[i]) || !std::isfinite(b[i]))return false;direct=std::max(direct,std::abs(a[i]-b[i]));opposite=std::max(opposite,std::abs(a[i]+b[i]));}
 return std::min(direct,opposite)<=1e-5;
}
struct OwnedRotation
{
 Rotation original{{0,0,0,1}},expected{{0,0,0,1}};bool wrote=false;
 bool restore(const Rotation& current) const{return wrote && sameRotation(current,expected);}
 bool complete(const Rotation& current) const{return !wrote || sameRotation(current,original) || restore(current);}
};
inline bool valid(const Rotations& rotations)
{
 if(rotations.empty() || rotations.size()>jointLimit)return false;
 for(const auto& joint:rotations){auto rotation=joint.second;if(!jointName(joint.first) || !normalize(rotation))return false;}
 return true;
}
inline bool presetID(const std::string& id)
{return !id.empty() && id.size()<=64 && id!="." && id!=".." && id.front()!='.' &&
 id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;}
// History belongs to this transaction. The native manual Poser has a separate
// motion and undo history; this never consumes or resets it.
class Transaction
{
public:
 unsigned revision() const{return mRevision;}
 const Rotations& rotations() const{return mCurrent;}
 const Rotations& original() const{return mOriginal;}
 bool update(unsigned expected,const Rotations& changes,const Rotations& captured)
 {
  if(expected!=mRevision || !valid(changes))return false;
  auto next=mCurrent;for(const auto& joint:changes)next[joint.first]=joint.second;
  if(next.size()>jointLimit)return false;
  for(const auto& joint:changes)if(!mOriginal.count(joint.first))
  {const auto before=captured.find(joint.first);if(before==captured.end())return false;}
  for(const auto& joint:changes)if(!mOriginal.count(joint.first))mOriginal[joint.first]=captured.at(joint.first);
  mHistory.push_back(mCurrent);if(mHistory.size()>20)mHistory.erase(mHistory.begin());
  mCurrent=std::move(next);++mRevision;return true;
 }
 bool undo(unsigned expected)
 {if(expected!=mRevision || mHistory.empty())return false;mCurrent=mHistory.back();mHistory.pop_back();++mRevision;return true;}
 bool reset(unsigned expected)
 {if(expected!=mRevision)return false;mHistory.push_back(mCurrent);if(mHistory.size()>20)mHistory.erase(mHistory.begin());mCurrent.clear();++mRevision;return true;}
 bool canUndo() const{return !mHistory.empty();}
private:
 unsigned mRevision=0;Rotations mCurrent,mOriginal;std::vector<Rotations> mHistory;
};
}
#endif
