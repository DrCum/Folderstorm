// Pure sampling compared with actual JointMotion::update and LLJointState setters.
#include "linden_common.h"
#include "llkeyframemotion.h"
#include "sampling_test_context.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <type_traits>
#include <utility>

namespace OwnedSamplingTest
{
unsigned checks = 0;
unsigned poses = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
class Probe : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    using Joint = JointMotion;
    using Request = SamplingRequest;
    using Result = SamplingResult;
    using Identity = SamplingIdentity;
    using Row = SampledJoint;
    using Mode = InterpolationType;
    using SK = ScaleKey;
    using RK = RotationKey;
    using PK = PositionKey;
    static constexpr Mode STEP = IT_STEP, LINEAR = IT_LINEAR, SPLINE = IT_SPLINE;
    static bool sample(Request request, Result& result) { return sampleKeyframes(std::move(request),result); }
    static bool matches(const Result& result,const Request& current) { return samplingResultMatches(result,current); }
};
static_assert(!std::is_copy_constructible_v<Probe::Request> && !std::is_copy_constructible_v<Probe::Result>);
static_assert(std::is_move_constructible_v<Probe::Request> && std::is_move_constructible_v<Probe::Result>);
constexpr U32 ALL = LLJointState::POS | LLJointState::ROT | LLJointState::SCALE;

Probe::Request currentCopy(const Probe::Request& input)
{
    Probe::Request current;
    current.numericContext=FSSampling::NumericContext::capture();
    current.asset=input.asset; current.identity=input.identity; current.time=input.time;
    current.duration=input.duration; current.usage=input.usage;
    return current;
}
LLPointer<LLJointState> seeded(U32 usage)
{
    LLPointer<LLJointState> state = new LLJointState;
    state->setUsage(ALL);
    state->setScale(LLVector3(2.5f,3.5f,4.5f));
    state->setRotation(LLQuaternion(0.5f,LLVector3::x_axis));
    state->setPosition(LLVector3(101.f,-101.f,0.125f));
    state->setWeight(0.375f); state->setPriority(LLJoint::HIGH_PRIORITY); state->setUsage(usage);
    return state;
}
bool exact(const LLVector3& a,const LLVector3& b) { return std::memcmp(a.mV,b.mV,sizeof(a.mV))==0; }
bool exact(const LLQuaternion& a,const LLQuaternion& b) { return std::memcmp(a.mQ,b.mQ,sizeof(a.mQ))==0; }
void apply(const Probe::Row& row,LLJointState& state)
{
    if(row.usage & LLJointState::SCALE) state.setScale(row.scale);
    if(row.usage & LLJointState::ROT) state.setRotation(row.rotation);
    if(row.usage & LLJointState::POS) state.setPosition(row.position);
}
void compare(const LLJointState& expected,const LLJointState& actual)
{
    require(exact(expected.getScale(),actual.getScale()) && exact(expected.getRotation(),actual.getRotation()) &&
            exact(expected.getPosition(),actual.getPosition()),"Pure sampling changed pose bytes or unrequested channels");
    require(expected.getUsage()==actual.getUsage() && expected.getWeight()==actual.getWeight() &&
            expected.getPriority()==actual.getPriority(),"Pure sampling changed usage, weight or priority");
    ++poses;
}
template<class Curve,class Factory> void fill(Curve& curve,unsigned selector,Probe::Mode mode,Factory make)
{
    const unsigned counts[] = {0,1,2,4,15,32,65};
    const unsigned count=counts[selector%7];
    curve.mInterpolationType=mode; curve.mNumKeys=0;
    for(unsigned i=0;i<count;++i)
    {
        const F32 time=static_cast<F32>(i)*0.5f;
        curve.mKeys[time]=make(time,i); ++curve.mNumKeys;
        if(i%5==0) {curve.mKeys[time]=make(time,i+17); ++curve.mNumKeys;}
    }
    if(selector%11==0) curve.mNumKeys=0; // The original gate is count, not container size.
    if(selector%13==0) {curve.mKeys.clear();curve.mNumKeys=1;}
}
std::shared_ptr<Probe::Asset> asset(unsigned count,unsigned seed)
{
    auto data=std::make_shared<Probe::Asset>();
    const auto mode=seed%3==0 ? Probe::STEP : (seed%3==1 ? Probe::LINEAR : Probe::SPLINE);
    for(unsigned j=0;j<count;++j)
    {
        auto* joint=new Probe::Joint;
        joint->mUsage=ALL; joint->mPriority=LLJoint::MEDIUM_PRIORITY;
        fill(joint->mScaleCurve,seed+j,mode,[](F32 time,unsigned i)
        {return Probe::SK(time,LLVector3(static_cast<F32>(i)/8.f,-static_cast<F32>(i)/16.f,-0.f));});
        fill(joint->mRotationCurve,seed+2*j+1,mode,[](F32 time,unsigned i)
        {
            LLQuaternion q(static_cast<F32>(i)/8.f,LLVector3::x_axis);
            q=q*LLQuaternion(static_cast<F32>(i)/32.f,LLVector3::y_axis);
            if(i%2) for(auto& lane:q.mQ) lane=-lane;
            return Probe::RK(time,q);
        });
        fill(joint->mPositionCurve,seed+j+3,mode,[](F32 time,unsigned i)
        {return Probe::PK(time,LLVector3(-static_cast<F32>(i)/4.f,0.f,static_cast<F32>(i)/32.f));});
        data->mJointMotionArray.push_back(joint);
    }
    return data;
}
Probe::Request request(const std::shared_ptr<Probe::Asset>& data,U64 frame)
{
    Probe::Request input;
    input.numericContext=FSSampling::NumericContext::capture();
    input.asset=data; input.identity={17,23,frame,31,41}; input.time=0.f;input.duration=3.f;
    input.usage.assign(data->mJointMotionArray.size(),ALL);
    return input;
}
U32 channels(const Probe::Joint& joint,U32 usage)
{
    return ((usage & LLJointState::SCALE) && joint.mScaleCurve.mNumKeys ? LLJointState::SCALE : 0) |
           ((usage & LLJointState::ROT) && joint.mRotationCurve.mNumKeys ? LLJointState::ROT : 0) |
           ((usage & LLJointState::POS) && joint.mPositionCurve.mNumKeys ? LLJointState::POS : 0);
}
void oracleCases()
{
    Probe::Result result;
    U64 frame=0;
    for(unsigned iteration=0;iteration<36;++iteration)
    {
        const unsigned count=1+iteration%6;
        auto data=asset(count,iteration);
        // Query scalars are authoritative; the sampler must not peek at live list metadata.
        data->mDuration=93.f; data->mLoop=true; data->mLoopInPoint=17.f;data->mLoopOutPoint=29.f;
        data->mHandPose=LLHandMotion::HAND_POSE_FIST;
        for(U32 mask:{0u,1u,2u,3u,4u,5u,6u,7u,0x87u})
            for(F32 time:{-5.f,-0.f,0.f,0.25f,0.5f,1.f,4.f,100.f})
                for(F32 duration:{0.f,0.75f,3.f})
                {
                    auto input=result.takeRequest();
                    input.asset=data;input.identity={iteration+1,23,++frame,31,41};
                    input.numericContext=FSSampling::NumericContext::capture();
                    input.time=time;input.duration=duration;input.usage.resize(count);
                    std::vector<LLPointer<LLJointState>> expected(count),actual(count);
                    for(unsigned j=0;j<count;++j)
                    {
                        const bool absent=(iteration+j+mask)%9==0;
                        input.usage[j]=absent ? 0 : mask;
                        if(!absent) {expected[j]=seeded(mask);actual[j]=seeded(mask);}
                        data->getJointMotion(j)->update(expected[j].get(),time,duration);
                    }
                    auto current=currentCopy(input);
                    const auto* maskBuffer=input.usage.data();
                    const auto* sampleBuffer=result.getJoints().data();
                    const auto oldSize=result.getJoints().size(),oldCapacity=result.getJoints().capacity();
                    const auto owners=data.use_count();
                    require(Probe::sample(std::move(input),result),"Valid pure sampling rejected");
                    require(!input.asset && Probe::matches(result,current),"Moved request or current-result identity changed");
                    require(result.getRequest().usage.data()==maskBuffer && data.use_count()==owners,"Submission copied masks or retained extra asset owners");
                    if(oldSize && oldCapacity>=count) require(result.getJoints().data()==sampleBuffer,"Reusable output capacity was discarded");
                    for(unsigned j=0;j<count;++j)
                    {
                        const auto& row=result.getJoints()[j];
                        require(row.usage==channels(*data->getJointMotion(j),current.usage[j]),"Applied-channel flags changed");
                        if(actual[j]) {apply(row,*actual[j]);compare(*expected[j],*actual[j]);}
                        else require(row.usage==0,"Absent state obtained a sample write");
                    }
                }
        require(data->mDuration==93.f && data->mLoopInPoint==17.f && data->mLoopOutPoint==29.f &&
                data->mHandPose==LLHandMotion::HAND_POSE_FIST,"Sampling modified list metadata");
    }
}
void identitiesAndMoves()
{
    auto data=asset(2,7);
    auto input=request(data,101);
    Probe::Result result;
    require(!result.isReady(),"Default result is ready");
    require(Probe::sample(std::move(input),result),"Identity fixture did not sample");
    auto current=currentCopy(result.getRequest());
    for(unsigned field=0;field<5;++field)
    {
        auto stale=currentCopy(current);
        U64* fields[]={&stale.identity.instance,&stale.identity.activation,&stale.identity.frame,&stale.identity.skeleton,&stale.identity.edit};
        ++*fields[field];
        require(!Probe::matches(result,stale),"Stale instance/activation/frame/skeleton/edit result accepted");
    }
    auto changed=currentCopy(current);changed.time=-0.f;
    require(!Probe::matches(result,changed),"Query time bits were not checked");
    changed=currentCopy(current);changed.duration=4.f;
    require(!Probe::matches(result,changed),"Query duration was not checked");
    changed=currentCopy(current);changed.usage[0]^=LLJointState::ROT;
    require(!Probe::matches(result,changed),"Changed usage mask accepted");
    changed=currentCopy(current);changed.usage.pop_back();
    require(!Probe::matches(result,changed),"Changed output shape accepted");
    changed=currentCopy(current);changed.asset=asset(2,7);
    require(!Probe::matches(result,changed),"Replacement asset identity accepted");
    changed=currentCopy(current);changed.asset=std::shared_ptr<const Probe::Asset>(data.get(),[](const Probe::Asset*){});
    require(!Probe::matches(result,changed),"Independent control block accepted as canonical owner");
    Probe::Result delivered=std::move(result);
    require(!result.isReady() && Probe::matches(delivered,current),"Result move did not transfer readiness");
    auto returned=delivered.takeRequest();
    require(!delivered.isReady() && !Probe::matches(delivered,current) && returned.asset==data,"Consumed result stayed valid or lost its lease");
    require(Probe::sample(std::move(returned),result) && Probe::matches(result,current),"Moved-from result could not be reused");
    auto zeroDuration=result.takeRequest();zeroDuration.duration=0.f;
    require(Probe::sample(std::move(zeroDuration),result),"Zero-duration request rejected");
    auto signedDuration=currentCopy(result.getRequest());signedDuration.duration=-0.f;
    require(!Probe::matches(result,signedDuration),"Query duration bits were not checked");
}
void malformedAndEmpty()
{
    auto data=asset(1,3);
    Probe::Result result;
    for(unsigned kind=0;kind<7;++kind)
    {
        require(Probe::sample(request(data,1),result),"Failure fixture did not prime a ready result");
        auto bad=request(data,1);
        if(kind==0) bad.asset.reset();
        if(kind==1) bad.usage.clear();
        if(kind==2) bad.time=std::numeric_limits<F32>::quiet_NaN();
        if(kind==3) bad.duration=std::numeric_limits<F32>::infinity();
        if(kind==4) bad.time=-std::numeric_limits<F32>::infinity();
        if(kind==5)
        {
            auto broken=std::make_shared<Probe::Asset>();broken->mJointMotionArray.push_back(nullptr);
            bad.asset=broken;
        }
        if(kind==6)
        {
            auto oversized=std::make_shared<Probe::Asset>();
            oversized->mJointMotionArray.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS+1,nullptr);
            bad.asset=oversized;bad.usage.resize(oversized->mJointMotionArray.size());
        }
        auto current=currentCopy(bad);
        require(!Probe::sample(std::move(bad),result),"Malformed request was sampled");
        require(!result.isReady() && result.getJoints().empty() && !Probe::matches(result,current),"Failed reuse left a valid stale result");
    }
    auto empty=std::make_shared<Probe::Asset>();
    require(Probe::sample(request(empty,9),result),"Empty owned request rejected");
    require(result.isReady() && result.getJoints().empty() && Probe::matches(result,result.getRequest()),"Empty request shape changed");
}
void backgroundLifetime()
{
    const LLUUID id("20000000-0000-0000-0000-000000000001");
    auto data=asset(1,5);
    std::weak_ptr<Probe::Asset> lifetime=data;
    auto input=request(data,301);
    auto expected=seeded(ALL);
    data->getJointMotion(0)->update(expected.get(),input.time,input.duration);
    LLKeyframeDataCache::addKeyframeData(id,data);
    data.reset();LLKeyframeDataCache::clear();
    require(!lifetime.expired(),"Owned request lost its evicted asset");
    Probe::Result result;
    bool sampled=false;
    std::thread worker([owned=std::move(input),&result,&sampled]() mutable {sampled=Probe::sample(std::move(owned),result);});
    input.usage.assign(8,0); // Reusing the moved-from source cannot alter submitted masks.
    worker.join();
    require(sampled && result.isReady() && !lifetime.expired(),"Owned background sampling did not retain its payload");
    auto actual=seeded(ALL);apply(result.getJoints()[0],*actual);compare(*expected,*actual);
    auto recycled=result.takeRequest();
    require(recycled.usage.size()==1 && recycled.usage[0]==ALL && !result.isReady(),"Submitted mask ownership was not exclusive");
    recycled.asset.reset();
    require(lifetime.expired(),"Consumed output retained an old asset lease");
}
int run()
{
    if (!FSSamplingTest::supportedContext("owned sampling")) return 0;
    oracleCases(); identitiesAndMoves(); malformedAndEmpty(); backgroundLifetime();
    std::cout<<poses<<" byte-exact actual joint-state comparisons and "<<checks<<" owned sampling checks passed\n";
    return 0;
}
}
int main() { return OwnedSamplingTest::run(); }
