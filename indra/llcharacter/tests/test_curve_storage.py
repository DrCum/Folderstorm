"""Generate an opt-in animation-key container experiment using production samplers."""
from pathlib import Path
import argparse
import test_curve_sampling

PREFIX = r"""
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <memory>
#include <boost/container/flat_map.hpp>

struct AllocationStats
{
    std::size_t calls=0, total=0, live=0, peak=0;
};
struct MapTag { inline static AllocationStats stats; };
struct FlatTag { inline static AllocationStats stats; };
template<class T, class Tag> struct CountingAllocator
{
    using value_type = T;
    using is_always_equal = std::true_type;
    CountingAllocator() = default;
    template<class U> CountingAllocator(const CountingAllocator<U,Tag>&) {}
    template<class U> struct rebind { using other = CountingAllocator<U,Tag>; };
    T* allocate(std::size_t n)
    {
        T* result = std::allocator<T>{}.allocate(n);
        auto& s=Tag::stats; ++s.calls; s.total+=n*sizeof(T); s.live+=n*sizeof(T);
        s.peak=std::max(s.peak,s.live);
        return result;
    }
    void deallocate(T* pointer, std::size_t n)
    {
        Tag::stats.live-=n*sizeof(T); std::allocator<T>{}.deallocate(pointer,n);
    }
    template<class U> bool operator==(const CountingAllocator<U,Tag>&) const { return true; }
    template<class U> bool operator!=(const CountingAllocator<U,Tag>&) const { return false; }
};
"""

EXPERIMENT = r"""
namespace StorageExperiment
{
using Clock = std::chrono::steady_clock;
volatile std::uint64_t outputSink=0;
unsigned crossComparisons=0;
template<class Key> auto& value(Key& key)
{
    if constexpr(requires { key.mRotation; }) return key.mRotation;
    else if constexpr(requires { key.mScale; }) return key.mScale;
    else return key.mPosition;
}
template<class Key> std::vector<Key> keys(unsigned count, unsigned seed, unsigned order, bool duplicates)
{
    std::mt19937 random(seed);
    std::vector<Key> result;
    for(unsigned i=0;i<count;++i)
    {
        Key key; key.mTime=static_cast<F32>(i)/30.f;
        auto lane=[&] { return static_cast<F32>(static_cast<int>(random()%2049)-1024)/128.f; };
        if constexpr(requires { key.mRotation; })
        {
            value(key)=LLQuaternion(lane(),LLVector3::x_axis)*LLQuaternion(lane(),LLVector3::z_axis);
            if(i%2) for(auto& component:value(key).mQ) component=-component;
        }
        else value(key)=LLVector3(lane(),lane(),lane());
        result.push_back(key);
        if(duplicates && i%7==0)
        {
            if constexpr(requires { key.mRotation; }) value(key)=LLQuaternion::DEFAULT;
            else value(key)=LLVector3(-1.f,0.f,1.f);
            result.push_back(key);
        }
    }
    if(order==1) std::reverse(result.begin(),result.end());
    if(order==2) std::shuffle(result.begin(),result.end(),random);
    return result;
}
template<class Curve, class Keys> void load(Curve& curve, const Keys& input)
{
    curve.mKeys.clear(); curve.mNumKeys=static_cast<S32>(input.size());
    for(const auto& source:input)
    {
        typename Curve::key_type key;
        key.mTime=source.mTime; value(key)=value(source);
        curve.mKeys[key.mTime]=key;
    }
    curve.mLoopInKey={}; curve.mLoopOutKey={};
    curve.mLoopInKey.mTime=0.375f; curve.mLoopOutKey.mTime=1.625f;
}
template<class MapCurve,class FlatCurve> void equivalence()
{
    for(unsigned count:{0,1,2,4,15,32,64,128,512,1024})
        for(unsigned order:{0,1,2})
            for(unsigned mode:{0,1,2})
            {
                const auto input=keys<typename MapCurve::key_type>(count,0x46534353+count,order,true);
                MapCurve map; FlatCurve flat;
                load(map,input); load(flat,input);
                map.mInterpolationType=static_cast<CurveMapFixture::InterpolationType>(mode);
                flat.mInterpolationType=static_cast<CurveFlatFixture::InterpolationType>(mode);
                auto reference=map;
                const auto before=CurveMapFixture::state(map);
                CurveMapFixture::require(before==CurveFlatFixture::state(flat),"Storage changed sorted keys/duplicate replacement");
                const F32 end=static_cast<F32>(count)/30.f;
                std::vector<F32> times={-10.f,-0.f,0.f,end*0.25f,end*0.5f,end,end+1.f,1.e6f};
                for(const auto& item:map.mKeys)
                {
                    times.push_back(item.first);
                    times.push_back(std::nextafter(item.first,-1000.f));
                    times.push_back(std::nextafter(item.first,1000.f));
                }
                for(F32 time:times)
                {
                    const auto expected=CurveMapFixture::legacy(reference,time,3.f);
                    CurveMapFixture::require(CurveMapFixture::exact(expected,map.getValue(time,3.f)) &&
                        CurveMapFixture::exact(expected,flat.getValue(time,3.f)),"Container sampling differs from frozen map behavior");
                    ++crossComparisons;
                }
                CurveMapFixture::require(before==CurveMapFixture::state(map) && before==CurveFlatFixture::state(flat),
                    "Container sampling changed key/metadata state");
                // Loop keys can be edited independently of the container.
                map.mLoopInKey.mTime=flat.mLoopInKey.mTime=0.75f;
                CurveMapFixture::require(CurveMapFixture::exact(map.getValue(end*0.5f,4.f),flat.getValue(end*0.5f,4.f)),
                    "Edited-loop sample changed");
            }
}
struct Query { unsigned curve; F32 time; };
std::vector<Query> queries(unsigned count,unsigned pattern)
{
    std::vector<Query> result; result.reserve(65536);
    std::mt19937 random(0x46535155);
    const F32 duration=std::max(1.f/30.f,static_cast<F32>(count)/30.f);
    for(unsigned i=0;i<65536;++i)
    {
        const F32 time=pattern==1 ? duration*static_cast<F32>(random()%65536)/65536.f :
            std::fmod(static_cast<F32>(i/32)/60.f+(pattern==2 ? duration*0.9f : 0.f),duration);
        result.push_back({(i*17)%32,time});
    }
    return result;
}
#ifdef _MSC_VER
#define FS_SAMPLE_NOINLINE __declspec(noinline)
#else
#define FS_SAMPLE_NOINLINE __attribute__((noinline))
#endif
template<class Curve> FS_SAMPLE_NOINLINE void sample(const std::vector<Curve>& curves,
    const std::vector<Query>& input,std::vector<typename Curve::value_type>& output)
{
    for(std::size_t i=0;i<input.size();++i) output[i]=curves[input[i].curve].getValue(input[i].time,10.f);
}
template<class Value> void consume(const std::vector<Value>& output)
{
    std::uint64_t hash=0;
    for(const auto& v:output)
        for(std::size_t i=0;i<CurveMapFixture::lanes(v);++i)
        {
            std::uint32_t bits; std::memcpy(&bits,CurveMapFixture::data(v)+i,sizeof(bits));
            hash=(hash*33)^bits;
        }
    outputSink=hash;
}
template<class Curve> double timedSample(const std::vector<Curve>& curves,const std::vector<Query>& input,
    std::vector<typename Curve::value_type>& output)
{
    std::atomic_signal_fence(std::memory_order_seq_cst);
    const auto start=Clock::now(); sample(curves,input,output); const auto finish=Clock::now();
    std::atomic_signal_fence(std::memory_order_seq_cst);
    consume(output);
    return std::chrono::duration<double,std::nano>(finish-start).count()/input.size();
}
double median(std::vector<double> values) { std::sort(values.begin(),values.end()); return values[values.size()/2]; }
template<class MapCurve,class FlatCurve> void samplingBench(const char* kind)
{
    for(unsigned count:{1,4,15,32,64,128,512,1024})
    {
        std::vector<MapCurve> map(32); std::vector<FlatCurve> flat(32);
        for(unsigned i=0;i<32;++i)
        {
            const auto input=keys<typename MapCurve::key_type>(count,0x46534353+i,0,false);
            load(map[i],input); load(flat[i],input);
        }
        for(unsigned pattern:{0,1,2})
        {
            const auto input=queries(count,pattern);
            std::vector<typename MapCurve::value_type> mapOutput(input.size()),flatOutput(input.size());
            sample(map,input,mapOutput); sample(flat,input,flatOutput);
            for(std::size_t i=0;i<input.size();++i)
                CurveMapFixture::require(CurveMapFixture::exact(mapOutput[i],flatOutput[i]),"Benchmark output mismatch");
            std::vector<double> mapTimes,flatTimes;
            for(unsigned repeat=0;repeat<9;++repeat)
            {
                auto mapRun=[&] {mapTimes.push_back(timedSample(map,input,mapOutput));};
                auto flatRun=[&] {flatTimes.push_back(timedSample(flat,input,flatOutput));};
                if(repeat%2) {flatRun();mapRun();} else {mapRun();flatRun();}
            }
            std::cout<<"sample,"<<kind<<','<<count<<','<<pattern<<','<<median(mapTimes)<<','<<median(flatTimes)
                <<','<<*std::min_element(mapTimes.begin(),mapTimes.end())<<','<<*std::max_element(mapTimes.begin(),mapTimes.end())
                <<','<<*std::min_element(flatTimes.begin(),flatTimes.end())<<','<<*std::max_element(flatTimes.begin(),flatTimes.end())<<'\n';
        }
    }
}
template<class Curve> double timedLoad(const std::vector<typename Curve::key_type>& input)
{
    const unsigned instances=input.size()<64 ? 4096 : (input.size()<256 ? 512 : 64);
    const auto start=Clock::now();
    std::vector<Curve> curves(instances);
    for(auto& curve:curves) load(curve,input);
    const auto finish=Clock::now();
    outputSink=curves.front().mKeys.size();
    return std::chrono::duration<double,std::nano>(finish-start).count()/curves.size();
}
template<class Curve,class Tag> AllocationStats allocationProbe(const std::vector<typename Curve::key_type>& input)
{
    CurveMapFixture::require(Tag::stats.live==0,"Live key allocations before counter reset");
    Tag::stats={};
    Curve curve; load(curve,input);
    return Tag::stats;
}
template<class MapCurve,class FlatCurve,class TrackedMap,class TrackedFlat> void loadingBench(const char* kind)
{
    for(unsigned count:{1,32,128,512,1024}) for(unsigned order:{0,1,2})
    {
        const auto mapInput=keys<typename MapCurve::key_type>(count,0x46534353,order,true);
        const auto flatInput=keys<typename FlatCurve::key_type>(count,0x46534353,order,true);
        const auto mapStats=allocationProbe<TrackedMap,MapTag>(keys<typename TrackedMap::key_type>(count,0x46534353,order,true));
        const auto flatStats=allocationProbe<TrackedFlat,FlatTag>(keys<typename TrackedFlat::key_type>(count,0x46534353,order,true));
        std::vector<double> mapTimes,flatTimes;
        for(unsigned repeat=0;repeat<9;++repeat)
        {
            auto mapRun=[&] {mapTimes.push_back(timedLoad<MapCurve>(mapInput));};
            auto flatRun=[&] {flatTimes.push_back(timedLoad<FlatCurve>(flatInput));};
            if(repeat%2) {flatRun();mapRun();} else {mapRun();flatRun();}
        }
        auto report=[&](const char* container,const std::vector<double>& times,const AllocationStats& s,std::size_t objectSize)
        {
            std::cout<<"load,"<<kind<<','<<count<<','<<order<<','<<container<<','<<median(times)<<','
                <<*std::min_element(times.begin(),times.end())<<','<<*std::max_element(times.begin(),times.end())
                <<','<<s.calls<<','<<s.total<<','<<s.live<<','<<s.peak<<','<<objectSize<<'\n';
        };
        report("map",mapTimes,mapStats,sizeof(MapCurve)); report("flat",flatTimes,flatStats,sizeof(FlatCurve));
    }
}
int run(bool benchmark)
{
    CurveMapFixture::run(); CurveFlatFixture::run();
    equivalence<CurveMapFixture::LLKeyframeMotion::ScaleCurve,CurveFlatFixture::LLKeyframeMotion::ScaleCurve>();
    equivalence<CurveMapFixture::LLKeyframeMotion::PositionCurve,CurveFlatFixture::LLKeyframeMotion::PositionCurve>();
    equivalence<CurveMapFixture::LLKeyframeMotion::RotationCurve,CurveFlatFixture::LLKeyframeMotion::RotationCurve>();
    std::cout<<crossComparisons<<" map/flat storage comparisons passed\n";
    if(!benchmark) return 0;
    std::cout<<"9 alternating samples; 32 curve instances; 65536 queries/sample; warmed; default linear interpolation\n"
        <<"sample,kind,keys,pattern(0=forward/1=seek/2=wrap),map_ns,flat_ns,map_min,map_max,flat_min,flat_max\n"
        <<std::fixed<<std::setprecision(3);
    samplingBench<CurveMapTiming::LLKeyframeMotion::PositionCurve,CurveFlatTiming::LLKeyframeMotion::PositionCurve>("position");
    samplingBench<CurveMapTiming::LLKeyframeMotion::RotationCurve,CurveFlatTiming::LLKeyframeMotion::RotationCurve>("rotation");
    std::cout<<"load,kind,keys,order(0=sorted/1=reverse/2=shuffle),container,ns_per_curve,min,max,allocations,allocated_bytes,retained_bytes,peak_bytes,curve_object_bytes\n";
    loadingBench<CurveMapTiming::LLKeyframeMotion::PositionCurve,CurveFlatTiming::LLKeyframeMotion::PositionCurve,
        CurveMapFixture::LLKeyframeMotion::PositionCurve,CurveFlatFixture::LLKeyframeMotion::PositionCurve>("position");
    loadingBench<CurveMapTiming::LLKeyframeMotion::RotationCurve,CurveFlatTiming::LLKeyframeMotion::RotationCurve,
        CurveMapFixture::LLKeyframeMotion::RotationCurve,CurveFlatFixture::LLKeyframeMotion::RotationCurve>("rotation");
    return 0;
}
}
int main(int argc,char** argv)
{
    if(argc>2 || (argc==2 && std::strcmp(argv[1],"--benchmark")!=0))
    { std::cerr<<"Usage: curve_storage [--benchmark]\n";return 2; }
    return StorageExperiment::run(argc==2);
}
"""


def generate(source: Path) -> str:
    baseline = test_curve_sampling.generate(source)
    main = "int main() { return CurveFixture::run(); }"
    if baseline.count(main) != 1:
        raise ValueError("Sampling harness entry point changed")
    baseline = baseline.replace(main, "")
    if baseline.count("std::map<F32, Key>") != 1:
        raise ValueError("Sampling harness key-container alias changed")
    map_fixture = baseline.replace("CurveFixture", "CurveMapFixture").replace(
        "std::map<F32, Key>",
        "std::map<F32, Key, std::less<F32>, CountingAllocator<std::pair<const F32,Key>,MapTag>>")
    flat_fixture = baseline.replace("CurveFixture", "CurveFlatFixture").replace(
        "std::map<F32, Key>",
        "boost::container::flat_map<F32, Key, std::less<F32>, CountingAllocator<std::pair<F32,Key>,FlatTag>>")
    # Put the common math/includes first, and allocator types before both fixtures.
    boundary = map_fixture.index("namespace CurveMapFixture")
    map_timing = baseline.replace("CurveFixture", "CurveMapTiming")
    flat_timing = baseline.replace("CurveFixture", "CurveFlatTiming").replace(
        "std::map<F32, Key>", "boost::container::flat_map<F32, Key>")
    # Timing matches release assertion behavior; correctness fixtures keep assertions active.
    assertion = '#define llassert(condition) require(bool(condition), "Production curve assertion failed")'
    if baseline.count(assertion) != 1:
        raise ValueError("Sampling harness assertion contract changed")
    map_timing = map_timing.replace(assertion, "#define llassert(condition) ((void)0)")
    flat_timing = flat_timing.replace(assertion, "#define llassert(condition) ((void)0)")
    return map_fixture[:boundary] + PREFIX + map_fixture[boundary:] + flat_fixture + map_timing + flat_timing + EXPERIMENT


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=test_curve_sampling.ROOT / "llkeyframemotion.cpp")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(generate(args.source), encoding="utf-8")
