#include "scheduler_probe.h"
#include <boost/fiber/fiber.hpp>

namespace FSSchedulerProbe
{
void burst(Mode mode,unsigned width)
{
    constexpr unsigned count=256;
    auto hits=std::make_shared<std::array<std::atomic<unsigned>,count>>();
    auto sum=std::make_shared<std::atomic<U64>>(0);auto wrong=std::make_shared<std::atomic<unsigned>>(0);
    auto order=std::make_shared<std::atomic<unsigned>>(0);const auto main=std::this_thread::get_id();
    Pool<> pool(mode,width,512);pool.startChecked();warm(pool);
    for(unsigned i=0;i<count;++i)
        require(pool.getQueue().post(pool.checked([=]{
            if(std::this_thread::get_id()==main)++*wrong;
            if(width==1&&order->fetch_add(1)!=i)++*wrong;
            ++(*hits)[i];sum->fetch_add(work(i));
        })),"Burst admission failed");
    pool.close();pool.noErrors();
    U64 expected=0;
    for(unsigned i=0;i<count;++i){require((*hits)[i].load()==1,"Burst work was lost or duplicated");expected+=work(i);}
    require(sum->load()==expected&&!wrong->load(),"Burst result, thread or FIFO order changed");
    require(!pool.getQueue().tryPost([]{}),"Closed pool accepted new work");
}
void fullAndWake(Mode mode)
{
    Pool<> pool(mode,1,4);pool.startChecked();warm(pool);
    auto entered=std::make_shared<std::promise<void>>(),release=std::make_shared<std::promise<void>>();
    auto ready=entered->get_future();auto gate=release->get_future().share();
    require(pool.getQueue().post(pool.checked([=]{entered->set_value();gate.wait();})),"Blocker admission failed");
    await(ready,"Blocker did not run");
    auto done=std::make_shared<std::atomic<unsigned>>(0);
    for(unsigned i=0;i<4;++i)require(pool.getQueue().tryPost(pool.checked([=]{++*done;})),"Known-free private slot rejected");
    require(!pool.getQueue().tryPost([]{}),"Full private queue accepted more work");
    release->set_value();
    auto marker=std::make_shared<std::promise<void>>();auto drained=marker->get_future();
    require(pool.getQueue().post(pool.checked([=]{marker->set_value();})),"Drain marker rejected");
    await(drained,"Full queue failed to drain");require(done->load()==4,"Queued work did not finish exactly once");
    for(unsigned i=0;i<64;++i)
    {
        auto signal=std::make_shared<std::promise<void>>();auto reached=signal->get_future();
        require(pool.getQueue().post(pool.checked([=]{signal->set_value();})),"Wake-transition post rejected");
        await(reached,"Empty-to-ready notification was lost");
        if(i%8==0)std::this_thread::sleep_for(1ms);
    }
    pool.close();pool.noErrors();
}
void timed(Mode mode)
{
    Pool<LL::WorkSchedule> pool(mode,1,16);pool.startChecked();warm(pool);
    auto late=std::make_shared<std::promise<Clock::time_point>>(),early=std::make_shared<std::promise<Clock::time_point>>();
    auto delayed=late->get_future(),immediate=early->get_future();
    const auto deadline=Clock::now()+500ms;
    require(pool.getQueue().post(pool.checked([=]{late->set_value(Clock::now());}),deadline),"Timed work rejected");
    std::this_thread::sleep_for(5ms);
    require(Clock::now()<deadline,"Host stalled before earlier-work test");
    require(pool.getQueue().post(pool.checked([=]{early->set_value(Clock::now());})),"Earlier immediate work rejected");
    const auto first=await(immediate,"Earlier work did not interrupt a future wait");
    const auto second=await(delayed,"Timed work was lost");
    require(first<second&&second>=deadline,"Timed work ran early or kept earlier work behind it");
    auto drained=std::make_shared<std::atomic<unsigned>>(0);
    require(pool.getQueue().post(pool.checked([=]{++*drained;}),Clock::now()+10ms),"Timed drain admission failed");
    pool.close();pool.noErrors();require(drained->load()==1,"Close failed to drain accepted timed work");
}
void fibers(Mode mode,unsigned width)
{
    Pool<> pool(mode,width,16);pool.startChecked();warm(pool);
    auto complete=std::make_shared<std::promise<bool>>();auto result=complete->get_future();
    require(pool.getQueue().post(pool.checked([=]{
        const auto worker=std::this_thread::get_id();std::array<unsigned,2> counts{};std::array<bool,2> progress{};bool valid=true;
        auto body=[&](unsigned id){
            for(unsigned i=0;i<32;++i){++counts[id];boost::this_fiber::yield();}
            progress[id]=counts[1-id]>0;
            const auto deadline=Clock::now()+2ms;
            boost::this_fiber::sleep_until(deadline);
            valid=valid&&Clock::now()>=deadline&&std::this_thread::get_id()==worker;
        };
        boost::fibers::fiber a([&]{body(0);}),b([&]{body(1);});a.join();b.join();
        complete->set_value(valid&&counts[0]==32&&counts[1]==32&&progress[0]&&progress[1]);
    })),"Fiber work rejected");
    require(await(result,"Cooperating fibers did not finish"),"Fiber progress, timer or thread affinity changed");
    pool.close();pool.noErrors();
}
struct Payload
{
    unsigned id;
    std::shared_ptr<std::array<std::atomic<unsigned>,256>> hits;
    Payload(unsigned n,std::shared_ptr<std::array<std::atomic<unsigned>,256>> state):id(n),hits(std::move(state)){}
};
void producerClose(Mode mode,unsigned width)
{
    auto hits=std::make_shared<std::array<std::atomic<unsigned>,256>>();
    std::array<bool,256> accepted{};std::array<std::weak_ptr<Payload>,256> leases;
    Pool<> pool(mode,width,64);pool.startChecked();warm(pool);
    auto half=std::make_shared<std::promise<void>>(),release=std::make_shared<std::promise<void>>();
    auto ready=half->get_future();auto gate=release->get_future().share();
    std::thread producer([&]{
        for(unsigned i=0;i<256;++i)
        {
            if(i==128){half->set_value();gate.wait();}
            auto item=std::make_shared<Payload>(i,hits);leases[i]=item;
            auto callback=pool.checked([item]{++(*item->hits)[item->id];});
            // Guarantee accepted-work coverage before closing; later contention is valid rejection.
            accepted[i]=i==0?pool.getQueue().post(callback):pool.getQueue().tryPost(callback);
        }
    });
    await(ready,"Producer did not reach close boundary");release->set_value();pool.close();producer.join();pool.noErrors();
    unsigned admitted=0;
    for(unsigned i=0;i<256;++i)
    {
        admitted+=accepted[i];require((*hits)[i].load()==unsigned(accepted[i]),"Close race lost, duplicated or ran rejected work");
        require(leases[i].expired(),"Callback payload outlived pool close/join");
    }
    require(admitted>0&&!pool.getQueue().tryPost([]{}),"Close/admission coverage missing");
}
int correctness()
{
    Watchdog watchdog;
    for(auto mode:{Mode::Current,Mode::Stock})
    {
        for(unsigned width:{1u,3u,8u})
        {
            watchdog.phase("burst");burst(mode,width);
            watchdog.phase("fibers");fibers(mode,width);
            watchdog.phase("producer/close");producerClose(mode,width);
        }
        watchdog.phase("full/empty transitions");fullAndWake(mode);
        watchdog.phase("scheduled deadlines");timed(mode);
    }
    std::cout<<checks<<" private current/stock queue, fiber, lifetime and shutdown checks passed\n";
    return 0;
}
}
int main(int argc,char**)
{
    if(argc!=1)return 1;
    try{return FSSchedulerProbe::correctness();}catch(const std::exception& error){FSSchedulerProbe::fail(error.what());}
}
