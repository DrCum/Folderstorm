#include "linden_common.h"
#include "current_motion_fixture.h"
#include "fssamplingshadow.h"
#include "sampling_test_context.h"
#include "sampling_allocation_probe.h"
#include "llsimdmath.h"
#include <cstdlib>
#include <iostream>

namespace SamplingMaskTest
{
using namespace FSCurrentMotion;
using FSSampling::ShadowScope;
unsigned checks=0;
void require(bool ok,const char* message){++checks;if(!ok){std::cerr<<message<<" at "<<checks<<'\n';std::exit(1);}}
LLUUID key(unsigned count,unsigned n,unsigned mode,unsigned i)
{
    LLUUID id("e7142000-0000-4000-8000-000000000000");
    id.mData[11]=U8(mode);id.mData[12]=U8(count>>8);id.mData[13]=U8(count);id.mData[14]=U8(n);id.mData[15]=U8(i+1);return id;
}
void conservation()
{
    const auto s=ShadowScope::totals();
    require(s.predicted==s.matched+s.mismatched+s.unused,"Query accounting");
    require(s.masked==s.maskedObserved+s.maskedUnobserved,"Negative prediction accounting");
    require(s.bufferBytesPeak<=ShadowScope::MAX_BUFFER_BYTES && s.retainedKeysPeak<=ShadowScope::MAX_RETAINED_KEYS,"Storage bound");
    require(!FSSampling::activeShadow,"Scope lifetime");
}
class OracleController : public LLMotionController
{
public:
    using LLMotionController::predictSamplingMask;
    auto masks() const {std::array<U8,2*LL_CHARACTER_MAX_ANIMATED_JOINTS> out{};std::memcpy(out.data(),mJointSignature,sizeof(mJointSignature));return out;}
};
class SignatureMotion : public LLNullMotion
{
public:
    unsigned calls=0,poseReads=0,typeReads=0;
    explicit SignatureMotion(const LLUUID& id):LLNullMotion(id){}
    LLPose* getPose() override {++poseReads;return &mPose;}
    LLMotionBlendType getBlendType() override {++typeReads;return NORMAL_BLEND;}
    bool onUpdate(F32,U8*) override {++calls;return true;}
    void configure(unsigned scenario,unsigned index)
    {
        std::memset(mJointSignature,0,sizeof(mJointSignature));
        F32 weight=1.f;
        if(scenario==0){}
        else if(scenario==1){mJointSignature[0][0]=0xff;}
        else if(scenario==2){mJointSignature[index%2][index*4+3]=U8(1u<<index);}
        else if(scenario==3){mJointSignature[2][LL_CHARACTER_MAX_ANIMATED_JOINTS-1]=0xff;}
        else
        {
            U32 state=scenario*1664525u+index*1013904223u;
            for(unsigned channel=0;channel<3;++channel)for(unsigned i=0;i<LL_CHARACTER_MAX_ANIMATED_JOINTS;++i)
            {state=state*1664525u+1013904223u;mJointSignature[channel][i]=U8(state>>24);}
            const F32 weights[]={-.5f,0.f,.5f,1.f,2.f};weight=weights[(scenario+index)%5];
        }
        if(scenario==31)weight=index%2?std::numeric_limits<F32>::infinity():std::numeric_limits<F32>::quiet_NaN();
        mPose.setWeight(weight);calls=poseReads=typeReads=0;
    }
    static LLMotion* create(const LLUUID& id){return new SignatureMotion(id);}
};
void oracle()
{
    OracleController registrar;
    for(unsigned i=0;i<4;++i)require(registrar.registerMotion(key(0,4,0,i),SignatureMotion::create),"Oracle registration");
    for(unsigned scenario=0;scenario<32;++scenario)
    {
        Clock::set(100.0);Character character;OracleController c;c.setCharacter(&character);c.updateMotions();
        for(unsigned i=0;i<4;++i){const auto id=key(0,4,0,i);require(c.startMotion(id,0.f),"Oracle activation");static_cast<SignatureMotion*>(c.findMotion(id))->configure(scenario,i);}
        U8 covered[2][LL_CHARACTER_MAX_ANIMATED_JOINTS]{};
        const auto before=c.masks();std::vector<std::pair<SignatureMotion*,bool>> expected;
        for(auto* base:c.getActiveMotions())
        {
            auto* motion=static_cast<SignatureMotion*>(base);bool admitted;
            {FSSampling::ScopedNumericContext preserve(FSSampling::NumericContext::capture());admitted=c.predictSamplingMask(*motion,covered);}
            require(!motion->poseReads&&!motion->typeReads,"Mask model called virtual getters");expected.emplace_back(motion,admitted);
        }
        require(before==c.masks(),"Mask model changed controller masks");Clock::set(100.125);c.updateMotions();
        for(const auto& entry:expected)
            require(scenario==31 ? entry.second||!entry.first->calls : entry.first->calls==unsigned(entry.second),"Mask prediction differs from real callback oracle");
    }
}
enum Mode {SATURATED,PARTIAL,DISJOINT,SCALE_ONLY,MIXED_PRIORITY,SATURATED_WITH_TAIL};
struct Group
{
    struct Seed{Seed(){Clock::set(100.0);}}seed;
    Character actual,expected;std::vector<LLUUID> ids;
    Group(unsigned count,unsigned n,unsigned mode):actual(count),expected(count)
    {
        actual.getMotionController().updateMotions();expected.getMotionController().updateMotions();
        for(unsigned i=0;i<n;++i)
        {
            const auto id=key(count,n,mode,i);ids.push_back(id);auto data=asset(count);data->mEaseInDuration=mode==PARTIAL?1.f:0.f;
            if(mode==DISJOINT)for(size_t j=data->mJointMotionArray.size();j-->0;)if(j%n!=i){delete data->mJointMotionArray[j];data->mJointMotionArray.erase(data->mJointMotionArray.begin()+j);}
            if(mode==SCALE_ONLY)for(auto* joint:data->mJointMotionArray)joint->mUsage=LLJointState::SCALE;
            if(mode==MIXED_PRIORITY || (mode==SATURATED_WITH_TAIL && i==0))
            {
                data->mBasePriority=data->mMaxPriority=i%2?LLJoint::LOW_PRIORITY:LLJoint::HIGH_PRIORITY;
                for(auto* joint:data->mJointMotionArray)joint->mPriority=data->mBasePriority;
            }
            LLKeyframeDataCache::addKeyframeData(id,data);
            require(actual.getMotionController().startMotion(id,0.f)&&expected.getMotionController().startMotion(id,0.f),"Group activation");
        }
        Clock::set(100.125);for(unsigned i=0;i<8;++i){actual.getMotionController().updateMotions();expected.getMotionController().updateMotions();}
    }
    ~Group(){actual.getMotionController().deactivateAllMotions();expected.getMotionController().deactivateAllMotions();LLKeyframeDataCache::clear();}
    auto state(Character& c)
    {
        std::vector<U32> out;auto scalar=[&](F32 value){U32 bits;std::memcpy(&bits,&value,4);out.push_back(bits);};
        scalar(c.getMotionController().getAnimTime());out.push_back(U32(c.getMotionController().getActiveMotions().size()));
        for(const auto& id:ids)
        {
            auto* motion=dynamic_cast<LLKeyframeMotion*>(c.findMotion(id));out.push_back(motion!=nullptr);
            if(motion){auto data=pose(*motion);out.insert(out.end(),data.begin(),data.end());scalar(motion->getPose()->getWeight());}
        }
        for(auto& joint:c.joints){for(F32 v:joint->getPosition().mV)scalar(v);for(F32 v:joint->getRotation().mQ)scalar(v);for(F32 v:joint->getScale().mV)scalar(v);}
        return out;
    }
    void compare(){require(state(actual)==state(expected),"Admission changed real poses or playback state");}
    void update(){expected.getMotionController().updateMotions();actual.getMotionController().updateMotions();compare();}
};
unsigned mutationKind=0;
class Mutator : public LLNullMotion
{
public:
    LLCharacter* owner=nullptr;unsigned kind,poseReads=0,typeReads=0;bool armed=false;
    explicit Mutator(const LLUUID& id):LLNullMotion(id),kind(mutationKind){}
    LLPose* getPose() override {++poseReads;return &mPose;}
    LLMotionBlendType getBlendType() override {++typeReads;return kind==1?ADDITIVE_BLEND:NORMAL_BLEND;}
    LLMotionInitStatus onInitialize(LLCharacter* c) override {owner=c;return STATUS_SUCCESS;}
    void arm(){armed=true;mPose.setWeight(0.f);poseReads=typeReads=0;}
    bool onUpdate(F32,U8*) override
    {
        if(armed)
        {
            armed=false;
            for(auto* base:owner->getMotionController().getActiveMotions())
                if(auto* motion=dynamic_cast<LLKeyframeMotion*>(base))motion->getPose()->setWeight(0.f);
            if(kind==2)owner->getMotionController().updateMotions(true);
        }
        return true;
    }
    static LLMotion* create(const LLUUID& id){return new Mutator(id);}
};
void opaqueCallbacks()
{
    for(mutationKind=0;mutationKind<3;++mutationKind)
    {
        Group g(3,8,SATURATED);const auto id=key(0,1,55,mutationKind);
        require(g.actual.registerMotion(id,Mutator::create),"Mutator registration");
        require(g.actual.getMotionController().startMotion(id,0.f)&&g.expected.getMotionController().startMotion(id,0.f),"Mutator activation");
        auto* a=static_cast<Mutator*>(g.actual.findMotion(id));auto* b=static_cast<Mutator*>(g.expected.findMotion(id));a->arm();b->arm();
        ShadowScope::resetTotals();
        {ShadowScope scope(g.actual.getMotionController(),true);require(!a->poseReads&&!a->typeReads,"Preparation invoked an opaque getter");g.update();}
        conservation();const auto s=ShadowScope::totals();require(s.masked==7&&s.maskedObserved==7&&!s.maskedUnobserved&&s.unpredicted>=7,"Opaque mutation or reentry hid an advisory miss");
    }
}
void ordinary()
{
    for(unsigned count:{1u,5u,unsigned(LL_CHARACTER_MAX_ANIMATED_JOINTS)})for(unsigned n:{1u,8u})for(unsigned mode=0;mode<5;++mode)
    {
        if(mode==DISJOINT&&n>count)continue;Group g(count,n,mode);ShadowScope::resetTotals();
        {ShadowScope scope(g.actual.getMotionController(),true);g.update();}
        conservation();const auto s=ShadowScope::totals();require(!s.mismatched&&!s.maskedObserved,"Stable inputs changed a prediction");
        if(mode==SATURATED)require(s.predicted==1&&s.matched==1&&s.masked==n-1&&s.maskedUnobserved==n-1,"Saturated admission");
        if(mode==SCALE_ONLY)require(!s.predicted&&s.masked==n&&!s.unpredicted,"Scale-only mask behavior changed");
        if(mode==PARTIAL||mode==DISJOINT)require(!s.masked&&s.matched+s.unpredicted==n,"Useful callbacks were masked");
    }
}
void missesAndBounds()
{
    {
        Group g(3,8,SATURATED);ShadowScope::resetTotals();
        {ShadowScope scope(g.actual.getMotionController(),true);
         for(auto* c:{&g.actual,&g.expected})for(auto* motion:c->getMotionController().getActiveMotions())motion->getPose()->setWeight(0.f);
         g.update();g.update();}
        conservation();const auto s=ShadowScope::totals();require(s.masked==7&&s.maskedObserved==7&&!s.maskedUnobserved&&s.unpredicted>=7,"Advisory callback miss was lost or suppressed");
    }
    {
        Group g(3,41,SATURATED);ShadowScope::resetTotals();{ShadowScope scope(g.actual.getMotionController(),true);g.update();}
        conservation();const auto s=ShadowScope::totals();require(s.masked==ShadowScope::MAX_MOTIONS&&s.predicted==9&&s.maskedUnobserved==32,"Negative record cap did not retain original preparation fallback");
    }
    {
        Group g(3,8,SATURATED);ShadowScope::resetTotals();
        {ShadowScope outer(g.actual.getMotionController(),true);{ShadowScope inner(g.expected.getMotionController(),true);g.expected.getMotionController().updateMotions();}g.actual.getMotionController().updateMotions();}
        g.compare();conservation();require(ShadowScope::totals().nested==1&&ShadowScope::totals().maskedUnobserved==7,"Cancelled negative records lost accounting");
    }
}
void destruction()
{
    Clock::set(100.0);auto c=std::make_unique<Character>(1);const auto id=key(1,1,42,0);auto data=asset(1);
    data->mJointMotionArray[0]->mUsage=LLJointState::SCALE;std::weak_ptr<LLKeyframeMotion::JointMotionList> life=data;
    LLKeyframeDataCache::addKeyframeData(id,data);data.reset();auto& controller=c->getMotionController();controller.updateMotions();
    require(controller.startMotion(id,0.f),"Destruction activation");c->findMotion(id)->getPose()->setWeight(1.f);Clock::set(100.125);ShadowScope::resetTotals();
    {ShadowScope scope(controller,true);require(ShadowScope::totals().masked==1&&!ShadowScope::totals().predicted,"Pruned lifetime fixture");LLKeyframeDataCache::removeKeyframeData(id);c.reset();require(life.expired(),"Negative record retained an asset or motion");}
    conservation();require(ShadowScope::totals().destroyed==1&&ShadowScope::totals().maskedUnobserved==1,"Destroyed negative record accounting");
}
int allocations()
{
#if FS_SAMPLING_ALLOCATION_PROBE
#if TRACY_ENABLE
    std::cout<<"SKIP global allocation injection: requires Tracy OFF\n";return 0;
#endif
    Group g(3,9,SATURATED_WITH_TAIL);ShadowScope::resetTotals();FSSamplingAlloc::Stats stats;
    {FSSamplingAlloc::Scope allocationScope;FSSamplingAlloc::failureCountdown=3;
     {ShadowScope scope(g.actual.getMotionController(),true);FSSamplingAlloc::failureCountdown=-1;require(!FSSampling::activeShadow,"Failed preparation kept observation active");g.update();}
     stats=FSSamplingAlloc::snapshot();}
    conservation();const auto s=ShadowScope::totals();require(s.rejected&&s.masked==7&&s.maskedUnobserved==7&&s.predicted==1&&s.unused==1,"Failure after negative records lost accounting");
    require(stats.live==stats.baseline,"Failure after negative records leaked storage");
    for(auto* motion:g.actual.getMotionController().getActiveMotions())require(!static_cast<LLKeyframeMotion*>(motion)->ownedSamplingBindingBytes(),"Failed preparation retained bindings");
    std::cout<<checks<<" post-prune allocation failure/cleanup checks passed\n";
#endif
    return 0;
}
int run()
{
    if(!FSSamplingTest::supportedContext("sampling mask admission"))return 0;
    LLVector4a::initClass();oracle();ordinary();opaqueCallbacks();missesAndBounds();destruction();
    std::cout<<checks<<" signature-oracle, admission, advisory and lifetime checks passed\n";return 0;
}
}
int main(int argc,char** argv)
{
    if(FS_SAMPLING_ALLOCATION_PROBE)
    {
        if(!FSSamplingTest::supportedContext("sampling mask allocations"))return 0;
        LLVector4a::initClass();return argc==2&&std::string(argv[1])=="--allocations"?SamplingMaskTest::allocations():0;
    }
    return SamplingMaskTest::run();
}
