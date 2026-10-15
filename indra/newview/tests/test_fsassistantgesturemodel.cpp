/** SPDX-License-Identifier: LGPL-2.1-or-later */
#include "fsassistantgesturemodel.h"
#include "fsassistantasync.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <limits>
using namespace fs_assistant_gesture;
const std::string animation="11111111-1111-4111-8111-111111111111";
std::string asset(const std::string& count,const std::string& body)
{return "2\n255\n0\n/trigger\n\n"+count+"\n"+body;}
std::string animationAsset(const std::string& emote)
{
    std::string result(41,'\0');result[0]=1;result[4]=3;
    const float duration=5;std::uint32_t bits;std::memcpy(&bits,&duration,sizeof(bits));
    for(unsigned i=0;i<4;++i)result[8+i]=static_cast<char>((bits>>(i*8))&255);
    result.insert(12,emote);return result;
}
int main(int argc,char** argv)
{
    Plan plan;std::string error;
    const auto good=asset("3","0\nAnimation\n"+animation+"\n0\n3\n5\n1\n2\n/tp literal text\n0\n");
    assert(decode(good,plan,error));assert(plan.steps.size()==3 && plan.chat && !plan.sound);
    assert(plan.steps[2].value=="/tp literal text"); // configurable commands remain literal data
    for(const auto& bad:std::vector<std::string>{
        asset("2147483647",""),asset("-1",""),asset("65",""),asset("nan",""),
        asset("1","3\n61\n1\n"),asset("1","3\n1\n4\n"),asset("1","3\ninf\n1\n"),
        asset("1","0\nforeign stop\n"+animation+"\n1\n"),
        asset("1","1\nsound\nnot-a-uuid\n0\n"),asset("1","2\nhello\n1\n"),
        good+"extra\n",good.substr(0,good.size()-1),std::string(16385,'x'),good+std::string("\0trailing",9)})
        assert(!decode(bad,plan,error));
    assert(!valid({{Effect::WaitTime,"",60},{Effect::WaitTime,"",60},{Effect::WaitTime,"",1}}));
    assert(!valid({{Effect::WaitTime,"",std::numeric_limits<double>::quiet_NaN()}}));
    auto native=asset("2","0\nAnimation\n"+animation+"\n0\n0\nAnimation\n"+animation+"\n1\n");
    assert(decode(native,plan,error));
    assert(decode(good+std::string(1,'\0'),plan,error)); // native serialized cache includes one terminator
    if(argc>1)
    {
        std::ifstream file(argv[1],std::ios::binary);assert(file);
        std::string bytes{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        assert(decode(bytes,plan,error));
    }

    bool permission=true;int delivered=0;
    auto guard=std::make_shared<fs_assistant::AsyncOperation>([&](){return permission?std::string():std::string("revoked");});
    auto task=std::make_shared<fs_assistant::AsyncTask>(guard);guard->seal();
    Timeline timed({{Effect::Chat,"first",0},{Effect::WaitTime,"",5},{Effect::Chat,"second",0}},0);
    auto allowed=[&](){return fs_assistant::asyncTaskAllowed(task);};auto none=[](){return false;};
    auto deliver=[&](const Step&){++delivered;return fs_assistant::asyncTaskSubmit(task);};
    timed.tick(0,allowed,none,deliver);assert(delivered==1 && !timed.done() && guard->pending()==1);
    timed.tick(4,allowed,none,deliver);assert(delivered==1);
    permission=false;timed.tick(5,allowed,none,deliver);assert(timed.done() && delivered==1);
    permission=true;timed.tick(6,allowed,none,deliver);assert(delivered==1); // revocation is sticky
    task.reset();assert(guard->pending()==0 && guard->state()=="partially_submitted");

    Timeline waiting({{Effect::WaitAnimations,"",60},{Effect::Chat,"must not send",0}},0);
    auto yes=[](){return true;};waiting.tick(0,yes,yes,deliver);waiting.tick(60,yes,yes,deliver);
    assert(waiting.done() && waiting.reason()=="animation_wait_timeout" && delivered==1);
    Timeline bounded({{Effect::WaitTime,"",60},{Effect::WaitTime,"",60},{Effect::Chat,"too late",0}},0);
    bounded.tick(0,yes,none,deliver);bounded.tick(60,yes,none,deliver);bounded.tick(120,yes,none,deliver);
    assert(bounded.done() && bounded.reason()=="playback_duration_limit" && delivered==1);
    bool current=true;int count=0;
    Timeline effects({{Effect::Sound,animation,0},{Effect::Chat,"after revoke",0}},0);
    effects.tick(0,[&](){return current;},none,[&](const Step&){++count;current=false;return true;});
    assert(count==1 && effects.done()); // authority is checked between immediate effects too

    AnimationLeases leases;auto original=leases.acquire(animation);assert(leases.owns(animation,original));
    leases.observed(animation);assert(!leases.owns(animation,original)); // manual/native request supersedes old cancellation
    auto second=leases.acquire(animation);leases.release(animation);auto third=leases.acquire(animation);
    assert(!leases.owns(animation,second) && leases.owns(animation,third)); // no ABA after release
    leases.observed("22222222-2222-4222-8222-222222222222");assert(leases.owns(animation,third));
    leases.invalidate();assert(!leases.owns(animation,third));
    assert(cleanupAllowed(true,true,false));assert(!cleanupAllowed(false,true,false));
    assert(!cleanupAllowed(true,false,false));assert(!cleanupAllowed(true,true,true));
    assert(animationAvailable(false,false,false,false));
    assert(!animationAvailable(false,false,false,true)); // local preview existed before acquisition
    assert(!animationAvailable(true,false,false,false));
    assert(!animationAvailable(false,true,false,false));assert(!animationAvailable(false,false,true,false));
    Plan builtins;builtins.steps={{Effect::AnimationStart,animation,0},{Effect::AnimationStop,animation,0}};
    assert(!animationPolicy(builtins,[&](const std::string& id){return id==animation;},error));
    builtins.steps.erase(builtins.steps.begin()); // STOP is independently checked too
    assert(!animationPolicy(builtins,[&](const std::string& id){return id==animation;},error));
    assert(animationPolicy(builtins,[](const std::string&){return false;},error));
    assert(animationHeader(animationAsset(""),error));
    for(const auto* emote:{"away","standup","Closed_Mouth","unknown","11111111-1111-4111-8111-111111111111"})
        assert(!animationHeader(animationAsset(emote),error));
    auto header=animationAsset("");header[0]=0;header[2]=1;assert(!animationHeader(header,error));
    header=animationAsset("");header[4]=static_cast<char>(0xfe);assert(!animationHeader(header,error));
    header=animationAsset("");header[11]=static_cast<char>(0x7f);header[10]=static_cast<char>(0xc0);assert(!animationHeader(header,error));
    assert(!animationHeader(animationAsset("").substr(0,40),error));
    assert(!animationHeader(std::string(maxAnimationAssetBytes+1,'\0'),error));
    assert(animationBytesAllowed(maxAnimationAssetBytes,maxAnimationTotalBytes-maxAnimationAssetBytes));
    assert(!animationBytesAllowed(0,0));assert(!animationBytesAllowed(maxAnimationAssetBytes+1,0));
    assert(!animationBytesAllowed(1,maxAnimationTotalBytes));
    assert(!animationBytesAllowed(1,std::numeric_limits<std::size_t>::max()));
    Plan referenced;for(unsigned i=0;i<17;++i)referenced.steps.push_back({Effect::AnimationStart,std::to_string(i),0});
    std::vector<std::string> referenceIDs;assert(!animationReferences(referenced,referenceIDs,error));
    referenced.steps.pop_back();referenced.steps.push_back(referenced.steps.front());
    assert(animationReferences(referenced,referenceIDs,error) && referenceIDs.size()==16);
    // Manual replay claims an animation while the old timeline is waiting;
    // source/lease validation prevents its next effect and unsafe cleanup.
    const auto old=leases.acquire(animation);Timeline overlap({{Effect::WaitTime,"",5},{Effect::Chat,"stale",0}},0);
    auto ownership=[&](){return leases.owns(animation,old);};
    overlap.tick(0,ownership,none,deliver);leases.observed(animation);overlap.tick(5,ownership,none,deliver);
    assert(overlap.done() && delivered==1 && !cleanupAllowed(true,ownership(),false));
    RegionEpoch epoch;const auto origin=epoch.generation();
    Timeline aba({{Effect::WaitTime,"",5},{Effect::Chat,"stale region",0}},0);
    auto sameRegion=[&](){return epoch.generation()==origin;};
    aba.tick(0,sameRegion,none,deliver);epoch.changed();epoch.changed(); // A -> B -> A without a tick
    aba.tick(5,sameRegion,none,deliver);assert(aba.done() && delivered==1);
    assert(!cleanupAllowed(sameRegion(),true,false));
}
