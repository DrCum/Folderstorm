// Actual cache/motion ownership checks. No viewer or worker dispatch is started.
#include "linden_common.h"
#include "llkeyframemotion.h"
#include "lldatapacker.h"
#include "keyframe_test_character.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <thread>
#include <type_traits>
#include <utility>

namespace AssetOwnershipTest
{
unsigned checks = 0;
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
    ++checks;
}
using Character = FSKeyframeTests::Character;
template<class A,class B> bool sameOwner(const std::shared_ptr<A>& a,const std::shared_ptr<B>& b)
{
    return a.get() == b.get() && !a.owner_before(b) && !b.owner_before(a);
}
class Motion final : public LLKeyframeMotion
{
public:
    using Asset = JointMotionList;
    using Joint = JointMotion;
    using Position = PositionKey;
    using Owner = std::shared_ptr<Asset>;
    Motion(Character& character, const LLUUID& id) : LLKeyframeMotion(id) { setCharacter(&character); }
    void bind(Owner data) { adoptJointMotionList(std::move(data)); }
    auto lease() const { return getKeyframeSamplingLease(); }
    void borrow(Asset* data) { mJointMotionList = data; }
    std::size_t constraintCount() const { return mConstraints.size(); }
    void addConstraint(Asset& data)
    {
        auto* shared = new JointConstraintSharedData;
        data.mConstraints.push_back(shared);
        mConstraints.push_back(new JointConstraint(shared));
    }
};
static_assert(std::is_same_v<decltype(std::declval<const Motion::Asset&>().getJointMotion(0)), const Motion::Joint*>);
struct TrackedAsset final : Motion::Asset
{
    explicit TrackedAsset(std::atomic<unsigned>& counter) : destroyed(counter) {}
    ~TrackedAsset() { if (beforeDestroy) beforeDestroy(); ++destroyed; }
    std::atomic<unsigned>& destroyed;
    std::function<void()> beforeDestroy;
};
void evictionAndReplacement(Character& character)
{
    const LLUUID id("10000000-0000-0000-0000-000000000001");
    std::atomic<unsigned> destroyed{0};
    auto first = std::make_shared<TrackedAsset>(destroyed);
    first->mDuration = 3.f;
    std::weak_ptr<Motion::Asset> old = first;
    LLKeyframeDataCache::addKeyframeData(id,first);
    Motion motion(character,id);
    motion.bind(LLKeyframeDataCache::getKeyframeData(id));
    auto held = motion.lease();
    require(sameOwner(held,first), "Lease is not the canonical cached asset owner");
    auto second = std::make_shared<TrackedAsset>(destroyed);
    second->mDuration = 7.f;
    LLKeyframeDataCache::addKeyframeData(id,second);
    require(LLKeyframeDataCache::getKeyframeData(id) == second, "Cache replacement did not publish the new owner");
    require(held->mDuration == 3.f && held.get() != second.get(), "Old/new asset identities were conflated");
    first.reset();
    motion.bind(LLKeyframeDataCache::getKeyframeData(id));
    require(destroyed == 0 && !old.expired(), "Replacement invalidated an outstanding old lease");
    held.reset();
    require(destroyed == 1 && old.expired(), "Old asset was not released exactly once");
    std::weak_ptr<Motion::Asset> current = second;
    second.reset();
    LLKeyframeDataCache::removeKeyframeData(id);
    require(!LLKeyframeDataCache::getKeyframeData(id), "Evicted asset is still in the cache");
    require(destroyed == 1 && !current.expired() && motion.lease(), "Eviction invalidated the live motion owner");
    LLKeyframeDataCache::removeKeyframeData(id); // Missing-entry removal stays harmless.
    motion.bind(nullptr);
    require(destroyed == 2 && current.expired(), "Final motion owner failed to release its asset");
}
void constraintsAndBorrowedViews(Character& character)
{
    const LLUUID id("10000000-0000-0000-0000-000000000002");
    std::atomic<unsigned> destroyed{0};
    Motion motion(character,id);
    auto old = std::make_shared<TrackedAsset>(destroyed);
    old->beforeDestroy = [&] {require(motion.constraintCount() == 0,"Old shared data died before obsolete constraints");};
    motion.bind(old);
    motion.addConstraint(*old);
    motion.bind(old);
    require(motion.constraintCount() == 1,"Adopting the same asset discarded its constraints");
    old.reset();
    auto next = std::make_shared<TrackedAsset>(destroyed);
    motion.bind(next);
    require(destroyed == 1 && motion.constraintCount() == 0,"Replacement retained obsolete constraints");
    Motion::Asset embedded;
    motion.borrow(&embedded);
    require(!motion.lease(),"Mismatched raw view obtained an owned sampling lease");
    motion.bind(nullptr);
    next.reset();
    require(destroyed == 2,"Mismatched view leaked the canonical owner");
    {
        Motion borrowed(character,id);
        borrowed.borrow(&embedded);
        require(!borrowed.lease(),"Embedded dummy data was admitted to owned sampling");
    }
    require(embedded.getNumJointMotions() == 0,"Borrowed data was deleted or modified by motion destruction");
}
void cacheHitAndClear(Character& character)
{
    const LLUUID id("10000000-0000-0000-0000-000000000003");
    std::atomic<unsigned> destroyed{0};
    auto data = std::make_shared<TrackedAsset>(destroyed);
    data->mDuration = 1.f;
    data->mEmoteName = "inspection-only";
    LLKeyframeDataCache::addKeyframeData(id,data);
    require(LLKeyframeDataCache::hasEmbeddedEmote(id),"Owned-cache emote inspection failed");
    auto motion = std::make_unique<Motion>(character,id);
    require(motion->onInitialize(&character) == LLMotion::STATUS_SUCCESS,"Actual cache-hit initialization failed");
    require(sameOwner(motion->lease(),data),"Cache-hit motion did not attach the canonical owner");
    data.reset();
    LLKeyframeDataCache::clear();
    require(!LLKeyframeDataCache::getKeyframeData(id) && !LLKeyframeDataCache::hasEmbeddedEmote(id),"Cache clear left a published entry");
    require(destroyed == 0 && motion->onUpdate(0.f,nullptr),"Cache clear invalidated active motion evaluation");
    motion.reset();
    require(destroyed == 1,"Cleared cached asset survived its last motion owner");
}
void backgroundLease(Character& character)
{
    const LLUUID id("10000000-0000-0000-0000-000000000004");
    std::atomic<unsigned> destroyed{0};
    auto data = std::make_shared<TrackedAsset>(destroyed);
    auto* joint = new Motion::Joint;
    Motion::Position key(0.f,LLVector3(1,2,3));
    joint->mPositionCurve.mKeys[0.f] = key;
    joint->mPositionCurve.mNumKeys = 1;
    data->mJointMotionArray.push_back(joint);
    Motion motion(character,id);
    motion.bind(data);
    LLKeyframeDataCache::addKeyframeData(id,data);
    auto lease = motion.lease();
    data.reset();
    LLKeyframeDataCache::clear();
    motion.bind(nullptr);
    require(destroyed == 0,"Background lease did not keep key data alive");
    std::thread reader([held = std::move(lease)]() mutable
    {
        const Motion::Joint* jointData = held->getJointMotion(0);
        const auto value = jointData->mPositionCurve.getValue(0.5f,1.f);
        require(value == LLVector3(1,2,3),"Owned background key read changed sample values");
        held.reset(); // Final destruction here must contain only asset-data cleanup.
    });
    reader.join();
    require(destroyed == 1,"Background final owner did not destroy the asset once");
}
struct Packet
{
    std::array<U8,512> bytes{};
    S32 size = 0;
    explicit Packet(bool truncated)
    {
        LLDataPackerBinaryBuffer dp(bytes.data(),static_cast<S32>(bytes.size()));
        bool ok = dp.packU16(KEYFRAME_MOTION_VERSION,"version") && dp.packU16(KEYFRAME_MOTION_SUBVERSION,"sub_version") &&
            dp.packS32(LLJoint::MEDIUM_PRIORITY,"base_priority") && dp.packF32(1.f,"duration") && dp.packString("","emote_name") &&
            dp.packF32(0.f,"loop_in_point") && dp.packF32(1.f,"loop_out_point") && dp.packS32(1,"loop") &&
            dp.packF32(0.f,"ease_in_duration") && dp.packF32(0.f,"ease_out_duration") &&
            dp.packU32(LLHandMotion::HAND_POSE_RELAXED,"hand_pose") && dp.packU32(1,"num_joints") &&
            dp.packString("missing_test_joint","joint_name") && dp.packS32(LLJoint::MEDIUM_PRIORITY,"joint_priority") &&
            dp.packS32(1,"num_rot_keys") && dp.packU16(32768,"time") && dp.packU16(32768,"rot_angle_x") &&
            dp.packU16(32768,"rot_angle_y") && dp.packU16(32768,"rot_angle_z") && dp.packS32(truncated ? 1 : 0,"num_pos_keys");
        if (!truncated) ok = ok && dp.packS32(0,"num_constraints");
        require(ok,"Unable to construct test animation packet");
        size = dp.getCurrentSize();
    }
};
void publication(Character& character)
{
    const LLUUID id("10000000-0000-0000-0000-000000000005");
    Motion motion(character,id);
    Packet partial(true);
    LLDataPackerBinaryBuffer incomplete(partial.bytes.data(),partial.size);
    require(!motion.deserialize(incomplete,id),"Truncated asset unexpectedly loaded");
    require(!motion.lease() && !LLKeyframeDataCache::getKeyframeData(id),"Failed decode published an owned asset");
    Packet complete(false);
    LLDataPackerBinaryBuffer valid(complete.bytes.data(),complete.size);
    require(motion.deserialize(valid,id),"Valid fixture animation did not load");
    auto first = motion.lease();
    require(first && sameOwner(first,LLKeyframeDataCache::getKeyframeData(id)),"Successful publication created independent owners");
    require(first->getNumJointMotions() == 1 && first->getJointMotion(0)->mRotationCurve.mKeys.size() == 1,"Decoded key data changed");
    LLDataPackerBinaryBuffer replacement(complete.bytes.data(),complete.size);
    require(motion.deserialize(replacement,id),"Repeated valid decode failed");
    require(first.get() != motion.lease().get() && first->getJointMotion(0)->mRotationCurve.mKeys.size() == 1,"Replacement invalidated the prior owned data");
    LLKeyframeDataCache::clear();
    require(motion.onUpdate(0.f,nullptr),"Decoded motion did not survive cache clear");
}
int run()
{
    LLKeyframeDataCache::clear();
    Character character;
    evictionAndReplacement(character); constraintsAndBorrowedViews(character);
    cacheHitAndClear(character); backgroundLease(character); publication(character);
    LLKeyframeDataCache::clear();
    std::cout << checks << " actual keyframe asset ownership checks passed\n";
    return 0;
}
}
int main() { return AssetOwnershipTest::run(); }
