/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "llviewerprecompiledheaders.h"
#include "fsklipyclient.h"
#include "llagent.h"
#include "llapp.h"
#include "llcallbacklist.h"
#include "lldir.h"
#include "llproxy.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#if __has_include("fsklipyconfig.h")
#include "fsklipyconfig.h"
#else
#define FS_KLIPY_APP_KEY ""
#endif
#include <cstdlib>
#include <deque>
#include <future>
#include <chrono>
namespace
{
using Clock=std::chrono::steady_clock;
struct Scope
{
    LLUUID account,login;std::string grid;
    bool valid()const {return !LLApp::isExiting()&&!gDisconnected&&LLStartUp::getStartupState()==STATE_STARTED&&account==gAgent.getID()&&login==gAgent.getSessionID()&&grid==LLGridManager::getInstance()->getGrid();}
    bool operator==(const Scope& b)const{return account==b.account&&login==b.login&&grid==b.grid;}
};
Scope scope(){return {gAgent.getID(),gAgent.getSessionID(),LLGridManager::getInstance()->getGrid()};}
struct Config{bool enabled=true;std::string key;bool operator==(const Config& b)const{return enabled==b.enabled&&key==b.key;}};
Config saved()
{
    auto key=gSavedSettings.getString("FSKlipyAppKey");
    if(key.empty())if(const auto* value=std::getenv("FOLDERSTORM_KLIPY_APP_KEY"))key=value;
    if(key.empty())key=FS_KLIPY_APP_KEY;
    return {gSavedSettings.getBOOL("FSKlipyEnabled"),key};
}
std::string customer()
{
    auto value=gSavedPerAccountSettings.getLLSD("FSKlipyCustomer");
    if(value["account"].asUUID()!=gAgent.getID()||value["grid"].asString()!=LLGridManager::getInstance()->getGrid()||value["id"].asUUID().isNull())
    {value=LLSD();value["account"]=gAgent.getID();value["grid"]=LLGridManager::getInstance()->getGrid();value["id"]=LLUUID::generateNewID();gSavedPerAccountSettings.setLLSD("FSKlipyCustomer",value);}
    return value["id"].asUUID().asString();
}
struct Intent{fs_klipy::Request request;unsigned generation=0;bool append=false;};
struct Job
{
    Scope owner;unsigned generation=0,page=0;bool share=false,append=false,categories=false;
    std::shared_ptr<std::atomic<bool>> cancelled=std::make_shared<std::atomic<bool>>(false);
    std::future<fs_klipy::Result> work;
};
struct Share{Scope owner;std::string slug,query,reason;};
class Client
{
public:
    Config config=saved();Scope owner=scope();FSKlipy::Snapshot state;
    std::vector<std::unique_ptr<Job>> jobs;
    std::deque<Share> shares;
    std::optional<Intent> intent;
    unsigned generation=0;bool running=false;
    Clock::time_point due{},backoff{};
    bool categoriesRequested=false;
    bool browsing=false;
    static void idle(void* p){static_cast<Client*>(p)->poll();}
    void start(){if(!running){gIdleCallbacks.addFunction(idle,this);running=true;}}
    bool enabled()const{return config.enabled&&fs_klipy::keyValid(config.key)&&owner.valid()&&!(LLProxy::instanceExists()&&LLProxy::getInstance()->isHTTPProxyEnabled());}
    void cancel(bool all)
    {
        ++generation;intent.reset();for(auto& job:jobs)if(all||!job->share)*job->cancelled=true;
        browsing=false;if(state.categories.empty())categoriesRequested=false;
        if(all){shares.clear();categoriesRequested=false;const auto revision=state.revision+1;state={};state.revision=revision;backoff={};}else{state.loading=false;state.items.clear();state.more=false;state.page=0;state.error.clear();++state.revision;}
    }
    void poll()
    {
        if(!(owner==scope())){cancel(true);owner=scope();}
        if(!enabled())
        {
            if(intent||!shares.empty()||state.loading||!state.items.empty()||std::any_of(jobs.begin(),jobs.end(),[](const auto& job){return !*job->cancelled;}))cancel(true);
            const std::string error=!owner.valid()?"Log in to search KLIPY.":!config.enabled?"KLIPY search is disabled.":!fs_klipy::keyValid(config.key)?"KLIPY search is not configured.":"KLIPY is unavailable with the current proxy settings.";
            if(state.error!=error){state.error=error;++state.revision;}
        }
        for(auto at=jobs.begin();at!=jobs.end();)
        {
            auto& job=**at;
            if(job.work.wait_for(std::chrono::seconds(0))!=std::future_status::ready){++at;continue;}
            fs_klipy::Result result;try{result=job.work.get();}catch(...){result.error="KLIPY request failed.";}
            if(!*job.cancelled&&job.owner.valid()&&enabled())
            {
                if(result.retryAfter)backoff=Clock::now()+std::chrono::seconds(result.retryAfter);
                if(job.categories){state.categories=std::move(result.categories);++state.revision;}
                else if(!job.share&&job.generation==generation)
                {
                    state.loading=false;state.error=result.error;
                    if(result.error.empty())
                    {state.page=job.page;state.more=result.more;if(!job.append)state.items.clear();if(state.items.size()+result.items.size()<=1000)state.items.insert(state.items.end(),result.items.begin(),result.items.end());else{state.more=false;state.error="Refine the search to see more GIFs.";}if(!result.categories.empty())state.categories=std::move(result.categories);}
                    ++state.revision;
                }
            }
            at=jobs.erase(at);
        }
        if(!enabled()||jobs.size()>=2||Clock::now()<backoff)return;
        if(!shares.empty())
        {
            auto share=std::move(shares.front());shares.pop_front();if(!share.owner.valid())return;
            const auto url=share.reason.empty()?fs_klipy::shareURL(config.key,share.slug):fs_klipy::reportURL(config.key,share.slug);
            const auto body=share.reason.empty()?fs_klipy::shareBody(customer(),share.query):fs_klipy::reportBody(customer(),share.reason),ca=gDirUtilp->getCAFile();
            if(url.empty()||body.empty())return;
            auto job=std::make_unique<Job>();job->owner=share.owner;job->share=true;const auto cancel=job->cancelled;
            try{job->work=std::async(std::launch::async,[url,body,ca,cancel]{const auto response=fs_chat_media::klipyRequest(url,ca,body,*cancel);fs_klipy::Result result;if(response.status==429)result.retryAfter=response.retryAfter?response.retryAfter:60;return result;});jobs.push_back(std::move(job));}catch(...){}
            return; // Share delivery is best effort, never automatically retried.
        }
        if(!intent)
        {
            if(categoriesRequested||!browsing||generation==0)return;
            categoriesRequested=true;
            fs_klipy::Request request;request.key=config.key;request.customer=customer();request.kind=fs_klipy::Kind::Categories;
            const auto url=fs_klipy::requestURL(request),ca=gDirUtilp->getCAFile();auto job=std::make_unique<Job>();job->owner=owner;job->categories=true;const auto cancel=job->cancelled;
            try{job->work=std::async(std::launch::async,[url,ca,cancel]{return fs_klipy::parse(fs_chat_media::klipyRequest(url,ca,"",*cancel),fs_klipy::Kind::Categories);});jobs.push_back(std::move(job));}catch(...){}
            return;
        }
        if(Clock::now()<due)return;
        auto next=std::move(*intent);intent.reset();next.request.key=config.key;next.request.customer=customer();const auto url=fs_klipy::requestURL(next.request),ca=gDirUtilp->getCAFile();
        if(url.empty()){state.loading=false;state.error="Invalid KLIPY search request.";++state.revision;return;}
        auto job=std::make_unique<Job>();job->owner=owner;job->generation=next.generation;job->page=next.request.page;job->append=next.append;const auto cancel=job->cancelled;const auto kind=next.request.kind;
        try{job->work=std::async(std::launch::async,[url,ca,cancel,kind]{return fs_klipy::parse(fs_chat_media::klipyRequest(url,ca,"",*cancel),kind);});jobs.push_back(std::move(job));}
        catch(...){state.loading=false;state.error="Could not start KLIPY search.";++state.revision;}
    }
};
Client* active=nullptr;
Client& client(){if(!active)active=new Client;active->start();return *active;}
}
const FSKlipy::Snapshot& FSKlipy::snapshot(){auto& c=client();return c.state;}
bool FSKlipy::configured(){return client().enabled();}
void FSKlipy::browse(fs_klipy::Kind kind,const std::string& query,const std::vector<std::string>& slugs,unsigned page,bool append)
{
    auto& c=client();c.poll();if(!c.enabled())return;
    c.browsing=true;
    ++c.generation;for(auto& job:c.jobs)if(!job->share&&!job->categories)*job->cancelled=true;
    if(!append){c.state.items.clear();c.state.error.clear();c.state.more=false;c.state.page=0;++c.state.revision;}
    fs_klipy::Request request;request.kind=kind;request.query=query;request.slugs=slugs;request.page=page;
    c.intent=Intent{std::move(request),c.generation,append};c.state.loading=true;c.due=Clock::now()+std::chrono::milliseconds(350);
}
void FSKlipy::cancelBrowse(){if(active)active->cancel(false);}
void FSKlipy::acceptedShare(const std::string& slug,const std::string& query,const LLUUID& account,const LLUUID& login,const std::string& grid)
{auto& c=client();Scope owner{account,login,grid};if(c.enabled()&&owner.valid()&&fs_klipy::slugValid(slug)&&fs_gif_board::text(query,256)&&c.shares.size()<16)c.shares.push_back({owner,slug,query,""});}
bool FSKlipy::reportItem(const std::string& slug,const std::string& reason,const LLUUID& account,const LLUUID& login,const std::string& grid)
{auto& c=client();Scope owner{account,login,grid};if(!c.enabled()||!owner.valid()||!fs_klipy::slugValid(slug)||fs_klipy::reportBody("validation",reason).empty()||c.shares.size()>=16)return false;c.shares.push_back({owner,slug,"",reason});return true;}
void FSKlipy::preferencesOpening(){(void)client();}
void FSKlipy::applyPreferences()
{
    auto& c=client();auto next=saved();
    if(!(next==c.config))
    {
        const bool sameKey=next.key==c.config.key;const auto backoff=c.backoff;
        c.cancel(true);c.config=std::move(next);
        if(sameKey)c.backoff=backoff; // Toggling the feature cannot bypass Retry-After.
    }
}
void FSKlipy::shutdown()
{if(!active)return;active->cancel(true);if(active->running){gIdleCallbacks.deleteFunction(Client::idle,active);active->running=false;}for(auto& job:active->jobs)if(job->work.valid()){try{job->work.get();}catch(...){}}active->jobs.clear();}
