#include "fsfileuploadmodel.h"
#include "fsassistantasync.h"
#include "fsassistantpolicy.h"
#include <cassert>
int main()
{
    namespace model=fs_file_upload;
    assert(model::folder("") && model::folder("Sounds/Subfolder"));
    for(const auto* bad:{"..","/a","a/","a//b","a/../b","a\\b","a:b","a/./b","a\nb","a/b/c/d/e/f/g/h/i"})assert(!model::folder(bad));
    assert(model::hash(std::string(64,'a')));assert(!model::hash(std::string(64,'z')));
    assert(model::name("Hello") && !model::name("") && !model::name("Bad\nname"));
    assert(model::quoteAccepted(0,0)); // Free Premium uploads are real quotes.
    assert(!model::quoteAccepted(0,10));assert(!model::quoteAccepted(10,-1));assert(!model::quoteAccepted(-1,0));
    assert(model::quoteAccepted(10,0));
    assert(model::serverQuoteAccepted(true,true,true,0,0));
    assert(!model::serverQuoteAccepted(true,false,false,0,0));
    assert(!model::serverQuoteAccepted(true,true,false,0,0));
    assert(!model::serverQuoteAccepted(true,true,true,0,10));
    assert(model::serverQuoteAccepted(false,false,false,0,0));
    assert(fs_assistant::decide({"allow","allow"},0)==fs_assistant::Decision::Allow);
    assert(fs_assistant::decide({"allow","allow"},10)==fs_assistant::Decision::Ask);
    assert(fs_assistant::decide({"ask","allow"},0)==fs_assistant::Decision::Ask);
    assert(fs_assistant::decide({"deny","allow"},0)==fs_assistant::Decision::Deny);
    model::Progress p;assert(p.start());assert(!p.start());assert(!p.acknowledge(1));
    p.sourceSubmitted=true;p.stop();assert(p.sourceSubmitted && !p.acknowledge(0) && !p.start());
    model::Progress done;assert(done.start());assert(done.acknowledge(0));assert(!done.acknowledge(0));assert(done.complete(1));assert(!done.start());
    bool allowed=true;auto guard=std::make_shared<fs_assistant::AsyncOperation>([&](){return allowed?std::string():"revoked";});
    auto task=std::make_shared<fs_assistant::AsyncTask>(guard);
    assert(fs_assistant::asyncTaskSubmit(task));allowed=false;
    assert(!fs_assistant::asyncTaskSubmit(task));assert(guard->submitted()==1);
    assert(fs_assistant::requiresAsyncAuthority("FSUpload","execute",false));
}
