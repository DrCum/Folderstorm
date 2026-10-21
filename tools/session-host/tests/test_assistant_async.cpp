/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantasync.h"
#include "fsassistantpolicy.h"
#include <cassert>
#include <iostream>
#include <tuple>

// Exercise the production load-generation tracker with the same captured
// (resident, request type, authority) key as AgentProfile's pending fetches.
void checkProfileRequestOwnership()
{
 using namespace fs_assistant;
 using Key=std::tuple<std::string,int,AsyncGuard>;
 for(bool nativeFirst:{false,true})for(bool nativeReplyFirst:{false,true})
 {
  bool permitted=true;
  auto authority=std::make_shared<AsyncOperation>([&](){return permitted?std::string():std::string("revoked");});
  const Key nativeKey{"resident",1,{}},assistantKey{"resident",1,authority};
  AsyncLoads<Key> loads;std::map<Key,bool> pending;
  auto begin=[&](const Key& key)
  {
   AsyncScope scope(std::get<2>(key));
   assert(pending.emplace(key,true).second);
   return loads.begin(key);
  };
  AsyncTaskPtr assistant,native;
  if(nativeFirst){native=begin(nativeKey);assistant=begin(assistantKey);}
  else {assistant=begin(assistantKey);native=begin(nativeKey);}
  assert(pending.size()==2 && !native->guard() && assistant->guard()==authority);
  // Duplicate native requests still deduplicate in their own domain.
  assert(pending.count(nativeKey)==1);
  permitted=false;unsigned nativeResults=0,assistantResults=0;
  auto finish=[&](const Key& key,const AsyncTaskPtr& task)
  {
   // Completion runs outside the initiating scope. It must use the captured
   // key rather than clearing another caller's null/native pending entry.
   assert(!AsyncScope::current());
   if(!loads.finish(key,task))return;
   assert(pending.erase(key)==1);
   if(!asyncTaskAllowed(task))return;
   ++nativeResults;
   if(asyncTaskAllowed(assistant))++assistantResults;
  };
  if(nativeReplyFirst){finish(nativeKey,native);assert(pending.count(assistantKey)==1);finish(assistantKey,assistant);}
  else {finish(assistantKey,assistant);assert(pending.count(nativeKey)==1);finish(nativeKey,native);}
  assert(nativeResults==1 && assistantResults==0 && pending.empty());
  finish(nativeKey,native);finish(assistantKey,assistant); // duplicate completions stay inert
  assert(nativeResults==1 && assistantResults==0);
  // A late expired native fetch cannot consume a replacement generation.
  auto old=begin(nativeKey);auto replacement=loads.begin(nativeKey);
  finish(nativeKey,old);assert(pending.count(nativeKey)==1);
  finish(nativeKey,replacement);assert(nativeResults==2 && pending.empty());
 }
 assert(!fs_assistant::AsyncScope::current());
}
int main()
{
 checkProfileRequestOwnership();
 using namespace fs_assistant;
 assert(requiresAsyncAuthority("LLInventory","createFolder",false));
 assert(requiresAsyncAuthority("LLInventory","createItem",false));
 assert(!requiresAsyncAuthority("LLInventory","get",true));
 bool live=true;auto guard=std::make_shared<AsyncOperation>([&](){return live?std::string():std::string("revoked");});
 assert(!AsyncScope::current());
 AsyncTaskPtr task;
 {
  AsyncScope scope(guard);task=asyncTask();assert(asyncSubmit());
  {AsyncScope native({});assert(!AsyncScope::current());assert(asyncAllowed());}
  assert(AsyncScope::current()==guard);
 }
 guard->seal();assert(guard->state()=="pending");assert(task->claim("reply"));assert(!task->claim("reply"));
 guard->acknowledge("created-id");guard->acknowledge("created-id");assert(guard->ids().size()==1);
 live=false;{AsyncScope scope(task->guard());assert(!asyncSubmit());}
 live=true;assert(!guard->allowed());assert(guard->submitted()==1 && guard->state()=="partially_submitted");
 task.reset();assert(guard->pending()==0);
 auto before=std::make_shared<AsyncOperation>([](){return std::string("expired");});
 assert(!before->submit() && before->state()=="cancelled_before_submission");
 int calls=0;auto deferred=std::make_shared<AsyncOperation>([](){return std::string();});
 {
  AsyncScope scope(deferred);auto run=asyncDeferred([&](){++calls;assert(AsyncScope::current()==deferred);});
  run();run();assert(calls==1);deferred->seal();assert(deferred->state()=="pending");
 }
 assert(deferred->pending()==0 && deferred->state()=="completed");
 auto stop=std::make_shared<AsyncOperation>([](){return std::string();});
 {AsyncScope scope(stop);auto run=asyncDeferred([&](){++calls;});stop->cancel("logout");run();}
 assert(calls==1 && !AsyncScope::current());
 auto creation=std::make_shared<AsyncOperation>([](){return std::string();});AsyncTaskPtr createReply;
 {AsyncScope scope(creation);createReply=asyncTask();assert(asyncSubmit());}
 creation->seal();creation->cancel("paused");int permissionWrites=0,cleanups=0;
 auto callback=[&](bool allowed){++cleanups;if(allowed && asyncSubmit())++permissionWrites;};
 asyncDeliver(createReply,"new-item",callback);asyncDeliver(createReply,"new-item",callback);
 assert(cleanups==1 && permissionWrites==0 && creation->ids().front()=="new-item");
 assert(!AsyncScope::current());createReply.reset();assert(creation->pending()==0);
 bool restricted=false;auto outfit=std::make_shared<AsyncOperation>([](){return std::string();});
 outfit->checkWith([&](){return restricted?std::string("restriction_changed"):std::string();});
 AsyncOrigin origin;AsyncTaskPtr libraryCopy;
 {AsyncScope scope(outfit);libraryCopy=asyncTask();origin={outfit,asyncTask()};assert(asyncSubmit());}
 restricted=true;int wears=0;
 asyncDeliver(libraryCopy,"copied-outfit",[&](bool allowed){if(allowed && asyncSubmit())++wears;});
 restricted=false;assert(wears==0 && !outfit->allowed() && !asyncOriginAllowed(origin));
 libraryCopy.reset();origin.pending.reset();outfit.reset();assert(!asyncOriginAllowed(origin)); // Expired provenance must never become native/unscoped.
 auto other=std::make_shared<AsyncOperation>([](){return std::string();});
 {AsyncScope scope(other);assert(asyncSubmit());}assert(other->submitted()==1);
 assert(!AsyncScope::current()); // No source scope crosses a coroutine yield.
 // Work can wait in a native transport queue after the HTTP receipt is sealed.
 // Revocation at that boundary must not record a submission or set in-flight state.
 bool authorized=true;auto queued=std::make_shared<AsyncOperation>([&](){return authorized?std::string():std::string("revoked");});
 AsyncTaskPtr transport;
 {AsyncScope scope(queued);transport=asyncTask();}
 queued->seal();authorized=false;int packets=0,inflight=0;
 auto send=[&](){if(!asyncTaskSubmit(transport))return;inflight=1;++packets;};
 send();authorized=true;send();
 assert(packets==0 && inflight==0 && queued->submitted()==0 && queued->state()=="cancelled_before_submission");
 assert(!AsyncScope::current());transport.reset();assert(queued->pending()==0);
 // Independent intent remains usable after a stopped queued operation.
 auto newer=std::make_shared<AsyncOperation>([](){return std::string();});
 auto newerTask=std::make_shared<AsyncTask>(newer);
 assert(asyncTaskAllowed(newerTask) && asyncTaskSubmit(newerTask));
 asyncDeliver(newerTask,"result",[&](bool allowed){assert(allowed);++packets;});
 newer->seal();newerTask.reset();assert(packets==1 && newer->state()=="submitted");
 assert(!AsyncScope::current());
 // A createFolder request must retain Create authority while queued in AIS.
 for (bool pauseAccess : {false,true})
 {
  bool paused=false;auto gate=std::make_shared<ExecutionGate>(std::vector<std::string>{"create"});
  gate->observe({"allow"});
  auto folder=std::make_shared<AsyncOperation>([&](){return paused || !gate->allowed()?std::string("revoked"):std::string();});
  std::vector<std::function<void()>> aisQueue;int folderPosts=0,folderReplies=0;
  {
   AsyncScope request(requiresAsyncAuthority("LLInventory","createFolder",false)?folder:AsyncGuard());
   auto queuedTask=asyncTask();
   aisQueue.push_back([&,queuedTask]()
   {
    if(asyncTaskSubmit(queuedTask))++folderPosts;
    asyncDeliver(queuedTask,"",[&](bool allowed){assert(!allowed);++folderReplies;});
   });
  }
  folder->seal();
  if(pauseAccess)paused=true;else gate->observe({"deny"});
  for(auto& drain:aisQueue)drain();
  aisQueue.clear();
  paused=false;gate->observe({"allow"});
  assert(folderPosts==0 && folderReplies==1 && folder->submitted()==0 && folder->pending()==0);
  assert(folder->state()=="cancelled_before_submission" && !folder->allowed());
 }
 // An uncached outfit gesture must not activate when its asset arrives after
 // revocation. Load generations also protect a newer manual activation.
 AsyncLoads<std::string> gestures;AsyncTaskPtr gestureTask;
 bool wearAllowed=true;int activeGestures=0,activationPackets=0;bool placeholder=true;
 auto wear=std::make_shared<AsyncOperation>([&](){return wearAllowed?std::string():std::string("revoked");});
 {AsyncScope request(wear);gestureTask=gestures.begin("outfit-gesture");}
 wear->seal();wearAllowed=false;
 auto completeGesture=[&](const AsyncTaskPtr& load)
 {
  AsyncScope completion(load->guard());
  if(!gestures.finish("outfit-gesture",load))return;
  if(!asyncAllowed()){placeholder=false;return;}
  ++activeGestures;
  if(asyncSubmit())++activationPackets;
 };
 completeGesture(gestureTask);wearAllowed=true;
 assert(!placeholder && activeGestures==0 && activationPackets==0 && !wear->allowed());
 gestureTask.reset();assert(wear->pending()==0 && wear->state()=="cancelled_before_submission");
 // Replace a pending stopped load with a native load, then deliver old first.
 {AsyncScope request(wear);gestureTask=gestures.begin("outfit-gesture");}
 gestures.erase("outfit-gesture");
 auto manualLoad=gestures.begin("outfit-gesture");placeholder=true;
 completeGesture(gestureTask);assert(placeholder && activeGestures==0 && activationPackets==0);
 completeGesture(manualLoad);assert(activeGestures==1 && activationPackets==1);
 completeGesture(manualLoad);assert(activeGestures==1 && activationPackets==1);
 gestureTask.reset();manualLoad.reset();assert(wear->pending()==0 && !AsyncScope::current());
 std::cout<<"Durable assistant async guard checks passed\n";
}
