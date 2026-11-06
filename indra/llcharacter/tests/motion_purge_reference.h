// Frozen from PR 111, 1ad2444867c70566619913b092b42cde0f2e25ab.
// Only class/cap spelling and the inaccessible logging-only bookmark differ.
// Production retains that final private bookmark unchanged.
void Controller::referencePurge()
{
    if (mLoadedMotions.size() > 32)
    {
        // clean up deprecated motions
        for (motion_set_t::iterator deprecated_motion_it = mDeprecatedMotions.begin();
             deprecated_motion_it != mDeprecatedMotions.end(); )
        {
            motion_set_t::iterator cur_iter = deprecated_motion_it++;
            LLMotion* cur_motionp = *cur_iter;
            if (!isMotionActive(cur_motionp))
            {
                // Motion is deprecated so we know it's not cannonical,
                //  we can safely remove the instance
                removeMotionInstance(cur_motionp); // modifies mDeprecatedMotions
                mDeprecatedMotions.erase(cur_iter);
            }
        }
    }

    std::set<LLUUID> motions_to_kill;
    if (mLoadedMotions.size() > 32)
    {
        // too many motions active this frame, kill all blenders
        mPoseBlender.clearBlenders();
        for (LLMotion* cur_motionp : mLoadedMotions)
        {
            // motion isn't playing, delete it
            if (!isMotionActive(cur_motionp))
            {
                motions_to_kill.insert(cur_motionp->getID());
            }
        }
    }

    // clean up all inactive, loaded motions
    for (const LLUUID& motion_id : motions_to_kill)
    {
        // look up the motion again by ID to get canonical instance
        // and kill it only if that one is inactive
        LLMotion* motionp = findMotion(motion_id);
        if (motionp && !isMotionActive(motionp))
        {
            removeMotion(motion_id);
        }
    }

    U32 loaded_count = static_cast<U32>(mLoadedMotions.size());
    // <FS:Ansariel> Can't do anything about it anyway - stop spamming the log
    //if (loaded_count > (2 * 32) && loaded_count > mLastCountAfterPurge)
    //{
    //  LL_WARNS_ONCE("Animation") << loaded_count << " Loaded Motions. Amount of motions is over limit." << LL_ENDL;
    //}
    // </FS:Ansariel>
    (void)loaded_count;
}
