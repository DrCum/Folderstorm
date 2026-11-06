// Opt-in measurements only. Never changes a production pool or imposes timing pass criteria.
#include "scheduler_probe.h"
#include <algorithm>
#include <iomanip>
#if LL_WINDOWS
#include <mmsystem.h>
#endif

namespace FSSchedulerProbe
{
struct TimerPeriod
{
    unsigned period=0;
    explicit TimerPeriod(bool requested)
    {
#if LL_WINDOWS
        if(requested)
        {
            TIMECAPS caps;
            if(timeGetDevCaps(&caps,sizeof(caps))==TIMERR_NOERROR)
            {
                const unsigned value=caps.wPeriodMin>1?caps.wPeriodMin:1;
                if(value<=caps.wPeriodMax&&timeBeginPeriod(value)==TIMERR_NOERROR)period=value;
            }
        }
#endif
    }
    ~TimerPeriod()
    {
#if LL_WINDOWS
        if(period)timeEndPeriod(period);
#endif
    }
};
void row(const char* phase,Mode mode,unsigned width,const std::string& timer,const char* wait,
    unsigned sample,U64 units,U64 wall,Cpu cpu,unsigned admitted,unsigned rejected,U64 checksum,const char* status)
{
    std::cout<<phase<<','<<name(mode)<<','<<width<<','<<timer<<','<<wait<<','<<sample<<','<<units<<','
        <<wall<<','<<cpu.ticks<<','<<cpu.cycles<<','<<admitted<<','<<rejected<<','<<checksum<<','<<status<<'\n'<<std::flush;
}
int measure(Mode mode,unsigned width,bool request,bool timed,unsigned seconds)
{
    Watchdog watchdog;TimerPeriod period(request);
    const auto timer=request?(period.period?"requested"+std::to_string(period.period)+"ms":"request_unavailable"):"default";
    const auto wait=timed?"finite_fiber_wait":"empty_fifo";
    Pool<> pool(mode,width,512);pool.startChecked();warm(pool);
    std::cout<<"phase,mode,width,timer,wait,sample,units,wall_ns,cpu_100ns,thread_cycles,admitted,rejected,checksum,status\n"<<std::flush;
    watchdog.phase("wake measurement");
    for(unsigned sample=0;sample<64;++sample)
    {
        std::this_thread::sleep_for(5ms);
        auto signal=std::make_shared<std::promise<Clock::time_point>>();auto reached=signal->get_future();
        const auto begin=Clock::now();
        const bool accepted=pool.getQueue().tryPost(pool.checked([=]{signal->set_value(Clock::now());}));
        if(!accepted){row("wake",mode,width,timer,wait,sample,0,0,{},0,1,0,"admission_rejected");continue;}
        const auto called=await(reached,"Wake measurement callback timeout");
        row("wake",mode,width,timer,wait,sample,1,std::chrono::duration_cast<std::chrono::nanoseconds>(called-begin).count(),{},1,0,0,"ok");
    }
    watchdog.phase("active measurement");
    constexpr unsigned count=4096;
    auto sum=std::make_shared<std::atomic<U64>>(0);auto hits=std::make_shared<std::array<std::atomic<unsigned>,count>>();
    auto remaining=std::make_shared<std::atomic<unsigned>>(count);
    auto complete=std::make_shared<std::promise<void>>();
    auto done=complete->get_future();U64 expected=0;
    for(unsigned i=0;i<count;++i)expected+=work(i);
    const auto activeCpu=pool.cpu();const auto activeStart=Clock::now();
    for(unsigned i=0;i<count;++i)
        require(pool.getQueue().post(pool.checked([=]{
            sum->fetch_add(work(i));++(*hits)[i];if(remaining->fetch_sub(1)==1)complete->set_value();
        })),"Controlled active-work admission failed");
    await(done,"Active work did not finish");
    const auto activeWall=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-activeStart).count();
    const auto activeUsed=pool.cpu()-activeCpu;
    require(sum->load()==expected,"Active-work checksum changed");
    for(unsigned i=0;i<count;++i)require((*hits)[i].load()==1,"Active work lost or duplicated");
    row("active",mode,width,timer,wait,0,count,activeWall,activeUsed,count,0,sum->load(),"ok");

    auto stop=std::make_shared<std::atomic<bool>>(false);auto pulses=std::make_shared<std::atomic<U64>>(0);
    if(timed)
    {
        auto arrived=std::make_shared<std::atomic<unsigned>>(0);
        auto ready=std::make_shared<std::promise<void>>();auto entered=ready->get_future();
        for(unsigned i=0;i<width;++i)
            require(pool.getQueue().post(pool.checked([=]{
                if(arrived->fetch_add(1)+1==width)ready->set_value();
                while(!stop->load()){boost::this_fiber::sleep_for(20ms);++*pulses;}
            })),"Finite-wait control rejected");
        await(entered,"Finite-wait workers did not start");
    }
    std::this_thread::sleep_for(50ms);
    watchdog.phase("sustained idle measurement",std::chrono::seconds(seconds+5));
    const auto idleCpu=pool.cpu();const auto idleStart=Clock::now();const auto pulseStart=pulses->load();
    bool capped=false;
    do
    {
        std::this_thread::sleep_for(1s);
        const auto used=pool.cpu()-idleCpu;
        const auto wall=std::chrono::duration<double>(Clock::now()-idleStart).count();
        // Stop an unexpectedly busy private experiment promptly; this is a resource cap, not an adoption threshold.
        if(double(used.ticks)/10000000.0>wall*width*.25){capped=true;break;}
    }while(Clock::now()-idleStart<std::chrono::seconds(seconds));
    const auto idleWall=std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-idleStart).count();
    const auto idleUsed=pool.cpu()-idleCpu;const auto completedPulses=pulses->load()-pulseStart;
    stop->store(true);watchdog.phase("measurement close/drain");pool.close();pool.noErrors();
    row("idle",mode,width,timer,wait,0,completedPulses,idleWall,idleUsed,0,0,0,capped?"resource_budget_exceeded":"ok");
    return 0;
}
}
int main(int argc,char** argv)
{
    if(argc==1){std::cout<<"Opt in: --case current|stock 1|3|8 default|request empty|timed 1..30\n";return 0;}
    if(argc!=7||std::string(argv[1])!="--case")return 1;
    const std::string mode=argv[2],timer=argv[4],wait=argv[5];char* end=nullptr;
    const auto width=std::strtoul(argv[3],&end,10);if(!end||*end||(width!=1&&width!=3&&width!=8))return 1;
    const auto seconds=std::strtoul(argv[6],&end,10);if(!end||*end||seconds<1||seconds>30)return 1;
    if((mode!="current"&&mode!="stock")||(timer!="default"&&timer!="request")||(wait!="empty"&&wait!="timed"))return 1;
#if !LL_WINDOWS
    std::cout<<"SKIP Windows CPU/timer experiment; run the separate portable correctness target.\n";return 0;
#else
    try{return FSSchedulerProbe::measure(mode=="current"?FSSchedulerProbe::Mode::Current:FSSchedulerProbe::Mode::Stock,
        unsigned(width),timer=="request",wait=="timed",unsigned(seconds));}
    catch(const std::exception& error){FSSchedulerProbe::fail(error.what());}
#endif
}
