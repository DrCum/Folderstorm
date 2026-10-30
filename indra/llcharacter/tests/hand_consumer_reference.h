// Frozen PR115 hand callback, captured before the cleanup change. Test-only reference.
#ifndef FS_HAND_CONSUMER_REFERENCE_H
#define FS_HAND_CONSUMER_REFERENCE_H
#include "llhandmotion.h"
#include "llcharacter.h"
extern const char* gHandPoseNames[LLHandMotion::NUM_HAND_POSES];
namespace FSHandReference
{
inline bool update(LLHandMotion& hand, F32 time, U8* joint_mask)
{
    auto& mCharacter=hand.mCharacter;
    auto& mLastTime=hand.mLastTime;
    auto& mCurrentPose=hand.mCurrentPose;
    auto& mNewPose=hand.mNewPose;
    constexpr F32 HAND_MORPH_BLEND_TIME=.2f;
    constexpr auto HAND_POSE_RELAXED=LLHandMotion::HAND_POSE_RELAXED;
    constexpr auto HAND_POSE_SPREAD=LLHandMotion::HAND_POSE_SPREAD;
    constexpr auto NUM_HAND_POSES=LLHandMotion::NUM_HAND_POSES;
    using eHandPose=LLHandMotion::eHandPose;
    LL_PROFILE_ZONE_SCOPED;
    eHandPose *requestedHandPose;

    F32 timeDelta = time - mLastTime;
    mLastTime = time;

    requestedHandPose = (eHandPose *)mCharacter->getAnimationData("Hand Pose");
    // check to see if requested pose has changed
    if (!requestedHandPose)
    {
        if (mNewPose != HAND_POSE_RELAXED && mNewPose != mCurrentPose)
        {
            // Only set param weight for poses other than
            // default (HAND_POSE_SPREAD); HAND_POSE_SPREAD
            // is not an animatable morph!
            if (mNewPose != HAND_POSE_SPREAD)
            {
                mCharacter->setVisualParamWeight(gHandPoseNames[mNewPose], 0.f);
            }

            // Reset morph weight for current pose back to its
            // full extend or it might be stuck somewhere in the middle if a
            // pose is requested and the old pose is requested again shortly
            // after while still blending to the other pose!
            if (mCurrentPose != HAND_POSE_SPREAD)
            {
                mCharacter->setVisualParamWeight(gHandPoseNames[mCurrentPose], 1.f);
            }

            // Update visual params now if we won't blend
            if (mCurrentPose == HAND_POSE_RELAXED)
            {
                mCharacter->updateVisualParams();
            }
        }
        mNewPose = HAND_POSE_RELAXED;
    }
    else
    {
        // Sometimes we seem to get garbage here, with poses that are out of bounds.
        // So check for a valid pose first.
        if (*requestedHandPose >= 0 && *requestedHandPose < NUM_HAND_POSES)
        {
            // This is a new morph we didn't know about before:
            // Reset morph weight for both current and new pose
            // back their starting values while still blending.
            if (*requestedHandPose != mNewPose && mNewPose != mCurrentPose)
            {
                if (mNewPose != HAND_POSE_SPREAD)
                {
                    mCharacter->setVisualParamWeight(gHandPoseNames[mNewPose], 0.f);
                }

                // Reset morph weight for current pose back to its full extend
                // or it might be stuck somewhere in the middle if a pose is
                // requested and the old pose is requested again shortly after
                // while still blending to the other pose!
                if (mCurrentPose != HAND_POSE_SPREAD)
                {
                    mCharacter->setVisualParamWeight(gHandPoseNames[mCurrentPose], 1.f);
                }

                // Update visual params now if we won't blend
                if (mCurrentPose == *requestedHandPose)
                {
                    mCharacter->updateVisualParams();
                }
            }
            mNewPose = *requestedHandPose;
        }
        else
        {
            LL_WARNS() << "Requested hand pose out of range. Ignoring requested pose." << LL_ENDL;
        }
    }

    mCharacter->removeAnimationData("Hand Pose");
    mCharacter->removeAnimationData("Hand Pose Priority");

//  if (requestedHandPose)
//      LL_INFOS() << "Hand Pose " << *requestedHandPose << LL_ENDL;

    // if we are still blending...
    if (mCurrentPose != mNewPose)
    {
        F32 incomingWeight = 1.f;
        F32 outgoingWeight = 0.f;

        if (mNewPose != HAND_POSE_SPREAD)
        {
            incomingWeight = mCharacter->getVisualParamWeight(gHandPoseNames[mNewPose]);
            incomingWeight += (timeDelta / HAND_MORPH_BLEND_TIME);
            incomingWeight = llclamp(incomingWeight, 0.f, 1.f);
            mCharacter->setVisualParamWeight(gHandPoseNames[mNewPose], incomingWeight);
        }

        if (mCurrentPose != HAND_POSE_SPREAD)
        {
            outgoingWeight = mCharacter->getVisualParamWeight(gHandPoseNames[mCurrentPose]);
            outgoingWeight -= (timeDelta / HAND_MORPH_BLEND_TIME);
            outgoingWeight = llclamp(outgoingWeight, 0.f, 1.f);
            mCharacter->setVisualParamWeight(gHandPoseNames[mCurrentPose], outgoingWeight);
        }

        mCharacter->updateVisualParams();

        if (incomingWeight == 1.f && outgoingWeight == 0.f)
        {
            mCurrentPose = mNewPose;
        }
    }

    return true;
}
}
#endif
