// Real hand callbacks and visual-param storage, without a viewer or mesh.
#include "linden_common.h"
#include "llcharacter.h"
#include "llhandmotion.h"
#include "keyframe_test_character.h"
#include "hand_consumer_reference.h"
#include "sampling_allocation_probe.h"
#include "llsimdmath.h"
#include <array>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace HandConsumerTest
{
unsigned checks=0;
void require(bool value,const char* message)
{
    ++checks;
    if(!value){std::cerr<<message<<" at "<<checks<<'\n';std::exit(1);}
}
U32 bits(F32 value){U32 out;std::memcpy(&out,&value,sizeof(out));return out;}
const std::string poseKey="Hand Pose",priorityKey="Hand Pose Priority",otherKey="other";
class MapCharacter : public FSKeyframeTests::Character
{
public:
    int other=42;
    MapCharacter(){setAnimationData(otherKey,&other);}
    unsigned mapState() const
    {
        return unsigned(mAnimationData.count(poseKey)) | (unsigned(mAnimationData.count(priorityKey))<<1) |
            (unsigned(mAnimationData.count(otherKey))<<2);
    }
    bool otherIntact(){return getAnimationData(otherKey)==&other && other==42;}
};
class TraceCharacter;
class ParamInfo : public LLVisualParamInfo
{
public:
    explicit ParamInfo(unsigned index)
    {
        mID=S32(100+index);mName=LLHandMotion::getHandPoseName(LLHandMotion::eHandPose(index));
        LLStringUtil::toLower(mName);mMinWeight=0.f;mMaxWeight=1.f;mDefaultWeight=0.f;mSex=SEX_BOTH;
    }
};
class Param : public LLVisualParam
{
    ParamInfo info;
    TraceCharacter& owner;
    unsigned index;
public:
    Param(TraceCharacter& character,unsigned i):info(i),owner(character),index(i){mInfo=&info;mID=info.getID();}
    void apply(ESex sex) override;
};
class TraceCharacter : public MapCharacter
{
public:
    struct Event{U32 kind,index,value,map,upload;};
    std::array<Event,128> events{};
    size_t eventCount=0;
    bool recording=false;
    unsigned mutateOn=0,mutations=0;
    LLHandMotion::eHandPose replacement=LLHandMotion::HAND_POSE_TYPING;
    LLJoint::JointPriority replacementPriority=LLJoint::HIGH_PRIORITY;
    TraceCharacter()
    {
        for(unsigned i=1;i<LLHandMotion::NUM_HAND_POSES;++i)addVisualParam(new Param(*this,i));
        LLCharacter::setVisualParamWeight("Hands_Relaxed",1.f);
        recording=true;
    }
    void record(unsigned kind,unsigned index,F32 value,bool upload=false)
    {
        if(!recording)return;
        require(eventCount<events.size(),"Visual-param event capacity");
        events[eventCount++]={kind,index,bits(value),mapState(),unsigned(upload)};
    }
    void mutate(unsigned kind)
    {
        if(mutateOn!=kind)return;
        mutateOn=0;++mutations;
        setAnimationData(poseKey,&replacement);setAnimationData(priorityKey,&replacementPriority);
    }
    bool setVisualParamWeight(const char* name,F32 value,bool upload=false) override
    {
        unsigned index=0;
        for(unsigned i=1;i<LLHandMotion::NUM_HAND_POSES;++i)if(std::strcmp(name,gHandPoseNames[i])==0){index=i;break;}
        record(1,index,value,upload);mutate(1);
        return LLCharacter::setVisualParamWeight(name,value,upload);
    }
    void updateVisualParams() override
    {
        record(2,0,0.f);mutate(2);LLCharacter::updateVisualParams();
    }
    std::vector<U32> state(LLHandMotion& hand,bool returned)
    {
        std::vector<U32> out{U32(hand.mCurrentPose),U32(hand.mNewPose),bits(hand.mLastTime),U32(returned),mapState(),mutations};
        for(unsigned i=1;i<LLHandMotion::NUM_HAND_POSES;++i)
        {
            auto* param=getVisualParam(S32(100+i));require(param!=nullptr,"Registered visual param");
            out.push_back(bits(param->getWeight()));out.push_back(bits(param->getLastWeight()));
        }
        out.push_back(U32(eventCount));
        for(size_t i=0;i<eventCount;++i)
        {
            const auto& e=events[i];out.insert(out.end(),{e.kind,e.index,e.value,e.map,e.upload});
        }
        return out;
    }
};
void Param::apply(ESex sex)
{
    const F32 effective=(getSex()&sex)?getWeight():getDefaultWeight();
    owner.record(3,index,effective);setLastWeight(effective);
}
void publish(TraceCharacter& c,unsigned mode,LLHandMotion::eHandPose& pose,LLJoint::JointPriority& priority)
{
    if(mode==0)c.setHandPoseAnimationData(pose,priority);
    else
    {
        if(mode==1||mode==2)c.setAnimationData(poseKey,&pose);
        if(mode==1||mode==3)c.setAnimationData(priorityKey,&priority);
        if(mode==4){c.setAnimationData(poseKey,nullptr);c.setAnimationData(priorityKey,&priority);}
    }
}
int correctness(bool trace)
{
    unsigned frames=0,mutations=0;
    for(unsigned direction=0;direction<2;++direction)for(unsigned start=0;start<LLHandMotion::NUM_HAND_POSES;++start)
    {
        TraceCharacter actual,reference;
        LLHandMotion hand(LLUUID::null),frozen(LLUUID::null);
        require(hand.onInitialize(&actual)==LLMotion::STATUS_SUCCESS && frozen.onInitialize(&reference)==LLMotion::STATUS_SUCCESS,"Hand initialization");
        const unsigned target=(start+(direction?13:1))%LLHandMotion::NUM_HAND_POSES;
        const unsigned third=(target+7)%LLHandMotion::NUM_HAND_POSES;
        const unsigned requests[]={start,target,target,third,0,0,14,15,0,target,1,0,start,0,target,0};
        const unsigned modes[]={0,1,1,1,5,3,1,1,4,2,0,1,1,5,1,5};
        const F32 times[]={.25f,.28125f,.28125f,.3125f,.34375f,.328125f,.375f,.40625f,.4375f,.5f,.75f,.875f,.90625f,.9375f,.96875f,1.f};
        LLJoint::JointPriority priority=LLJoint::MEDIUM_PRIORITY;
        for(unsigned step=0;step<16;++step)
        {
            auto request=LLHandMotion::eHandPose(requests[step]);
            publish(actual,modes[step],request,priority);publish(reference,modes[step],request,priority);
            actual.eventCount=reference.eventCount=0;
            if(step==12)actual.mutateOn=reference.mutateOn=1;
            if(step==14)actual.mutateOn=reference.mutateOn=2;
            const bool a=hand.onUpdate(times[step],nullptr),b=FSHandReference::update(frozen,times[step],nullptr);
            const auto state=actual.state(hand,a);
            require(state==reference.state(frozen,b),"Hand morph values, call order, map state or returns differ from frozen callback");
            require(actual.otherIntact()&&reference.otherIntact(),"Hand cleanup removed unrelated data");
            if(trace){std::cout<<"frame,"<<direction<<','<<start<<','<<step;for(U32 v:state)std::cout<<','<<v;std::cout<<'\n';}
            ++frames;
        }
        mutations+=actual.mutations;
    }
    require(frames==448&&mutations>28,"Missing transition or callback-republication coverage");
    std::cout<<checks<<" actual hand/visual-param checks across "<<frames<<" frozen frames; callback republications="<<mutations<<'\n';
    return 0;
}

int allocations(bool cold)
{
#if FS_SAMPLING_ALLOCATION_PROBE
#if TRACY_ENABLE
    std::cout<<"SKIP allocation injection: requires Tracy OFF\n";return 0;
#endif
    MapCharacter c;LLHandMotion hand(LLUUID::null);require(hand.onInitialize(&c)==LLMotion::STATUS_SUCCESS,"Allocation hand initialization");
    auto pose=LLHandMotion::HAND_POSE_RELAXED;auto priority=LLJoint::HIGH_PRIORITY;
    if(cold)
    {
        FSSamplingAlloc::Stats nameCost;
        {FSSamplingAlloc::Scope scope;{std::string probe(priorityKey.c_str());require(probe==priorityKey,"Cold name probe");}nameCost=FSSamplingAlloc::snapshot();}
        c.setAnimationData(poseKey,&pose);c.setAnimationData(priorityKey,&priority);
        bool failed=false;
        {FSSamplingAlloc::Scope scope;FSSamplingAlloc::failureCountdown=0;
         try{hand.onUpdate(1.f,nullptr);}catch(const std::bad_alloc&){failed=true;}FSSamplingAlloc::failureCountdown=-1;}
        require(failed==(nameCost.calls!=0),"Cold name allocation did not follow string storage policy");
        require(c.mapState()==(failed?6u:4u)&&c.otherIntact(),"Cold failure changed cleanup order or unrelated data");
        FSSamplingAlloc::Stats retry;
        {FSSamplingAlloc::Scope scope;require(hand.onUpdate(2.f,nullptr),"Cold retry failed");retry=FSSamplingAlloc::snapshot();}
        require(c.mapState()==4,"Cold retry left a hand entry");
        std::cout<<"cold_key_retry,"<<retry.calls<<','<<retry.requested<<','<<retry.live-retry.baseline<<'\n';
    }
    else
    {
        c.setHandPoseAnimationData(pose,priority);require(hand.onUpdate(1.f,nullptr),"Warmup failed");
        for(unsigned mode=0;mode<5;++mode)for(unsigned i=0;i<32;++i)
        {
            if(mode==1)c.setHandPoseAnimationData(pose,priority);
            if(mode==2){c.setAnimationData(poseKey,&pose);c.setAnimationData(priorityKey,&priority);}
            if(mode==3)c.setAnimationData(poseKey,&pose);
            if(mode==4)c.setAnimationData(priorityKey,&priority);
            FSSamplingAlloc::Stats s;
            {FSSamplingAlloc::Scope scope;require(hand.onUpdate(2.f+i,nullptr),"Consumer failed");s=FSSamplingAlloc::snapshot();}
            require(!s.calls&&!s.requested&&s.live==s.baseline,"Warm consumer retained key-name allocation");
            require(c.mapState()==4&&c.otherIntact(),"Consumer failed to clear current entries");
        }
        TraceCharacter morph;LLHandMotion changing(LLUUID::null);require(changing.onInitialize(&morph)==LLMotion::STATUS_SUCCESS,"Morph allocation initialization");
        for(unsigned i=0;i<64;++i)
        {
            morph.setHandPoseAnimationData(LLHandMotion::eHandPose(i%LLHandMotion::NUM_HAND_POSES),priority);morph.eventCount=0;
            FSSamplingAlloc::Stats s;
            {FSSamplingAlloc::Scope scope;require(changing.onUpdate((i+1)*.03125f,nullptr),"Changing consumer failed");s=FSSamplingAlloc::snapshot();}
            require(!s.calls&&!s.requested&&s.live==s.baseline,"Changing morph consumer allocates");
        }
    }
    c.setAnimationData(poseKey,&pose);c.setAnimationData(priorityKey,&priority);
    FSSamplingAlloc::Stats s;
    {FSSamplingAlloc::Scope scope;FSSamplingAlloc::failureCountdown=0;
     try{require(hand.onUpdate(40.f,nullptr),"Warm fault-injection callback failed");}catch(...){FSSamplingAlloc::failureCountdown=-1;throw;}
     FSSamplingAlloc::failureCountdown=-1;s=FSSamplingAlloc::snapshot();}
    require(!s.calls&&s.live==s.baseline&&c.mapState()==4&&c.otherIntact(),"Warm cleanup failed under allocation pressure");
    std::cout<<checks<<" hand-consumer allocation/failure checks passed\n";
#endif
    return 0;
}
}
int main(int argc,char** argv)
{
    LLVector4a::initClass();
    if(FS_SAMPLING_ALLOCATION_PROBE)
        return argc==2&&std::string(argv[1])=="--allocations"?HandConsumerTest::allocations(false):
            argc==2&&std::string(argv[1])=="--cold"?HandConsumerTest::allocations(true):0;
    return argc==1||(argc==2&&std::string(argv[1])=="--trace")?HandConsumerTest::correctness(argc==2):1;
}
