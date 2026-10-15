/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fscontentupload.h"
#include "fsassistantasync.h"
#include <cassert>
#include <iostream>
int main()
{
 using namespace fs_content;using namespace fs_assistant;
 const std::string stages[]={"queue","prepare","dequeued","capability","capability_result","source","source_result","commit"};
 for(unsigned cancel=0;cancel<8;++cancel)
 {
  bool allowed=true;auto guard=std::make_shared<AsyncOperation>([&](){return allowed?std::string():std::string("revoked");});
  auto task=std::make_shared<AsyncTask>(guard);UploadProgress progress;unsigned submits=0;
  for(unsigned i=0;i<8;++i)
  {
   if(i==cancel)allowed=false;
   const bool submission=stages[i]=="capability" || stages[i]=="source";
   const bool sent=progress.before(stages[i],submission,[&](){return asyncTaskAllowed(task);},[&](){++submits;return asyncTaskSubmit(task);});
   assert(sent==(i<cancel));
  }
  assert(submits==(cancel>5?2u:cancel>3?1u:0u));
  allowed=true;assert(!progress.before("source",true,[](){return true;},[](){return true;}));
  assert(progress.finish());assert(!progress.finish());
  const auto outcome=progress.outcome(false,false);assert(!outcome.sourceSaved);assert(outcome.sourceSavedKnown==(cancel<=5));
  assert(outcome.state==(cancel>5?"submitted_unknown":"cancelled_before_source_submission"));
 }
 UploadProgress completed;unsigned calls=0;
 for(const auto& stage:stages)assert(completed.before(stage,stage=="capability" || stage=="source",[](){return true;},[&](){++calls;return true;}));
 assert(calls==2);assert(!completed.before("source",true,[](){return true;},[&](){++calls;return true;}));assert(calls==2);
 assert(completed.finish());auto receipt=completed.outcome(true,true);assert(receipt.sourceSaved && receipt.sourceSavedKnown && !receipt.uncertain && receipt.state=="server_confirmed");
 auto late=completed.outcome(true,false);assert(late.sourceSaved && late.uncertain && late.state=="server_confirmed_local_conflict");
 assert(uploadRetryLimit(true)==0 && uploadRetryLimit(false)==2);
 std::cout<<"content upload boundary checks passed\n";
}
