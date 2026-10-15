/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_ASSISTANT_GESTURE_MODEL_H
#define FS_ASSISTANT_GESTURE_MODEL_H
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace fs_assistant_gesture {
enum class Effect { AnimationStart, AnimationStop, Sound, Chat, WaitTime, WaitAnimations };
struct Step { Effect effect; std::string value; double seconds=0; };
struct Plan { std::string trigger; std::vector<Step> steps; bool chat=false, sound=false; };
constexpr std::size_t maxSteps=64, maxAssetBytes=16384;
constexpr double maxDuration=120, maxWait=60;
inline bool uuid(const std::string& value)
{
    if(value.size()!=36 || value=="00000000-0000-0000-0000-000000000000")return false;
    for(std::size_t i=0;i<value.size();++i)
        if((i==8 || i==13 || i==18 || i==23)?value[i]!='-':
           std::string("0123456789abcdefABCDEF").find(value[i])==std::string::npos)return false;
    return true;
}
inline bool valid(const std::vector<Step>& steps)
{
    if(steps.empty() || steps.size()>maxSteps)return false;
    double duration=0;std::set<std::string> started;
    for(const auto& step:steps)
    {
        if(!std::isfinite(step.seconds) || step.seconds<0 || step.seconds>60)return false;
        if(step.effect==Effect::WaitTime)duration+=step.seconds;
        if(step.effect==Effect::Chat && (step.value.empty() || step.value.size()>1023 || step.value.find('\0')!=std::string::npos))return false;
        if(step.effect==Effect::AnimationStart){if(!uuid(step.value))return false;started.insert(step.value);}
        if(step.effect==Effect::AnimationStop){if(!started.erase(step.value))return false;}
        if(step.effect==Effect::Sound && !uuid(step.value))return false;
    }
    return duration<=maxDuration;
}
// Decode the native v2 positional ASCII format ourselves. Native deserialize
// trusts step_count and ignores failed numeric reads. No native allocation or
// command parser is reached, even for malformed assets.
inline bool decode(std::string bytes,Plan& out,std::string& error)
{
    auto fail=[&](const char* reason){error=reason;return false;};
    if(bytes.empty() || bytes.size()>maxAssetBytes)return fail("asset_size_out_of_bounds");
    if(bytes.back()=='\0')bytes.pop_back();
    if(bytes.find('\0')!=std::string::npos)return fail("embedded_nul");
    std::vector<std::string> lines;std::size_t at=0;
    while(at<bytes.size())
    {
        const auto end=bytes.find('\n',at);
        if(end==std::string::npos || lines.size()>=6+maxSteps*4)return fail("malformed_or_excess_fields");
        auto line=bytes.substr(at,end-at);if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line.size()>1023)return fail("field_too_long");
        lines.push_back(std::move(line));at=end+1;
    }
    std::size_t pos=0;
    auto number=[&](double& value){if(pos>=lines.size() || lines[pos].empty())return false;char* end=nullptr;
        const auto& line=lines[pos++];value=std::strtod(line.c_str(),&end);
        return end==line.c_str()+line.size() && std::isfinite(value);};
    auto integer=[&](double& value,double maximum){return number(value) && value>=0 && value<=maximum && std::floor(value)==value;};
    double version,key,mask,count;
    if(!integer(version,2) || version!=2 || !integer(key,255) || !integer(mask,4294967295.) || pos+2>lines.size())return fail("bad_header");
    Plan plan;plan.trigger=lines[pos++];++pos;
    if(!integer(count,maxSteps) || count<1)return fail("step_count_out_of_bounds");
    for(std::size_t i=0;i<static_cast<std::size_t>(count);++i)
    {
        double type,flags,seconds=0;
        if(!integer(type,3))return fail("unknown_step");
        std::string value;
        if(type==0 || type==1)
        {if(pos+2>lines.size())return fail("missing_asset");++pos;value=lines[pos++];}
        else if(type==2){if(pos>=lines.size())return fail("missing_chat");value=lines[pos++];}
        else if(!number(seconds) || seconds<0 || seconds>maxWait)return fail("wait_out_of_bounds");
        if(!integer(flags,7))return fail("bad_flags");
        Effect effect;
        if(type==0){if(flags>1)return fail("unsupported_animation_flags");effect=flags==1?Effect::AnimationStop:Effect::AnimationStart;}
        else if(type==1){if(flags!=0)return fail("unsupported_sound_flags");effect=Effect::Sound;plan.sound=true;}
        else if(type==2){if(flags!=0)return fail("unsupported_chat_flags");effect=Effect::Chat;plan.chat=true;}
        else {if(flags!=1 && flags!=2)return fail("unsupported_wait_flags");effect=flags==1?Effect::WaitTime:Effect::WaitAnimations;if(flags==2)seconds=maxWait;}
        plan.steps.push_back({effect,value,seconds});
    }
    if(pos!=lines.size() || !valid(plan.steps))return fail("invalid_or_unsafe_steps");
    out=std::move(plan);return true;
}
// Only leased animation IDs are retained. Any native/manual request invalidates
// the old lease; an obsolete cancellation can never stop the newer request.
class AnimationLeases {
public:
    std::uint64_t acquire(const std::string& id){return versions[id]=++serial;}
    void observed(const std::string& id){auto it=versions.find(id);if(it!=versions.end())it->second=++serial;}
    std::uint64_t current(const std::string& id)const{auto it=versions.find(id);return it==versions.end()?0:it->second;}
    bool owns(const std::string& id,std::uint64_t version)const
    {auto it=versions.find(id);return it!=versions.end() && it->second==version;}
    void release(const std::string& id){versions.erase(id);}
    void invalidate(){for(auto& entry:versions)entry.second=++serial;}
    void clear(){versions.clear();}
private: std::map<std::string,std::uint64_t> versions;std::uint64_t serial=0;
};
inline bool cleanupAllowed(bool sameLogin,bool owns,bool foreignSource)
{return sameLogin && owns && !foreignSource;}
inline bool animationAvailable(bool foreignSource,bool signaled,bool playing,bool locallyActive)
{return !foreignSource && !signaled && !playing && !locallyActive;}
template<typename Builtin> bool animationPolicy(const Plan& plan,Builtin builtin,std::string& error)
{
    for(const auto& step:plan.steps)
        if((step.effect==Effect::AnimationStart || step.effect==Effect::AnimationStop) && builtin(step.value))
        {error="unsupported_builtin_animation: native animation states can change avatar controls";return false;}
    return true;
}
class Timeline {
public:
    explicit Timeline(std::vector<Step> steps,double now):mSteps(std::move(steps)),mStarted(now){}
    template<typename Allowed,typename Pending,typename Apply>
    void tick(double now,Allowed allowed,Pending pending,Apply apply)
    {
        if(done())return;
        if(!allowed()){stop("authority_or_source_changed");return;}
        if(now-mStarted>=maxDuration){stop("playback_duration_limit");return;}
        if(mWaiting)
        {
            const auto& step=mSteps[mIndex];
            if(step.effect==Effect::WaitTime){if(now-mWaitStarted<step.seconds)return;}
            else if(pending()){if(now-mWaitStarted>=maxWait)stop("animation_wait_timeout");return;}
            mWaiting=false;++mIndex;
        }
        while(mIndex<mSteps.size())
        {
            if(!allowed()){stop("authority_or_source_changed");return;}
            const auto& step=mSteps[mIndex];
            if(step.effect==Effect::WaitTime || step.effect==Effect::WaitAnimations)
            {mWaiting=true;mWaitStarted=now;return;}
            if(!apply(step)){stop("effect_refused");return;}++mIndex;
        }
        if(pending())return;
        stop("completed");
    }
    void stop(const std::string& reason){if(mReason.empty())mReason=reason;}
    bool done()const{return !mReason.empty();}
    const std::string& reason()const{return mReason;}
    std::size_t index()const{return mIndex;}
private:
    std::vector<Step> mSteps;double mStarted,mWaitStarted=0;std::size_t mIndex=0;bool mWaiting=false;std::string mReason;
};
}
#endif
