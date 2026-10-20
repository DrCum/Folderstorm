/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_NAVIGATION_MODEL_H
#define FS_NAVIGATION_MODEL_H
#include <string>
namespace fs_navigation
{
// Native pilot behavior names are ownership tokens, never a general stop switch.
inline bool owns(const std::string& token,bool pilot,const std::string& behavior)
{ return !token.empty() && pilot && behavior==token; }
inline bool canContinue(bool authority,bool sameSource,bool active,bool restrictions,double now,double deadline)
{ return authority && sameSource && active && restrictions && now<deadline; }
inline bool bounds(double distance,double stopDistance,int seconds)
{ return distance>=0. && distance<=64. && stopDistance>=.25 && stopDistance<=4. && seconds>=1 && seconds<=30; }
}
#endif
