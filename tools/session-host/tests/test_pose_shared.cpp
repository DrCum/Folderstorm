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
 // The actual queue coalescer retains each pose entry's own revision rather
 // than assigning the newest overall revision to all disjoint retained joints.
 OwnerSession coalescedOwner;coalescedOwner.start("owner","coalesced",{{"mHead",initial},{"mNeck",initial}});
 assert(coalescedOwner.acquire("owner","","head",{"mHead"},0).granted);
 assert(coalescedOwner.apply("owner","","head",{{"mHead",changed}},true,0));
 assert(coalescedOwner.acquire("owner","","neck",{"mNeck"},0).granted);
 assert(coalescedOwner.apply("owner","","neck",{{"mNeck",changed}},true,0));
 const auto merged=coalescePoseUpdate({{"mHead",changed}},{{"mHead",1}},{{"mNeck",changed}},{{"mNeck",2}});
 assert(merged.pose==coalescedOwner.pose() && merged.revisions.at("mHead")==1 && merged.revisions.at("mNeck")==2);
 assert(coalescedOwner.revisionsMatch(merged.revisions,{"mHead","mNeck"}));
 const auto overlap=coalescePoseUpdate(merged.pose,merged.revisions,{{"mHead",initial}},{{"mHead",3}});
 assert(overlap.pose.at("mHead")==initial && overlap.revisions.at("mHead")==3 && overlap.revisions.at("mNeck")==2);
 const auto unknown=coalescePoseUpdate(merged.pose,merged.revisions,{{"mHead",initial}},{});
 assert(!unknown.revisions.count("mHead") && unknown.revisions.at("mNeck")==2);
 // The real ACK handler delegates cache writes to this helper. Late ACKs
 // still settle historical receipts but cannot mutate replacement sessions,
 // regrants, removed joints, or equal/newer per-joint cache versions.
 const SharedAcknowledgementSource oldSource{"session-a","generation-a","session-a","grant-a","private-token-a"};
 Pose cache{{"mHead",initial},{"mNeck",changed}};std::map<std::string,std::uint32_t> cacheRevisions{{"mHead",5},{"mNeck",9}};
 const Pose acknowledged{{"mHead",changed},{"mNeck",initial}};const JointSet ackJoints{"mHead","mNeck"};
 for(auto replacement:{SharedAcknowledgementSource{"session-b","generation-b","session-b","grant-b","private-token-b"},
   SharedAcknowledgementSource{"session-a","generation-a","session-a","grant-b","private-token-a"},
   SharedAcknowledgementSource{"session-a","generation-a","session-a","grant-a","private-token-b"},
   SharedAcknowledgementSource{"session-a","generation-b","session-a","grant-a","private-token-a"},
   SharedAcknowledgementSource{"session-a","generation-a","session-b","grant-a","private-token-a"}})
 {
  const auto beforeCache=cache;const auto beforeRevisions=cacheRevisions;
  assert(updateAcknowledgedCache(oldSource,replacement,ackJoints,acknowledged,{{"mHead",10},{"mNeck",10}},cache,cacheRevisions)==0);
  assert(cache==beforeCache && cacheRevisions==beforeRevisions);
 }
 // Out-of-order replies: advance head independently, keep newer neck state.
 assert(updateAcknowledgedCache(oldSource,oldSource,ackJoints,acknowledged,{{"mHead",6},{"mNeck",8}},cache,cacheRevisions)==1);
 assert(cache.at("mHead")==changed && cacheRevisions.at("mHead")==6 && cache.at("mNeck")==changed && cacheRevisions.at("mNeck")==9);
 assert(updateAcknowledgedCache(oldSource,oldSource,ackJoints,{{"mHead",initial},{"mNeck",initial}},{{"mHead",6},{"mNeck",9}},cache,cacheRevisions)==0);
 assert(cache.at("mHead")==changed && cache.at("mNeck")==changed);
 cache.erase("mHead");assert(updateAcknowledgedCache(oldSource,oldSource,{"mHead"},acknowledged,{{"mHead",10}},cache,cacheRevisions)==0 && !cache.count("mHead"));
 using namespace fs_assistant;bool active=true,nativeGrant=true;auto guard=std::make_shared<AsyncOperation>([&](){return active && nativeGrant?std::string():std::string("authority_changed");});
 auto task=std::make_shared<AsyncTask>(guard);assert(asyncTaskAllowed(task));nativeGrant=false;assert(!asyncTaskAllowed(task));nativeGrant=true;assert(!asyncTaskAllowed(task));
 // Publication authority retains its original native-session task while
 // independently approved peer edits and short-lived receipts come and go.
 OwnerSession publication;publication.start("owner","published",{{"mHead",initial},{"mNeck",initial}});
 const auto publishedSession=publication.session();
 auto originalAuthority=std::make_shared<AsyncOperation>([&](){return publication.session()==publishedSession?std::string():std::string("publication_replaced");});
 auto publicationTask=std::make_shared<AsyncTask>(originalAuthority);
 publication.grant("peer-a","grant-a",0);publication.grant("peer-b","grant-b",0);
 for(const auto& peer:std::vector<std::array<std::string,3>>{{"peer-a","grant-a","mHead"},{"peer-b","grant-b","mNeck"}})
 {
  auto editReceipt=std::make_shared<AsyncOperation>([](){return std::string();});
  assert(publication.acquire(peer[0],peer[1],peer[0],{peer[2]},0).granted);
  assert(publication.apply(peer[0],peer[1],peer[0],{{peer[2],changed}},true,0,true,1));
  editReceipt->cancel("receipt_pruned");editReceipt.reset();assert(asyncTaskAllowed(publicationTask));
 }
 assert(publication.authorized("peer-a","grant-a",0) && publication.authorized("peer-b","grant-b",0));
 // A -> B -> A with identical current region/URL IDs still invalidates a
 // captured queued transport. The production synchronous signal advances this
 // epoch and cancels captured guards before idle or the HTTP pool can observe.
 RegionGeneration region;const auto capturedRegion=region.value();assert(region.matches(capturedRegion));
 auto regionGuard=std::make_shared<AsyncOperation>([&](){return region.matches(capturedRegion)?std::string():std::string("native_region_changed");});
 auto regionTask=std::make_shared<AsyncTask>(regionGuard);assert(asyncTaskAllowed(regionTask));
 region.advance();regionGuard->cancel("native_region_changed");region.advance();
 assert(!region.matches(capturedRegion) && !asyncTaskAllowed(regionTask));assert(region.matches(region.value()));
 auto observer=std::make_shared<AsyncOperation>([](){return std::string();});observer->cancel("native_sharing_disabled");assert(!observer->allowed());
 std::cout<<"native shared-pose rotation, revision, lease and authority checks passed\n";
}
