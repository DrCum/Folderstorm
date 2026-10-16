/**
 * @file fsposingmotion.cpp
 * @brief Model for posing your (and other) avatar(s).
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (c) 2024 Angeldark Raymaker @ Second Life
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include <deque>
#include <boost/algorithm/string.hpp>
#include "fsposingmotion.h"
#include "llcharacter.h"

FSPosingMotion::FSPosingMotion(const LLUUID& id) : LLKeyframeMotion(id)
{
    mName = "fs_poser_pose";
    mMotionID = id;
    mJointMotionList = &dummyMotionList;
}

LLMotion::LLMotionInitStatus FSPosingMotion::onInitialize(LLCharacter* character)
{
    if (!character)
        return STATUS_FAILURE;

    mJointPoses.clear();

    LLJoint* targetJoint;
    for (S32 i = 0; (targetJoint = character->getCharacterJoint(i)); ++i)
    {
        if (!targetJoint)
            continue;

        FSJointPose jointPose = FSJointPose(targetJoint, POSER_JOINT_STATE);
        mJointPoses.push_back(jointPose);

        addJointState(jointPose.getJointState());
    }

    for (S32 i = 0; (targetJoint = character->findCollisionVolume(i)); ++i)
    {
        if (!targetJoint)
            continue;

        FSJointPose jointPose = FSJointPose(targetJoint, POSER_JOINT_STATE, true);
        mJointPoses.push_back(jointPose);

        addJointState(jointPose.getJointState());
    }

    return STATUS_SUCCESS;
}

void FSPosingMotion::configureAssistantRotations(std::function<bool()> allowed,std::function<void(bool)> stopped)
{
    mAssistantRotationOnly=true;mAssistantAllowed=std::move(allowed);mAssistantStopped=std::move(stopped);
    for(auto& joint:mJointPoses)setJointState(joint.getJointState()->getJoint(),0);
}

bool FSPosingMotion::setAssistantRotations(const std::map<std::string,LLQuaternion>& rotations,std::function<bool()> allowed)
{
    if(!mAssistantRotationOnly || mAssistantRevoked || !allowed || !allowed())return false;
    for(const auto& entry:rotations)if(!getJointPoseByJointName(entry.first))return false;
    mAssistantAllowed=std::move(allowed);
    for(auto& joint:mJointPoses)
    {
        const auto found=rotations.find(joint.jointName());
        if(found==rotations.end()){releaseAssistantJoint(joint);continue;}
        auto* native=joint.getJointState()->getJoint();
        if(!mAssistantOwned.count(joint.jointName()))
        {
            const auto q=native->getRotation();fs_pose::OwnedRotation owned;
            owned.original={{q.mQ[0],q.mQ[1],q.mQ[2],q.mQ[3]}};mAssistantOwned[joint.jointName()]=owned;
        }
        setJointState(native,LLJointState::ROT);joint.setAssistantTargetRotation(found->second);
    }
    return true;
}

bool FSPosingMotion::assistantAuthorityValid()
{
    if(!mAssistantRotationOnly)return true;
    if(!mAssistantRevoked && (!mAssistantAllowed || !mAssistantAllowed()))mAssistantRevoked=true;
    return !mAssistantRevoked;
}
void FSPosingMotion::releaseAssistantJoint(FSJointPose& joint)
{
    auto* native=joint.getJointState()->getJoint();
    const auto owned=mAssistantOwned.find(joint.jointName());
    if(native && owned!=mAssistantOwned.end())
    {
        const auto q=native->getRotation();const fs_pose::Rotation current{{q.mQ[0],q.mQ[1],q.mQ[2],q.mQ[3]}};
        if(owned->second.restore(current))
        {
            const auto& original=owned->second.original;
            native->setRotation(LLQuaternion(static_cast<F32>(original[0]),static_cast<F32>(original[1]),static_cast<F32>(original[2]),static_cast<F32>(original[3])));
        }
        else if(!owned->second.complete(current))mAssistantRestorationComplete=false;
        mAssistantOwned.erase(owned);
    }
    setJointState(native,0);
}
void FSPosingMotion::releaseAssistantRotations()
{for(auto& joint:mJointPoses)releaseAssistantJoint(joint);}

bool FSPosingMotion::onActivate()
{
    mActivationGeneration=LLUUID::generateNewID();return true;
}

bool FSPosingMotion::onUpdate(F32 time, U8* joint_mask)
{
    if(mAssistantRotationOnly && !assistantAuthorityValid())
    {
        releaseAssistantRotations();
        // Returning false routes through LLVOAvatarSelf::requestStopMotion and
        // sends a simulator STOP for this private UUID. The independent idle
        // tick removes this local motion instead. Revocation remains sticky.
        return true;
    }
    LLQuaternion targetRotation;
    LLQuaternion currentRotation;
    LLVector3 currentPosition;
    LLVector3 targetPosition;
    LLVector3 currentScale;
    LLVector3 targetScale;

    for (FSJointPose jointPose : mJointPoses)
    {
        LLJoint* joint = jointPose.getJointState()->getJoint();
        if (!joint)
            continue;

        if(mAssistantRotationOnly)
        {
            if(!(jointPose.getJointState()->getUsage() & LLJointState::ROT))continue;
            // Authority is checked at the actual timed write, without a TLS
            // scope spanning a viewer frame or coroutine yield.
            if(!assistantAuthorityValid()){releaseAssistantRotations();return true;}
            const auto rotation=slerp(mInterpolationTime,joint->getRotation(),jointPose.getTargetRotation());
            jointPose.getJointState()->setRotation(rotation);
            auto& owned=mAssistantOwned.at(jointPose.jointName());owned.expected={{rotation.mQ[0],rotation.mQ[1],rotation.mQ[2],rotation.mQ[3]}};owned.wrote=true;
            continue;
        }
        currentRotation = joint->getRotation();
        currentPosition = joint->getPosition();
        currentScale = joint->getScale();
        targetRotation  = jointPose.getTargetRotation();
        targetPosition  = jointPose.getTargetPosition();
        targetScale     = jointPose.getTargetScale();

        if (vectorsNotQuiteEqual(currentPosition, targetPosition))
        {
            currentPosition = lerp(currentPosition, targetPosition, mInterpolationTime);
            jointPose.getJointState()->setPosition(currentPosition);
        }

        if (quatsNotQuiteEqual(currentRotation, targetRotation))
        {
            currentRotation = slerp(mInterpolationTime, currentRotation, targetRotation);
            jointPose.getJointState()->setRotation(currentRotation);
        }

        if (vectorsNotQuiteEqual(currentScale, targetScale))
        {
            currentScale = lerp(currentScale, targetScale, mInterpolationTime);
            jointPose.getJointState()->setScale(currentScale);
        }
    }

    return true;
}

void FSPosingMotion::onDeactivate()
{
    if(mAssistantRotationOnly)
    {
        releaseAssistantRotations();mAssistantRevoked=true;
        mAssistantAllowed={};if(mAssistantStopped){auto stopped=std::move(mAssistantStopped);stopped(mAssistantRestorationComplete);}return;
    }
    revertJointsAndCollisionVolumes();
}

void FSPosingMotion::revertJointsAndCollisionVolumes()
{
    for (FSJointPose jointPose : mJointPoses)
    {
        jointPose.revertJoint();

        LLJoint* joint = jointPose.getJointState()->getJoint();
        if (!joint)
            continue;

        addJointToState(joint);
    }
}

bool FSPosingMotion::currentlyPosingJoint(FSJointPose* joint)
{
    if (!joint)
        return false;

    LLJoint* avJoint = joint->getJointState()->getJoint();
    if (!avJoint)
        return false;

    return currentlyPosingJoint(avJoint);
}

void FSPosingMotion::addJointToState(FSJointPose* joint)
{
    if (!joint)
        return;

    LLJoint* avJoint = joint->getJointState()->getJoint();
    if (!avJoint)
        return;

    setJointState(avJoint, POSER_JOINT_STATE);
}

void FSPosingMotion::removeJointFromState(FSJointPose* joint)
{
    if (!joint)
        return;

    LLJoint* avJoint = joint->getJointState()->getJoint();
    if (!avJoint)
        return;

    joint->revertJoint();

    setJointState(avJoint, 0);
}

void FSPosingMotion::addJointToState(LLJoint* joint)
{
    setJointState(joint, POSER_JOINT_STATE);
}

void FSPosingMotion::removeJointFromState(LLJoint* joint)
{
    setJointState(joint, 0);
}

void FSPosingMotion::setJointState(LLJoint* joint, U32 state)
{
    if (mJointPoses.empty())
        return;
    if (!joint)
        return;

    LLPose* pose = this->getPose();
    if (!pose)
        return;

    LLPointer<LLJointState> jointState = pose->findJointState(joint);
    if (jointState.isNull())
        return;

    pose->removeJointState(jointState);
    FSJointPose *jointPose = getJointPoseByJointName(joint->getName());
    if (!jointPose)
        return;

    jointPose->getJointState()->setUsage(state);
    addJointState(jointPose->getJointState());
}

FSJointPose* FSPosingMotion::getJointPoseByJointName(const std::string& name)
{
    if (name.empty() || mJointPoses.empty())
        return nullptr;

    for (auto poserJoint_iter = mJointPoses.begin(); poserJoint_iter != mJointPoses.end(); ++poserJoint_iter)
    {
        if (!boost::iequals(poserJoint_iter->jointName(), name))
            continue;

        return &*poserJoint_iter;
    }

    return nullptr;
}

FSJointPose* FSPosingMotion::getJointPoseByJointNumber(const S32 number)
{
    if (mJointPoses.empty())
        return nullptr;
    if (number < 0)
        return nullptr;

    for (auto poserJoint_iter = mJointPoses.begin(); poserJoint_iter != mJointPoses.end(); ++poserJoint_iter)
    {
        if (poserJoint_iter->getJointNumber() != number)
            continue;

        return &*poserJoint_iter;
    }

    return nullptr;
}

bool FSPosingMotion::currentlyPosingJoint(LLJoint* joint)
{
    if (mJointPoses.empty())
        return false;

    if (!joint)
        return false;

    LLPose* pose = getPose();
    if (!pose)
        return false;

    LLPointer<LLJointState> jointState = pose->findJointState(joint);
    if (jointState.isNull())
        return false;

    U32 state = jointState->getUsage();
    return (state & POSER_JOINT_STATE);
}

bool FSPosingMotion::allStartingRotationsAreZero() const
{
    for (auto poserJoint_iter = mJointPoses.begin(); poserJoint_iter != mJointPoses.end(); ++poserJoint_iter)
    {
        if (poserJoint_iter->isCollisionVolume())
            continue;

        if (!poserJoint_iter->isBaseRotationZero())
            return false;
    }

    return true;
}

void FSPosingMotion::setAllRotationsToZeroAndClearUndo()
{
    for (auto poserJoint_iter = mJointPoses.begin(); poserJoint_iter != mJointPoses.end(); ++poserJoint_iter)
    {
        poserJoint_iter->purgeUndoQueue();
        poserJoint_iter->setPublicRotation(true, true, POSER_CHANGE_ROTATION, LLQuaternion::DEFAULT);
    }
}

void FSPosingMotion::setJointBvhLock(FSJointPose* joint, bool lockInBvh)
{
    joint->zeroBaseRotation(lockInBvh);
}

bool FSPosingMotion::loadOtherMotionToBaseOfThisMotion(LLKeyframeMotion* motionToLoad, F32 timeToLoadAt, const std::vector<S32>& selectedJointNumbers)
{
    FSPosingMotion* motionToLoadAsFsPosingMotion = static_cast<FSPosingMotion*>(motionToLoad);
    if (!motionToLoadAsFsPosingMotion)
        return false;

    LLJoint::JointPriority priority = motionToLoad->getPriority();
    bool                   motionIsForAllJoints = selectedJointNumbers.empty();

    LLQuaternion rot;
    LLVector3    position, scale;
    bool         hasRotation = false, hasPosition = false, hasScale = false;

    for (auto poserJoint_iter = mJointPoses.begin(); poserJoint_iter != mJointPoses.end(); ++poserJoint_iter)
    {
        S32         jointNumber = poserJoint_iter->getJointNumber();
        std::string jointName   = poserJoint_iter->jointName();

        bool motionIsForThisJoint =
            std::find(selectedJointNumbers.begin(), selectedJointNumbers.end(), jointNumber) != selectedJointNumbers.end();
        if (!motionIsForAllJoints && !motionIsForThisJoint)
            continue;

        hasRotation = hasPosition = hasScale = false;
        motionToLoadAsFsPosingMotion->getJointStateAtTime(jointName, timeToLoadAt, &hasRotation, &rot, &hasPosition, &position, &hasScale, &scale);

        if (hasRotation && !poserJoint_iter->userHasSetBaseRotationToZero())
            poserJoint_iter->setBaseRotation(rot, priority);

        if (hasPosition)
            poserJoint_iter->setBasePosition(position, priority);

        if (hasScale)
            poserJoint_iter->setBaseScale(scale, priority);
    }

    return true;
}

void FSPosingMotion::getJointStateAtTime(std::string jointPoseName, F32 timeToLoadAt,
                                            bool* hasRotation, LLQuaternion* jointRotation,
                                            bool* hasPosition, LLVector3* jointPosition,
                                            bool* hasScale,    LLVector3* jointScale)
{
    if ( mJointMotionList == nullptr)
        return;

    for (U32 i = 0; i < mJointMotionList->getNumJointMotions(); i++)
    {
        JointMotion* jm = mJointMotionList->getJointMotion(i);
        if (!boost::iequals(jointPoseName, jm->mJointName))
            continue;

        *hasRotation = (jm->mRotationCurve.mNumKeys > 0);
        if (hasRotation)
            jointRotation->set(jm->mRotationCurve.getValue(timeToLoadAt, mJointMotionList->mDuration));

        *hasPosition = (jm->mPositionCurve.mNumKeys > 0);
        if (hasPosition)
            jointPosition->set(jm->mPositionCurve.getValue(timeToLoadAt, mJointMotionList->mDuration));

        *hasScale = (jm->mScaleCurve.mNumKeys > 0);
        if (hasScale)
            jointScale->set(jm->mScaleCurve.getValue(timeToLoadAt, mJointMotionList->mDuration));

        return;
    }
}

bool FSPosingMotion::otherMotionAnimatesJoints(LLKeyframeMotion* motionToQuery, const std::vector<S32>& recapturedJointNumbers)
{
    FSPosingMotion* motionToLoadAsFsPosingMotion = static_cast<FSPosingMotion*>(motionToQuery);
    if (!motionToLoadAsFsPosingMotion)
        return false;

    return motionToLoadAsFsPosingMotion->motionAnimatesJoints(recapturedJointNumbers);
}

// Do not try to access FSPosingMotion state; you are a LLKeyframeMotion cast as a FSPosingMotion, NOT an FSPosingMotion.
bool FSPosingMotion::motionAnimatesJoints(const std::vector<S32>& recapturedJointNumbers)
{
    if (mJointMotionList == nullptr)
        return false;

    for (U32 i = 0; i < mJointMotionList->getNumJointMotions(); i++)
    {
        JointMotion* jm = mJointMotionList->getJointMotion(i);
        LLJoint*     joint = mCharacter->getJoint(jm->mJointName);

        if (std::find(recapturedJointNumbers.begin(), recapturedJointNumbers.end(), joint->getJointNum()) == recapturedJointNumbers.end())
            continue;

        if (jm->mRotationCurve.mNumKeys > 0)
            return true;
    }

    return false;
}

void FSPosingMotion::resetBonePriority(const std::vector<S32>& boneNumbersToReset)
{
    for (S32 boneNumber : boneNumbersToReset)
    {
        for (auto poserJoint_iter = mJointPoses.begin(); poserJoint_iter != mJointPoses.end(); ++poserJoint_iter)
        {
            if (poserJoint_iter->getJointNumber() == boneNumber)
                poserJoint_iter->setJointPriority(LLJoint::LOW_PRIORITY);
        }
    }
}

bool FSPosingMotion::vectorsNotQuiteEqual(const LLVector3& v1, const LLVector3& v2) const
{
    if (vectorAxesAlmostEqual(v1.mV[VX], v2.mV[VX]) &&
        vectorAxesAlmostEqual(v1.mV[VY], v2.mV[VY]) &&
        vectorAxesAlmostEqual(v1.mV[VZ], v2.mV[VZ]))
        return false;

    return true;
}

bool FSPosingMotion::quatsNotQuiteEqual(const LLQuaternion& q1, const LLQuaternion& q2) const
{
    if (vectorAxesAlmostEqual(q1.mQ[VW], q2.mQ[VW]) &&
        vectorAxesAlmostEqual(q1.mQ[VX], q2.mQ[VX]) &&
        vectorAxesAlmostEqual(q1.mQ[VY], q2.mQ[VY]) &&
        vectorAxesAlmostEqual(q1.mQ[VZ], q2.mQ[VZ]))
        return false;

    if (vectorAxesAlmostEqual(q1.mQ[VW], -q2.mQ[VW]) &&
        vectorAxesAlmostEqual(q1.mQ[VX], -q2.mQ[VX]) &&
        vectorAxesAlmostEqual(q1.mQ[VY], -q2.mQ[VY]) &&
        vectorAxesAlmostEqual(q1.mQ[VZ], -q2.mQ[VZ]))
        return false;

    return true;
}
