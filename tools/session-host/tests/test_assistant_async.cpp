/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantasync.h"
#include <cassert>
#include <iostream>
int main()
{
 using namespace fs_assistant;
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
 std::cout<<"Durable assistant async guard checks passed\n";
}
