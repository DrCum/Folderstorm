/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsposepolicy.h"
#include "fsassistantasync.h"
#include <cassert>
#include <iostream>
#include <limits>
int main()
{
 using namespace fs_pose;
 assert(!jointName("mPelvis") && !jointName("mRoot") && !jointName("/tmp/mHead"));assert(jointName("mHead"));
 Rotation q{{0,0,0,-1}};assert(normalize(q) && q[3]==1);Rotation nan{{0,0,std::numeric_limits<double>::quiet_NaN(),1}};assert(!normalize(nan));
 assert(!presetID("../escape") && !presetID("https://example.test") && !presetID(".hidden"));assert(presetID("my_pose-1"));
 Rotations captured{{"mHead",{{0,0,0,1}}},{"mNeck",{{0,0,0,1}}}};
 Transaction transaction;assert(transaction.update(0,{{"mHead",{{0,0,1,0}}}},captured));assert(transaction.revision()==1);
 assert(!transaction.update(0,{{"mHead",{{0,0,0,1}}}},captured));assert(transaction.rotations().at("mHead")[2]==1);
 assert(transaction.update(1,{{"mNeck",{{0,0,1,0}}}},captured));assert(transaction.undo(2));assert(transaction.rotations().size()==1);
 assert(transaction.reset(3));assert(transaction.rotations().empty());assert(transaction.undo(4));assert(transaction.rotations().size()==1);
 // Model the real blender: an absent ROT state retains the current joint
 // as its seed. All release reasons use the same native ownership policy.
 for(const auto* reason:{"undo_first_update","reset","stop","revoke"})
 {
  (void)reason;OwnedRotation owned;owned.original={{0,0,0,1}};owned.expected={{0,0,1,0}};owned.wrote=true;
  Rotation current=owned.expected;if(owned.restore(current))current=owned.original;
  const Rotation blenderSeed=current;assert(sameRotation(blenderSeed,owned.original));
  Rotation external{{0,1,0,0}};assert(!owned.restore(external) && !owned.complete(external));
  assert(owned.complete(owned.original)); // first update had not blended yet
  Rotation equivalent{{0,0,-1,0}};assert(owned.restore(equivalent));
  owned.wrote=false;assert(!owned.restore(external) && owned.complete(external));
 }
 using namespace fs_assistant;bool active=true,manual=false;auto guard=std::make_shared<AsyncOperation>([&](){return active && !manual?std::string():std::string("ownership_changed");});
 auto task=std::make_shared<AsyncTask>(guard);int rotations=0,positions=0,scales=0,manualResets=0;
 auto frame=[&](){if(asyncTaskAllowed(task))++rotations;};frame();manual=true;frame();manual=false;frame();assert(rotations==1 && positions==0 && scales==0 && manualResets==0);
 auto newer=std::make_shared<AsyncOperation>([](){return std::string();});auto newerTask=std::make_shared<AsyncTask>(newer);assert(asyncTaskAllowed(newerTask));
 std::cout<<"pose policy checks passed\n";
}
