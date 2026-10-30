#ifndef FS_KEYFRAME_TEST_CHARACTER_H
#define FS_KEYFRAME_TEST_CHARACTER_H

#include "llcharacter.h"
#include "lluuid.h"
#include "v3dmath.h"

namespace FSKeyframeTests
{
// No avatar, renderer, account or network dependencies.
class Character : public LLCharacter
{
public:
    const char* getAnimationPrefix() override { return "test"; }
    LLJoint* getRootJoint() override { return nullptr; }
    LLVector3 getCharacterPosition() override { return LLVector3(); }
    LLQuaternion getCharacterRotation() override { return LLQuaternion(); }
    LLVector3 getCharacterVelocity() override { return LLVector3(); }
    LLVector3 getCharacterAngularVelocity() override { return LLVector3(); }
    void getGround(const LLVector3&, LLVector3& position, LLVector3& normal) override
    { position = LLVector3(); normal = LLVector3(0,0,1); }
    LLJoint* getCharacterJoint(U32) override { return nullptr; }
    F32 getTimeDilation() override { return 1.f; }
    F32 getPixelArea() const override { return 1.f; }
    LLPolyMesh* getHeadMesh() override { return nullptr; }
    LLPolyMesh* getUpperBodyMesh() override { return nullptr; }
    LLVector3d getPosGlobalFromAgent(const LLVector3&) override { return LLVector3d(); }
    LLVector3 getPosAgentFromGlobal(const LLVector3d&) override { return LLVector3(); }
    void addDebugText(const std::string&) override {}
    const LLUUID& getID() const override { return mID; }
private:
    LLUUID mID{"51abb30a-f75f-4bca-9bb0-167ce8c60974"};
};
}

#endif
