// Test-only private pools. The viewer's scheduler and registered pools are untouched.
#ifndef FS_SCHEDULER_PROBE_H
#define FS_SCHEDULER_PROBE_H
#include "linden_common.h"
#include "threadpool.h"
#include <boost/fiber/algo/round_robin.hpp>
#include <boost/fiber/operations.hpp>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#if LL_WINDOWS
#include <windows.h>
#endif

namespace FSSchedulerProbe
{
using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
inline unsigned checks=0;
[[noreturn]] inline void fail(const char* message)
{
    std::cerr<<"Scheduler probe failed: "<<message<<std::endl;
    std::_Exit(2); // A failed private test must not hang in a blocked pool destructor.
}
inline void require(bool ok,const char* message){++checks;if(!ok)fail(message);}
class Watchdog
{
    std::atomic<bool> done{false};
    std::atomic<Clock::rep> deadline{0};
    std::atomic<const char*> stage{"startup"};
    std::thread thread;
    static Clock::rep now(){return Clock::now().time_since_epoch().count();}
public:
    void phase(const char* literal,std::chrono::seconds budget=10s)
    {
        stage.store(literal);deadline.store((Clock::now()+budget).time_since_epoch().count());
    }
    Watchdog()
    {
        phase("startup");
        thread=std::thread([this]{while(!done.load()){
            if(now()>deadline.load())fail(stage.load());
            std::this_thread::sleep_for(25ms);
        }});
    }
    ~Watchdog(){done.store(true);thread.join();}
};
enum class Mode {Current,Stock};
inline const char* name(Mode mode){return mode==Mode::Current?"current":"stock";}
inline std::string poolName(Mode mode,unsigned width)
{
    static unsigned next=0;
    return "FSPrivateWait-"+std::string(name(mode))+"-"+std::to_string(width)+"-"+std::to_string(++next);
}
struct Cpu
{
    U64 ticks=0,cycles=0;
    Cpu operator-(const Cpu& first) const{return {ticks-first.ticks,cycles-first.cycles};}
};
template<class Queue=LL::WorkQueue>
class Pool final : public LL::ThreadPoolUsing<Queue>
{
    using Base=LL::ThreadPoolUsing<Queue>;
    Mode mode;
    unsigned requested;
    std::shared_ptr<std::atomic<unsigned>> errors=std::make_shared<std::atomic<unsigned>>(0);
public:
    Pool(Mode variant,unsigned width,unsigned capacity=1024):Base(poolName(variant,width),width,capacity,false),mode(variant),requested(width)
    {
        require(LL::ThreadPoolBase::getConfiguredWidth(this->getQueue().getKey(),width)==width,"Unexpected private-pool width override");
    }
    ~Pool() override{this->close();}
    void startChecked(){Base::start();require(this->getWidth()==requested,"Private-pool width mismatch");}
    void run() override
    {
        if(mode==Mode::Stock)boost::fibers::use_scheduling_algorithm<boost::fibers::algo::round_robin>();
        Base::run();
    }
    template<class Fn> LL::WorkQueueBase::Work checked(Fn fn)
    {
        return [fn=std::move(fn),state=errors]() noexcept {try{fn();}catch(...){++*state;}};
    }
    void noErrors(){require(errors->load()==0,"Worker test exception escaped its checked body");}
    Cpu cpu()
    {
        Cpu out;
#if LL_WINDOWS
        for(auto& worker:this->mThreads)
        {
            FILETIME created,exited,kernel,user;ULONG64 cycles=0;
            require(GetThreadTimes(worker.second.native_handle(),&created,&exited,&kernel,&user)!=0,"GetThreadTimes failed");
            require(QueryThreadCycleTime(worker.second.native_handle(),&cycles)!=0,"QueryThreadCycleTime failed");
            out.ticks+=((U64(kernel.dwHighDateTime)<<32)|kernel.dwLowDateTime)+((U64(user.dwHighDateTime)<<32)|user.dwLowDateTime);
            out.cycles+=cycles;
        }
#endif
        return out;
    }
};
template<class T> T await(std::future<T>& future,const char* message)
{
    require(future.wait_for(5s)==std::future_status::ready,message);return future.get();
}
inline void await(std::future<void>& future,const char* message)
{
    require(future.wait_for(5s)==std::future_status::ready,message);future.get();
}
template<class Queue> void warm(Pool<Queue>& pool)
{
    auto entered=std::make_shared<std::promise<void>>(),finished=std::make_shared<std::promise<void>>();
    auto ready=entered->get_future(),done=finished->get_future();
    auto release=std::make_shared<std::promise<void>>();auto released=release->get_future().share();
    auto arrived=std::make_shared<std::atomic<unsigned>>(0),completed=std::make_shared<std::atomic<unsigned>>(0);
    const auto width=pool.getWidth();
    for(unsigned i=0;i<width;++i)
        require(pool.getQueue().post(pool.checked([=]{
            if(arrived->fetch_add(1)+1==width)entered->set_value();
            released.wait();
            if(completed->fetch_add(1)+1==width)finished->set_value();
        })),"Warmup admission failed");
    await(ready,"Not all private workers reached warmup");release->set_value();await(done,"Warmup did not finish");
    pool.noErrors();std::this_thread::sleep_for(20ms);
}
inline U64 work(U64 value)
{
    for(unsigned i=0;i<256;++i)value=(value^(value>>17))*U64(6364136223846793005)+i;
    return value;
}
}
#endif
