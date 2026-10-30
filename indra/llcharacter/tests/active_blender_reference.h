#ifndef FS_ACTIVE_BLENDER_REFERENCE_H
#define FS_ACTIVE_BLENDER_REFERENCE_H
// Frozen outer collection from b8c82e686def15937fdfad1620845d557e995a89.
// Only the class name changes. Numerical joint blending remains shared and unchanged.
#include "llpose.h"
#include "llmotion.h"
#include "llstl.h"
#include <list>
namespace FSActiveBlenderReference
{
class ReferenceBlender
{
protected:
    typedef std::list<LLJointStateBlender*> blender_list_t;
    typedef std::map<LLJoint*,LLJointStateBlender*> blender_map_t;
    blender_map_t mJointStateBlenderPool;
    blender_list_t mActiveBlenders;

    S32         mNextPoseSlot;
    LLPose      mBlendedPose;
public:
    // Constructor
    ReferenceBlender();
    // Destructor
    ~ReferenceBlender();

    // request motion joint states to be added to pose blender joint state records
    bool addMotion(LLMotion* motion);

    // blend all joint states and apply to skeleton
    void blendAndApply();

    // removes all joint state blenders from last time
    void clearBlenders();

    // blend all joint states and cache results
    void blendAndCache(bool reset_cached_joints);

    // interpolate all joints towards cached values
    void interpolate(F32 u);

    LLPose* getBlendedPose() { return &mBlendedPose; }
};
ReferenceBlender::ReferenceBlender()
    : mNextPoseSlot(0)
{
}

ReferenceBlender::~ReferenceBlender()
{
    for_each(mJointStateBlenderPool.begin(), mJointStateBlenderPool.end(), DeletePairedPointer());
    mJointStateBlenderPool.clear();
}

//-----------------------------------------------------------------------------
// addMotion()
//-----------------------------------------------------------------------------
bool ReferenceBlender::addMotion(LLMotion* motion)
{
    LLPose* pose = motion->getPose();

    for(LLJointState* jsp = pose->getFirstJointState(); jsp; jsp = pose->getNextJointState())
    {
        LLJoint *jointp = jsp->getJoint();
        LLJointStateBlender* joint_blender;
        if (mJointStateBlenderPool.find(jointp) == mJointStateBlenderPool.end())
        {
            // this is the first time we are animating this joint
            // so create new jointblender and add it to our pool
            joint_blender = new LLJointStateBlender();
            mJointStateBlenderPool[jointp] = joint_blender;
        }
        else
        {
            joint_blender = mJointStateBlenderPool[jointp];
        }

        if (jsp->getPriority() == LLJoint::USE_MOTION_PRIORITY)
        {
            joint_blender->addJointState(jsp, motion->getPriority(), motion->getBlendType() == LLMotion::ADDITIVE_BLEND);
        }
        else
        {
            joint_blender->addJointState(jsp, jsp->getPriority(), motion->getBlendType() == LLMotion::ADDITIVE_BLEND);
        }

        // add it to our list of active blenders
        if (std::find(mActiveBlenders.begin(), mActiveBlenders.end(), joint_blender) == mActiveBlenders.end())
        {
            mActiveBlenders.push_front(joint_blender);
        }
    }
    return true;
}

//-----------------------------------------------------------------------------
// blendAndApply()
//-----------------------------------------------------------------------------
void ReferenceBlender::blendAndApply()
{
    for (blender_list_t::iterator iter = mActiveBlenders.begin();
         iter != mActiveBlenders.end(); )
    {
        LLJointStateBlender* jsbp = *iter++;
        jsbp->blendJointStates();
    }

    // we're done now so there are no more active blenders for this frame
    mActiveBlenders.clear();
}

//-----------------------------------------------------------------------------
// blendAndCache()
//-----------------------------------------------------------------------------
void ReferenceBlender::blendAndCache(bool reset_cached_joints)
{
    for (blender_list_t::iterator iter = mActiveBlenders.begin();
         iter != mActiveBlenders.end(); ++iter)
    {
        LLJointStateBlender* jsbp = *iter;
        if (reset_cached_joints)
        {
            jsbp->resetCachedJoint();
        }
        jsbp->blendJointStates(false);
    }
}

//-----------------------------------------------------------------------------
// interpolate()
//-----------------------------------------------------------------------------
void ReferenceBlender::interpolate(F32 u)
{
    for (blender_list_t::iterator iter = mActiveBlenders.begin();
         iter != mActiveBlenders.end(); ++iter)
    {
        LLJointStateBlender* jsbp = *iter;
        jsbp->interpolate(u);
    }
}

//-----------------------------------------------------------------------------
// clearBlenders()
//-----------------------------------------------------------------------------
void ReferenceBlender::clearBlenders()
{
    for (blender_list_t::iterator iter = mActiveBlenders.begin();
         iter != mActiveBlenders.end(); ++iter)
    {
        LLJointStateBlender* jsbp = *iter;
        jsbp->clear();
    }

    mActiveBlenders.clear();
}
}
#endif
