/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fssharedposepolicy.h"
#include "fsassistantasync.h"
#include <cassert>
#include <iostream>
int main()
{
 using namespace FSSharedPose;using namespace fs_pose;
 Joint initial;initial.enabled=true;initial.priority=3;initial.transform[4]=.25f;initial.transform[7]=1.25f;
 const Rotation requested{{0,0,1,0}};const auto changed=rotationJoint(initial,requested);
 for(unsigned i=4;i<10;++i)assert(changed.transform[i]==initial.transform[i]);
 assert(changed.priority==initial.priority && changed.enabled==initial.enabled && changed.worldLocked==initial.worldLocked && changed.mirrored==initial.mirrored);
 assert(rotationResult(initial,changed,requested,false));assert(!rotationResult(initial,initial,requested,false));assert(rotationResult(initial,initial,requested));
 auto external=changed;external.transform[4]=42;assert(!rotationResult(initial,external,requested));external=changed;external.priority=5;assert(!rotationResult(initial,external,requested));
 auto equivalent=changed;for(unsigned i=0;i<4;++i)equivalent.transform[i]=-equivalent.transform[i];assert(sameSharedJoint(changed,equivalent));
 OwnerSession owner;owner.start("owner","native-session",{{"mHead",initial},{"mNeck",initial}});
 const JointSet affected{"mHead"};const std::map<std::string,std::uint32_t> expected{{"mHead",0}};
 assert(owner.revisionsMatch(expected,affected));assert(!owner.acquire("source","forged","lease",affected,0).granted);
 owner.grant("source","native-token",0);owner.grant("manual","manual-token",0);
 assert(owner.acquire("manual","manual-token","manual-lease",affected,0).granted);
 assert(owner.reservationOwner("mHead",0)=="manual");assert(!owner.acquire("source","native-token","lease",affected,0).granted);
 owner.release("source","manual-lease");assert(owner.reservationOwner("mHead",0)=="manual"); // cleanup cannot release another editor
 owner.release("manual","manual-lease");assert(owner.acquire("source","native-token","lease",affected,0).granted);
 assert(owner.revisionsMatch(expected,affected));assert(owner.apply("source","native-token","lease",{{"mHead",changed}},true,0,true,1));
 assert(owner.jointRevision("mHead")==1 && owner.jointRevision("mNeck")==0);assert(!owner.revisionsMatch(expected,affected));
 assert(!owner.apply("source","native-token","lease",{{"mHead",initial}},true,0,true,1)); // no uncertain finalization replay
 // General native grant permits a new per-operation joint lease; it is not a
 // joint allowlist and cannot be supplied by an MCP Ask.
 assert(owner.acquire("source","native-token","neck",{"mNeck"},1).granted);
 owner.revoke("source");assert(!owner.apply("source","native-token","neck",{{"mNeck",changed}},true,1,true,1));
 assert(owner.pose().at("mHead")==changed && owner.pose().at("mNeck")==initial);
 assert(owner.acquire("manual","manual-token","manual-neck",{"mNeck"},1).granted);
 assert(owner.apply("manual","manual-token","manual-neck",{{"mNeck",changed}},true,1,true,1));
 // Accepted source history is scoped to its joints and refuses stale manual
 // changes. Cancellation does not call restore or owner.clear.
 owner.grant("source","new-token",1);assert(owner.acquire("source","new-token","source-undo",owner.historyJoints("source",false),1).granted);
 assert(owner.restore("source","new-token","source-undo",false,1));assert(owner.pose().at("mNeck")==changed);
 using namespace fs_assistant;bool active=true,nativeGrant=true;auto guard=std::make_shared<AsyncOperation>([&](){return active && nativeGrant?std::string():std::string("authority_changed");});
 auto task=std::make_shared<AsyncTask>(guard);assert(asyncTaskAllowed(task));nativeGrant=false;assert(!asyncTaskAllowed(task));nativeGrant=true;assert(!asyncTaskAllowed(task));
 auto observer=std::make_shared<AsyncOperation>([](){return std::string();});observer->cancel("native_sharing_disabled");assert(!observer->allowed());
 std::cout<<"native shared-pose rotation, revision, lease and authority checks passed\n";
}
