// Frozen actual update and keyframe application from dcbc51a058d931722a6db5b4109d859e6b0d6200.
// Only class/method names are changed; keep this oracle independent of the phase refactor.
bool Probe::legacyUpdate(F32 time, U8* joint_mask)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR;
    // llassert(time >= 0.f);       // This will fire
    time = llmax(0.f, time);

    if (mJointMotionList->mLoop)
    {
        if (mJointMotionList->mDuration == 0.0f)
        {
            time = 0.f;
            mLastLoopedTime = 0.0f;
        }
        else if (mStopped)
        {
            mLastLoopedTime = llmin(mJointMotionList->mDuration, mLastLoopedTime + time - mLastUpdateTime);
        }
        else if (time > mJointMotionList->mLoopOutPoint)
        {
            if ((mJointMotionList->mLoopOutPoint - mJointMotionList->mLoopInPoint) == 0.f)
            {
                mLastLoopedTime = mJointMotionList->mLoopOutPoint;
            }
            else
            {
                mLastLoopedTime = mJointMotionList->mLoopInPoint +
                    fmod(time - mJointMotionList->mLoopOutPoint,
                    mJointMotionList->mLoopOutPoint - mJointMotionList->mLoopInPoint);
            }
        }
        else
        {
            mLastLoopedTime = time;
        }
    }
    else
    {
        mLastLoopedTime = time;
    }

    legacyApplyKeyframes(mLastLoopedTime);

    applyConstraints(mLastLoopedTime, joint_mask);

    mLastUpdateTime = time;

    return mLastLoopedTime <= mJointMotionList->mDuration;
}

void Probe::legacyApplyKeyframes(F32 time)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_AVATAR; // </FS:Perf> Attribute curve sampling and state application.
    llassert_always (mJointMotionList->getNumJointMotions() <= mJointStates.size());
    for (U32 i=0; i<mJointMotionList->getNumJointMotions(); i++)
    {
        mJointMotionList->getJointMotion(i)->update(mJointStates[i],
                                                      time,
                                                      mJointMotionList->mDuration );
    }

    LLJoint::JointPriority* pose_priority = (LLJoint::JointPriority* )mCharacter->getAnimationData("Hand Pose Priority");
    if (pose_priority)
    {
        if (mJointMotionList->mMaxPriority >= *pose_priority)
        {
            // <FS:Perf> Copy the winning values into character-owned storage.
            // mCharacter->setAnimationData("Hand Pose", &mJointMotionList->mHandPose);
            // mCharacter->setAnimationData("Hand Pose Priority", &mJointMotionList->mMaxPriority);
            mCharacter->setHandPoseAnimationData(mJointMotionList->mHandPose, mJointMotionList->mMaxPriority);
            // </FS:Perf>
        }
    }
    else
    {
        // <FS:Perf> Copy the winning values into character-owned storage.
        // mCharacter->setAnimationData("Hand Pose", &mJointMotionList->mHandPose);
        // mCharacter->setAnimationData("Hand Pose Priority", &mJointMotionList->mMaxPriority);
        mCharacter->setHandPoseAnimationData(mJointMotionList->mHandPose, mJointMotionList->mMaxPriority);
        // </FS:Perf>
    }
}
