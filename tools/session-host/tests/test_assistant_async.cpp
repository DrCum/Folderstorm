/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantasync.h"
#include "fsassistantpolicy.h"
#include <cassert>
#include <iostream>
int main()
{
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
 std::cout<<"Durable assistant async guard checks passed\n";
}
