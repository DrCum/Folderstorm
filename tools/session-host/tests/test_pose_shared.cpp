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
 // Received-overlay ownership survives stream updates and is released only
 // for the exact unchanged native generation. Pre-existing/manual work is
 // detached and must survive toggle/region cleanup, including same-value edits.
 const Pose firstOverlay{{"mHead",initial}},secondOverlay{{"mHead",changed}};
 ReceivedOverlay received;received.observe("",{},"received-motion",firstOverlay);
 assert(received.canRelease("received-motion",firstOverlay));
 received.observe("received-motion",firstOverlay,"received-motion",secondOverlay);
 assert(received.canRelease("received-motion",secondOverlay));
 assert(!received.canRelease("newer-native-motion",secondOverlay));
 assert(!received.canRelease("received-motion",firstOverlay));
 ReceivedOverlay preexisting;preexisting.observe("manual-motion",firstOverlay,"manual-motion",secondOverlay);assert(!preexisting.owned());
 received.detach();received.observe("received-motion",secondOverlay,"received-motion",secondOverlay);assert(!received.owned());
 ReceivedOverlay externallyChanged;externallyChanged.observe("",{},"received-motion",firstOverlay);
 externallyChanged.observe("received-motion",secondOverlay,"received-motion",secondOverlay);assert(!externallyChanged.owned());
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
 // Ordinary native final ACKs use the same proof/cache helper. A newer
 // broadcast followed by ACK1 cannot poison bytes while retaining revision2;
 // completion/locked refresh renders the cache, and duplicate snapshot2 stays
 // consistent even though equal-version content cannot repair bad cache bytes.
 Pose nativeCache{{"mHead",initial}};std::map<std::string,std::uint32_t> nativeRevisions{{"mHead",0}};std::uint32_t nativeGlobal=0;
 assert(ingestObservedPose({{"mHead",changed}},{{"mHead",2}},2,false,false,nativeCache,nativeRevisions,nativeGlobal).accepted);
 assert(updateAcknowledgedCache(oldSource,oldSource,{"mHead"},{{"mHead",initial}},{{"mHead",1}},nativeCache,nativeRevisions)==0);
 assert(observedWorkPose(nativeCache,{"mHead"}).at("mHead")==changed && nativeRevisions.at("mHead")==2);
 auto duplicateSnapshot=ingestObservedPose({{"mHead",changed}},{{"mHead",2}},2,true,false,nativeCache,nativeRevisions,nativeGlobal);
 assert(duplicateSnapshot.display.at("mHead")==changed && nativeCache.at("mHead")==changed && nativeRevisions.at("mHead")==2);
 assert(updateAcknowledgedCache(oldSource,oldSource,{"mHead"},{{"mHead",initial}},{},nativeCache,nativeRevisions)==0); // unversioned native reply never invents a revision
 // An Ask freezes the exact selected joint, not unrelated joint/global
 // updates. Native/session/grant identity and selected reservation remain exact.
 PreparedSharedSource prepared{"target","session","grant","motion",true,true,true,true,false};
 std::map<std::string,PreparedSharedJoint> preparedState{{"mHead",{}},{"mNeck",{}}};preparedState["mHead"].editable=true;preparedState["mNeck"].editable=true;
 auto currentPrepared=preparedState;currentPrepared["mNeck"].rotation=requested;currentPrepared["mNeck"].revision=7;currentPrepared["mNeck"].reserved=true;
 assert(preparedSharedEditCurrent(prepared,prepared,{"mHead"},preparedState,currentPrepared));
 currentPrepared["mHead"].revision=1;assert(!preparedSharedEditCurrent(prepared,prepared,{"mHead"},preparedState,currentPrepared));
 currentPrepared=preparedState;currentPrepared["mHead"].rotation=requested;assert(!preparedSharedEditCurrent(prepared,prepared,{"mHead"},preparedState,currentPrepared));
 currentPrepared=preparedState;currentPrepared["mHead"].reservationKnown=true;assert(!preparedSharedEditCurrent(prepared,prepared,{"mHead"},preparedState,currentPrepared));
 auto changedProof=prepared;changedProof.grantGeneration="new-grant";assert(!preparedSharedEditCurrent(prepared,changedProof,{"mHead"},preparedState,preparedState));
 // ACK head2 can overtake a globally older broadcast head1/neck1. The real
 // ingestion helper preserves head2 for cache and display while accepting the
 // disjoint neck, even when an unrelated frame already advanced global r.
 Pose observed{{"mHead",changed}};std::map<std::string,std::uint32_t> observedRevisions{{"mHead",2}};std::uint32_t observedGlobal=0;
 auto frame=ingestObservedPose({{"mHead",initial},{"mNeck",initial}},{{"mHead",1},{"mNeck",1}},1,false,false,observed,observedRevisions,observedGlobal);
 assert(frame.accepted && observed.at("mHead")==changed && frame.display.at("mHead")==changed && observedRevisions.at("mHead")==2);
 assert(observed.count("mNeck") && observedRevisions.at("mNeck")==1 && observedGlobal==1);
 observedGlobal=4;frame=ingestObservedPose({{"mNeck",changed}},{{"mNeck",3}},3,false,false,observed,observedRevisions,observedGlobal);
 assert(frame.accepted && observed.at("mNeck")==changed && observedRevisions.at("mNeck")==3 && observedGlobal==4);
 frame=ingestObservedPose({{"mHead",initial},{"mNeck",initial}},{{"mHead",1},{"mNeck",1}},1,true,false,observed,observedRevisions,observedGlobal);
 assert(frame.accepted && observed.at("mHead")==changed && observed.at("mNeck")==changed);
 frame=ingestObservedPose({{"mNeck",initial}},{{"mNeck",1}},1,true,false,observed,observedRevisions,observedGlobal);
 assert(frame.accepted && observed.count("mHead") && frame.display.at("mHead")==changed); // snapshot cannot erase newer ACK joint
 frame=ingestObservedPose({{"mHead",initial}},{{"mHead",0}},0,true,true,observed,observedRevisions,observedGlobal);
 assert(frame.accepted && observed.size()==1 && observed.at("mHead")==initial && observedRevisions.at("mHead")==0 && observedGlobal==0);
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
 // Both native disable controls use OwnerSession::clear synchronously,
 // including a manually published owner with no local assistant work. Turning
 // the control back on does not recreate consent, reservations or publication.
 for(const auto* control:{"FSSharedPosingEnabled","UseLSLBridge"})
 {
  (void)control;OwnerSession manualPublication;manualPublication.start("owner","manual-session",{{"mHead",initial}});
  manualPublication.grant("remote","old-grant",0);assert(manualPublication.acquire("remote","old-grant","old-lease",{"mHead"},0).granted);
  manualPublication.clear();
  assert(!manualPublication.active() && manualPublication.editors().empty());
  assert(!manualPublication.authorized("remote","old-grant",0));
  assert(!manualPublication.apply("remote","old-grant","old-lease",{{"mHead",changed}},true,0,true,1));
  // Only a later explicit native publication/consent flow grants again.
  manualPublication.start("owner","new-manual-session",{{"mHead",initial}});
  assert(!manualPublication.authorized("remote","old-grant",0));manualPublication.grant("remote","new-grant",0);
  assert(!manualPublication.apply("remote","new-grant","old-lease",{{"mHead",changed}},true,0,true,1));
  assert(manualPublication.pose().at("mHead")==initial);
 }
 // The owner's region signal must clear native consent even when the
 // editor stays in A and its own source task/region never changes. No owner
 // assistant task or idle observation is involved in this round trip.
 OwnerSession travellingOwner;travellingOwner.start("owner","owner-in-A",{{"mHead",initial}});
 travellingOwner.grant("stationary-editor","old-owner-grant",0);
 assert(travellingOwner.acquire("stationary-editor","old-owner-grant","old-owner-lease",{"mHead"},0).granted);
 RegionGeneration ownerRegion,stationaryEditorRegion;const auto unchangedEditorRegion=stationaryEditorRegion.value();
 std::string currentOwnerRegion="A";
 const auto ownerRegionChanged=[&](){ownerRegion.advance();travellingOwner.clear();};
 currentOwnerRegion="B";ownerRegionChanged();currentOwnerRegion="A";ownerRegionChanged();
 assert(currentOwnerRegion=="A" && stationaryEditorRegion.matches(unchangedEditorRegion));
 assert(!travellingOwner.active() && !travellingOwner.authorized("stationary-editor","old-owner-grant",0));
 assert(!travellingOwner.apply("stationary-editor","old-owner-grant","old-owner-lease",{{"mHead",changed}},true,0,true,1));
 travellingOwner.start("owner","new-owner-in-A",{{"mHead",initial}});
 assert(!travellingOwner.authorized("stationary-editor","old-owner-grant",0));
 // Native location/name/nearby restrictions gate every private payload and
 // deferred source effect. A restrict/unrestrict cycle cannot revive a task.
 for(unsigned mask=0;mask<8;++mask)assert(sharedPrivacyAllowed(mask&1,mask&2,mask&4)==(mask==7));
 bool privacyVisible=true;auto privacyGuard=std::make_shared<AsyncOperation>([&](){return sharedPrivacyAllowed(privacyVisible,true,true)?std::string():std::string("native_privacy_restricted");});
 auto privacyTask=std::make_shared<AsyncTask>(privacyGuard);assert(asyncTaskAllowed(privacyTask));
 privacyVisible=false;privacyGuard->cancel("native_privacy_restricted");privacyVisible=true;assert(!asyncTaskAllowed(privacyTask));
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
