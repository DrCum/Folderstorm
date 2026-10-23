"""Generate a native regression harness using the actual avatar bounds block."""
from pathlib import Path
import argparse
import re

ROOT = Path(__file__).resolve().parents[1]

LEGACY = r"""
    if (box_detail >= 3)
    {
        updateRiggingInfo();
        for (S32 joint_num = 0; joint_num < LL_CHARACTER_MAX_ANIMATED_JOINTS; ++joint_num)
        {
            LLJoint* joint = getJoint(joint_num);
            LLJointRiggingInfo* rig_info = nullptr;
            if (joint_num < mJointRiggingInfoTab.size())
                rig_info = &mJointRiggingInfoTab[joint_num];
            if (joint && rig_info && rig_info->isRiggedTo())
            {
                auto* attachment = dynamic_cast<LLViewerJointAttachment*>(joint);
                if (attachment && attachment->getIsHUDAttachment()) continue;
                LLMatrix4a mat;
                LLVector4a extents[2];
                mat.loadu(joint->getWorldMatrix());
                matMulBoundBox(mat, rig_info->getRiggedExtents(), extents);
                update_min_max(newMin, newMax, extents[0]);
                update_min_max(newMin, newMax, extents[1]);
            }
        }
    }
"""

HARNESS = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <vector>
using S32 = int;
constexpr S32 LL_CHARACTER_MAX_ANIMATED_JOINTS = @JOINT_CAP@;
struct LLVector4a { std::array<float, 3> v{}; };
using Matrix = std::array<float, 16>;
struct LLMatrix4a { Matrix m{}; void loadu(const Matrix& value) { m = value; } };
void update_min_max(LLVector4a& lo, LLVector4a& hi, const LLVector4a& point)
{
    for (std::size_t i = 0; i < 3; ++i)
    {
        lo.v[i] = std::min(lo.v[i], point.v[i]);
        hi.v[i] = std::max(hi.v[i], point.v[i]);
    }
}
// The transform is a typed boundary stub. Both traversals must invoke it with
// the same inputs and order; the unchanged native SIMD transform is not replaced.
void matMulBoundBox(const LLMatrix4a& mat, const LLVector4a* box, LLVector4a* result)
{
    result[0].v.fill(1.e20f); result[1].v.fill(-1.e20f);
    for (unsigned corner = 0; corner < 8; ++corner)
    {
        LLVector4a point;
        for (unsigned row = 0; row < 3; ++row)
        {
            point.v[row] = mat.m[12 + row];
            for (unsigned column = 0; column < 3; ++column)
                point.v[row] += mat.m[column * 4 + row] * box[(corner >> column) & 1].v[column];
        }
        update_min_max(result[0], result[1], point);
    }
}
struct LLJoint
{
    virtual ~LLJoint() = default;
    Matrix matrix{};
    int id = 0;
    std::vector<int>* transformed = nullptr;
    const Matrix& getWorldMatrix() { transformed->push_back(id); return matrix; }
};
struct LLViewerJointAttachment : LLJoint
{
    bool hud = false;
    bool getIsHUDAttachment() const { return hud; }
};
struct LLJointRiggingInfo
{
    bool used = false;
    LLVector4a extents[2];
    bool isRiggedTo() const { return used; }
    const LLVector4a* getRiggedExtents() const { return extents; }
};
struct RiggingTable : std::vector<LLJointRiggingInfo>
{
    // The production table also returns S32, rather than vector::size_type.
    S32 size() const { return static_cast<S32>(std::vector<LLJointRiggingInfo>::size()); }
};
struct Avatar
{
    RiggingTable mJointRiggingInfoTab;
    std::vector<std::unique_ptr<LLJoint>> joints;
    std::vector<int> lookedUp, transformed;
    unsigned refreshes = 0;
    std::function<void()> refresh;
    void updateRiggingInfo() { ++refreshes; if (refresh) refresh(); }
    LLJoint* getJoint(S32 index)
    {
        lookedUp.push_back(index);
        return index < static_cast<S32>(joints.size()) ? joints[index].get() : nullptr;
    }
    void legacy(S32 box_detail, LLVector4a& newMin, LLVector4a& newMax)
    {
@LEGACY@
    }
    void production(S32 box_detail, LLVector4a& newMin, LLVector4a& newMax)
    {
@PRODUCTION@
    }
};
unsigned cases = 0;
void compare(Avatar& avatar, int detail)
{
    LLVector4a oldMin{{-1.f, -2.f, -3.f}}, oldMax{{1.f, 2.f, 3.f}};
    auto newMin = oldMin, newMax = oldMax;
    avatar.lookedUp.clear(); avatar.transformed.clear(); avatar.refreshes = 0;
    avatar.legacy(detail, oldMin, oldMax);
    const auto oldOrder = avatar.transformed;
    const auto oldRefreshes = avatar.refreshes;
    const auto oldLookups = avatar.lookedUp.size();
    avatar.lookedUp.clear(); avatar.transformed.clear(); avatar.refreshes = 0;
    avatar.production(detail, newMin, newMax);
    assert(oldMin.v == newMin.v && oldMax.v == newMax.v);
    assert(avatar.transformed == oldOrder && avatar.refreshes == oldRefreshes);
    std::vector<int> expectedLookups;
    if (detail >= 3)
    {
        for (int i = 0; i < std::min(LL_CHARACTER_MAX_ANIMATED_JOINTS, avatar.mJointRiggingInfoTab.size()); ++i)
            if (avatar.mJointRiggingInfoTab[i].used) expectedLookups.push_back(i);
        assert(oldLookups == static_cast<std::size_t>(LL_CHARACTER_MAX_ANIMATED_JOINTS));
        assert(avatar.refreshes == 1);
    }
    else assert(oldLookups == 0 && avatar.refreshes == 0);
    assert(avatar.lookedUp == expectedLookups);
    ++cases;
}
Avatar fixture(int size)
{
    Avatar result;
    result.mJointRiggingInfoTab.resize(size);
    result.joints.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS + 8);
    for (int i = 0; i < static_cast<int>(result.joints.size()); ++i)
    {
        std::unique_ptr<LLJoint> joint;
        if (i % 3 == 0) joint = std::make_unique<LLJoint>();
        else joint = std::make_unique<LLViewerJointAttachment>();
        joint->id = i; joint->matrix = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
        result.joints[i] = std::move(joint);
    }
    for (auto& info : result.mJointRiggingInfoTab)
    {
        info.extents[0].v = {-1.f, -2.f, -3.f};
        info.extents[1].v = {1.f, 2.f, 3.f};
    }
    return result;
}
void bind(Avatar& avatar)
{
    for (auto& joint : avatar.joints) if (joint) joint->transformed = &avatar.transformed;
}
int main()
{
    auto avatar = fixture(0); bind(avatar); compare(avatar, 3);
    assert(avatar.lookedUp.empty());
    avatar.mJointRiggingInfoTab.resize(LL_CHARACTER_MAX_ANIMATED_JOINTS);
    compare(avatar, 3); // A populated table with no rigged joints.
    for (auto& info : avatar.mJointRiggingInfoTab) info.used = true;
    compare(avatar, 3); // All joints contribute, including ordinary bone joints.
    for (auto& info : avatar.mJointRiggingInfoTab) info.used = false;
    for (int index : {0, 14, LL_CHARACTER_MAX_ANIMATED_JOINTS - 1})
        avatar.mJointRiggingInfoTab[index].used = true;
    compare(avatar, 3);
    assert(avatar.lookedUp.size() == 3);
    avatar.joints[14].reset(); compare(avatar, 3); // Missing rigged joint.
    static_cast<LLViewerJointAttachment*>(avatar.joints[LL_CHARACTER_MAX_ANIMATED_JOINTS - 1].get())->hud = true;
    compare(avatar, 3);
    for (int detail : {-1, 0, 1, 2, 4}) compare(avatar, detail);
    avatar.refresh = [&avatar] { avatar.mJointRiggingInfoTab.resize(1); };
    compare(avatar, 3); // Refresh can resize the table before traversal.
    avatar.refresh = {};
    avatar = fixture(LL_CHARACTER_MAX_ANIMATED_JOINTS + 32); bind(avatar);
    for (auto& info : avatar.mJointRiggingInfoTab) info.used = true;
    compare(avatar, 3); // Preserve the original cap for an oversized table.

    std::mt19937 random(0x46535045);
    for (int iteration = 0; iteration < 2000; ++iteration)
    {
        avatar = fixture(static_cast<int>(random() % (LL_CHARACTER_MAX_ANIMATED_JOINTS + 32)));
        bind(avatar);
        for (auto& info : avatar.mJointRiggingInfoTab)
        {
            info.used = (random() % 4) != 0;
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                info.extents[0].v[axis] = -static_cast<float>(random() % 100) / 8.f;
                info.extents[1].v[axis] = static_cast<float>(random() % 100) / 8.f;
            }
        }
        for (auto& joint : avatar.joints)
        {
            if (random() % 9 == 0) { joint.reset(); continue; }
            if (auto* attachment = dynamic_cast<LLViewerJointAttachment*>(joint.get()))
                attachment->hud = random() % 7 == 0;
            // Signed scale, shear, translation and non-uniform boxes.
            for (auto& value : joint->matrix) value = static_cast<float>(static_cast<int>(random() % 65) - 32) / 8.f;
        }
        compare(avatar, iteration % 6);
        // Changes between frames must not use a stale mask or lookup cache.
        for (auto& info : avatar.mJointRiggingInfoTab) info.used = !info.used;
        compare(avatar, 3);
    }
    std::cout << cases << " production bounds comparisons passed; empty/sparse lookups 0/3 instead of "
              << LL_CHARACTER_MAX_ANIMATED_JOINTS << '\n';
}
"""


def generate(source: Path) -> str:
    text = source.read_text(encoding="utf-8")
    start = text.index("// Stretch bounding box by rigged mesh joint boxes")
    end = text.index("// Update pixel area", start)
    block = text[start:end]
    cap = re.search(r"LL_CHARACTER_MAX_ANIMATED_JOINTS\s*=\s*(\d+)",
                    (ROOT.parent / "llcharacter/lljoint.h").read_text(encoding="utf-8"))
    if cap is None:
        raise ValueError("Cannot locate the production animated joint limit")
    return (HARNESS.replace("@JOINT_CAP@", cap.group(1))
            .replace("@LEGACY@", LEGACY).replace("@PRODUCTION@", block))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "llvoavatar.cpp")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    arguments.output.write_text(generate(arguments.source), encoding="utf-8")
