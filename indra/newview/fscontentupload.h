/** SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef FS_CONTENT_UPLOAD_H
#define FS_CONTENT_UPLOAD_H
#include <string>
namespace fs_content {
struct NativeSourceOwner
{
    bool missingCore=false, pendingUploads=false, dirty=false, externalWatcher=false;
    bool forcedSave=false, saveDialog=false, preprocessing=false, includeFetch=false;
};
inline bool nativeSourceBusy(const NativeSourceOwner& owner)
{
    return owner.missingCore || owner.pendingUploads || owner.dirty || owner.externalWatcher ||
        owner.forcedSave || owner.saveDialog || owner.preprocessing || owner.includeFetch;
}
inline constexpr int uploadRetryLimit(bool assistantOwned){return assistantOwned?0:2;}
// Shared by the live assistant uploader and portable boundary tests. A stage
// may submit only once, including after a lost result or an authority rebound.
class UploadProgress
{
public:
    template<typename Allowed,typename Submit>
    bool before(const std::string& stage,bool submission,Allowed allowed,Submit submit)
    {
        if(mFinished || mStopped || !allowed()){mStopped=true;return false;}
        if(stage=="commit" && !mSourceSubmitted){mStopped=true;return false;}
        if(submission)
        {
            if((stage=="capability" && mHandshakeSubmitted) ||
               (stage=="source" && (!mHandshakeSubmitted || mSourceSubmitted)) ||
               (stage!="capability" && stage!="source") || !submit())
            {mStopped=true;return false;}
            if(stage=="capability")mHandshakeSubmitted=true;
            else mSourceSubmitted=true;
        }
        return true;
    }
    bool finish(){if(mFinished)return false;mFinished=true;return true;}
    bool finished() const{return mFinished;}
    bool sourceSubmitted() const{return mSourceSubmitted;}
    struct Outcome {std::string state;bool sourceSaved=false,sourceSavedKnown=false,uncertain=true;};
    Outcome outcome(bool completeAsset,bool locallyAccepted) const
    {
        return {completeAsset?(locallyAccepted?"server_confirmed":"server_confirmed_local_conflict"):
            mSourceSubmitted?"submitted_unknown":"cancelled_before_source_submission",
            completeAsset,completeAsset || !mSourceSubmitted,!locallyAccepted || !completeAsset};
    }
private:
    bool mHandshakeSubmitted=false,mSourceSubmitted=false,mFinished=false,mStopped=false;
};
}
#endif
