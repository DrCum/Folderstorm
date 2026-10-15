/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_ASYNC_H
#define FS_ASSISTANT_ASYNC_H
#include <functional>
#include <algorithm>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs_assistant
{
// Viewer-main-thread state. Authority is supplied by the owning bridge, never
// deserialized from a tool request. A failed check permanently stops this work.
class AsyncOperation
{
public:
    using Authority=std::function<std::string()>;
    explicit AsyncOperation(Authority authority):mAuthority(std::move(authority)){}
    bool allowed()
    {
        if(!mStopped.empty())return false;
        const auto reason=mAuthority?mAuthority():std::string("missing_authority");
        if(!reason.empty())cancel(reason);
        return mStopped.empty();
    }
    void cancel(const std::string& reason){if(mStopped.empty())mStopped=reason.empty()?"cancelled":reason;}
    bool submit(){if(!allowed())return false;++mSubmitted;return true;}
    void acknowledge(const std::string& id)
    {if(!id.empty() && mIds.size()<128 && std::find(mIds.begin(),mIds.end(),id)==mIds.end())mIds.push_back(id);}
    void begin(){if(++mPending>512)cancel("callback_capacity");}
    void end(){if(mPending)--mPending;}
    void seal(){mSealed=true;}
    std::string state()
    {
        allowed();
        if(!mStopped.empty())return mSubmitted?"partially_submitted":"cancelled_before_submission";
        if(!mSealed || mPending)return "pending";
        // Native fire-and-forget writes do not prove final simulator state.
        return mSubmitted?"submitted":"completed";
    }
    const std::string& reason() const{return mStopped;}
    unsigned int pending() const{return mPending;}
    unsigned int submitted() const{return mSubmitted;}
    const std::vector<std::string>& ids() const{return mIds;}
private:
    Authority mAuthority;std::string mStopped;std::vector<std::string> mIds;
    unsigned int mPending=0,mSubmitted=0;bool mSealed=false;
};
using AsyncGuard=std::shared_ptr<AsyncOperation>;

class AsyncScope
{
public:
    explicit AsyncScope(AsyncGuard guard):mPrevious(current()){current()=std::move(guard);}
    ~AsyncScope(){current()=std::move(mPrevious);}
    AsyncScope(const AsyncScope&)=delete;
    AsyncScope& operator=(const AsyncScope&)=delete;
    static AsyncGuard& current(){static thread_local AsyncGuard guard;return guard;}
private:AsyncGuard mPrevious;
};
inline bool asyncAllowed(){const auto& guard=AsyncScope::current();return !guard || guard->allowed();}
inline bool asyncSubmit(){const auto& guard=AsyncScope::current();return !guard || guard->submit();}

// Captured before scheduling, so even destructor-initiated work retains its
// original authority. Copies share one lease; duplicate deliveries do not run.
class AsyncTask
{
public:
    explicit AsyncTask(AsyncGuard guard=AsyncScope::current()):mGuard(std::move(guard)){if(mGuard)mGuard->begin();}
    AsyncTask(const AsyncTask&)=delete;
    AsyncTask& operator=(const AsyncTask&)=delete;
    ~AsyncTask(){if(mGuard)mGuard->end();}
    const AsyncGuard& guard() const{return mGuard;}
    bool claim(const std::string& key)
    {if(!mGuard)return true;
        if(mSeen.size()>=128){mGuard->cancel("callback_capacity");return false;}
        return mSeen.insert(key).second;}
private:AsyncGuard mGuard;std::set<std::string> mSeen;
};
using AsyncTaskPtr=std::shared_ptr<AsyncTask>;
inline AsyncTaskPtr asyncTask(){return std::make_shared<AsyncTask>();}
template<typename Function> void asyncDeliver(const AsyncTaskPtr& task,const std::string& key,Function function)
{
    AsyncScope scope(task?task->guard():AsyncGuard());
    if(task && !task->claim(key))return;
    if(task && task->guard())task->guard()->acknowledge(key);
    function(asyncAllowed()); // Failure still reaches cleanup under stopped scope.
}
template<typename Function> auto asyncDeferred(Function function)
{
    auto task=asyncTask();
    return [task,function=std::move(function)]() mutable
    {
        AsyncScope scope(task->guard());
        if(task->claim("run") && asyncAllowed())function();
    };
}
}
#endif
