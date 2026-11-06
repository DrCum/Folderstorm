/**
 * @file lljoint_matrix_cache_test.cpp
 * @brief Verify the aligned joint cache against the legacy matrix getter.
 */

#include "linden_common.h"
#include "lljoint.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>

namespace
{
unsigned comparisons = 0;

void equivalent(LLJoint& joint, bool aligned_first)
{
    LLMatrix4 legacy;
    LLMatrix4a cached;
    if (aligned_first)
    {
        cached = joint.getWorldMatrix4a();
        legacy = joint.getWorldMatrix();
    }
    else
    {
        legacy = joint.getWorldMatrix();
        cached = joint.getWorldMatrix4a();
    }
    LLMatrix4a converted;
    converted.loadu(legacy);
    if (std::memcmp(converted.getF32ptr(), cached.getF32ptr(), sizeof(F32) * 16) != 0)
    {
        std::cerr << "Joint matrix cache mismatch at comparison " << comparisons << '\n';
        std::exit(1);
    }
    ++comparisons;
}

float sample(std::mt19937& random)
{
    return static_cast<float>(static_cast<int>(random() % 2049) - 1024) / 128.f;
}

void change(LLJoint& joint, std::mt19937& random)
{
    joint.setPosition(LLVector3(sample(random), sample(random), sample(random)));
    joint.setRotation(LLQuaternion(sample(random), LLVector3::x_axis) *
                      LLQuaternion(sample(random), LLVector3::y_axis));
    // Include reflections and zero scale without invoking world-space inversion.
    joint.setScale(LLVector3(sample(random), sample(random), sample(random)));
}
}

int main()
{
    LLJoint root, other_root, parent, child;
    equivalent(root, true);
    equivalent(child, false);
    root.addChild(&parent);
    parent.addChild(&child);
    equivalent(child, true);

    root.setPosition(LLVector3(3.f, -5.f, 7.f));
    root.setScale(LLVector3(2.f, 3.f, -4.f));
    child.setPosition(LLVector3(1.f, 2.f, 3.f));
    child.getWorldPosition();
    child.getWorldRotation();
    equivalent(child, true);
    equivalent(child, false);

    std::mt19937 random(0x46534d43);
    for (unsigned iteration = 0; iteration < 2000; ++iteration)
    {
        change(root, random);
        change(parent, random);
        change(child, random);
        if (iteration % 3 == 0)
        {
            child.getWorldPosition();
            child.getWorldRotation();
        }
        if (iteration % 5 == 0) root.updateWorldMatrixChildren();
        equivalent(child, iteration % 2 == 0);
        equivalent(parent, iteration % 2 != 0);
        equivalent(root, iteration % 2 == 0);
        equivalent(child, iteration % 2 != 0); // Clean-cache access.
    }

    // World-space setters invert parent transforms, so use nonsingular scales.
    root.setScale(LLVector3(1.f, 1.f, 1.f));
    parent.setScale(LLVector3(1.f, 1.f, 1.f));
    child.setWorldPosition(LLVector3(1.f, 4.f, -2.f));
    equivalent(child, true);
    child.setWorldRotation(LLQuaternion(0.75f, LLVector3::z_axis));
    equivalent(child, false);

    // Sitting connects the root transform to an external drawable transform.
    LLXformMatrix seat;
    seat.setPosition(LLVector3(9.f, -3.f, 2.f));
    seat.updateMatrix();
    root.getXform()->setParent(&seat);
    root.touch();
    equivalent(child, true);
    seat.setPosition(LLVector3(-4.f, 8.f, 6.f));
    seat.updateMatrix();
    root.touch();
    equivalent(child, false);
    root.getXform()->setParent(nullptr);
    root.getXform()->update();
    root.touch();
    equivalent(child, true);

    other_root.setPosition(LLVector3(-8.f, 6.f, 4.f));
    other_root.addChild(&child);
    equivalent(child, true);
    other_root.removeChild(&child);
    equivalent(child, false);
    root.removeAllChildren();
    equivalent(parent, true);
    std::cout << comparisons << " actual LLJoint matrix-cache comparisons passed\n";
}
