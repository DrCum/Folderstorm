/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_SCRIPT_TEST_MODEL_H
#define FS_SCRIPT_TEST_MODEL_H
#include <cstddef>
#include <cstdint>
#include <string>
namespace fs_script_test {
class ObservationWindow
{
public:
    void action(std::uint64_t latest){mBaseline=latest;mCursor=latest;}
    void consume(std::uint64_t sequence){if(sequence>mCursor)mCursor=sequence;}
    std::uint64_t cursor()const{return mCursor;}
    std::uint64_t baseline()const{return mBaseline;}
private:std::uint64_t mBaseline=0,mCursor=0;
};
class Sequence
{
public:
    void start(std::size_t count,double at,double seconds)
    {mCount=count;mIndex=0;mStepAt=at;mDeadline=at+seconds;mState="running";mReason.clear();}
    bool running()const{return mState=="running";}
    void check(bool authority,bool sameSource,bool gap,double at)
    {
        if(!running())return;
        if(!authority || !sameSource){finish("stopped","source_or_authority_changed");return;}
        if(gap){finish("inconclusive","diagnostic_gap");return;}
        if(at>=mDeadline)finish("failed","run_timeout");
    }
    void next(double at)
    {if(!running())return;if(++mIndex>=mCount)finish("passed","");else mStepAt=at;}
    bool stepExpired(double at,double seconds)const{return at>=mStepAt+seconds;}
    void stop(){if(running())finish("stopped","explicit_stop");}
    void finish(const std::string& state,const std::string& reason){mState=state;mReason=reason;}
    const std::string& state()const{return mState;}
    const std::string& reason()const{return mReason;}
    std::size_t index()const{return mIndex;}
    double stepAt()const{return mStepAt;}
private:
    std::size_t mCount=0,mIndex=0;double mStepAt=0.,mDeadline=0.;std::string mState="idle",mReason;
};
}
#endif
