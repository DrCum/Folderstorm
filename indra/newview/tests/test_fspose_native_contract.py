"""Portable architecture checks for the actual native pose submission paths.

These complement policy/dispatch tests; native scheduler and two-viewer consent
acceptance still require the viewer runtime.
"""
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[3]

def function(path, signature):
    source = (ROOT / "indra/newview" / path).read_text()
    start = source.index(signature)
    start = source.index("{", start)
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

class PoseNativeContract(unittest.TestCase):
    def test_private_motion_never_changes_native_animation_state(self):
        start = function("fsposeranimator.cpp", "LLUUID FSPoserAnimator::startAssistantPose")
        self.assertIn("avatar!=gAgentAvatarp", start)
        self.assertIn("avatar->getID()!=gAgentID", start)
        self.assertIn("avatar->isControlAvatar()", start)
        self.assertIn("configureAssistantRotations", start)
        self.assertNotIn("startDefaultMotions", start)
        self.assertNotIn("stopFidget", start)
        self.assertNotIn("mPosingState", start)
        stop = function("fsposeranimator.cpp", "bool FSPoserAnimator::stopAssistantPose")
        self.assertIn("found->second!=id", stop)
        self.assertNotIn("stopPosingAvatar", stop)
        self.assertNotIn("stopAll", stop)
    def test_actual_timed_write_rechecks_authority_and_only_rotates(self):
        update = function("fsposingmotion.cpp", "bool FSPosingMotion::onUpdate")
        branch = update[update.index("if(mAssistantRotationOnly)"):update.index("currentRotation = joint->getRotation();")]
        self.assertLess(branch.index("!assistantAuthorityValid()"), branch.index("setRotation(rotation)"))
        self.assertNotIn("setPosition", branch)
        self.assertNotIn("setScale", branch)
        self.assertIn("LLJointState::ROT", branch)
        header = (ROOT / "indra/newview/fsposingmotion.h").read_text()
        self.assertIn("mAssistantRotationOnly?0.f:MIN_REQUIRED_PIXEL_AREA_POSING", header)
        self.assertIn("mAssistantRotationOnly?0.f:0.5f", header)
    def test_deactivation_does_not_revert_manual_or_nonrotation_state(self):
        deactivate = function("fsposingmotion.cpp", "void FSPosingMotion::onDeactivate")
        own = deactivate[:deactivate.index("revertJointsAndCollisionVolumes();")]
        self.assertIn("mAssistantRotationOnly", own)
        self.assertIn("mAssistantStopped", own)
        self.assertNotIn("setPosition", own)
        self.assertNotIn("setScale", own)
        self.assertIn("return;", own)
    def test_owned_cleanup_restores_blender_seed_without_overwriting_others(self):
        release = function("fsposingmotion.cpp", "void FSPosingMotion::releaseAssistantJoint")
        self.assertLess(release.index("owned->second.restore(current)"), release.index("native->setRotation("))
        self.assertLess(release.index("native->setRotation("), release.index("setJointState(native,0)"))
        self.assertIn("mAssistantRestorationComplete=false", release)
        removed = function("fsposingmotion.cpp", "bool FSPosingMotion::setAssistantRotations")
        self.assertIn("found==rotations.end()){releaseAssistantJoint(joint)", removed)
        self.assertIn("releaseAssistantRotations()", function("fsposingmotion.cpp", "void FSPosingMotion::onDeactivate"))
    def test_idle_tick_and_native_takeover_prevent_masked_motion_resume(self):
        source = (ROOT / "indra/newview/fsposelistener.cpp").read_text()
        self.assertIn("gIdleCallbacks.addFunction([](void*){fs_pose_tick();})", source)
        tick = function("fsposeranimator.cpp", "void FSPoserAnimator::tickAssistantPoses")
        self.assertIn("!motion->assistantAuthorityValid()", tick)
        self.assertIn("stopAssistantPose", tick)
        takeover = function("fsposeranimator.cpp", "bool FSPoserAnimator::tryPosingAvatar")
        self.assertLess(takeover.index("invalidateAssistantPose(avatar)"), takeover.index("findOrCreatePosingMotion(avatar)"))
        update = function("fsposingmotion.cpp", "bool FSPosingMotion::onUpdate")
        self.assertNotIn("return false", update)
        self.assertIn("avatar->LLCharacter::stopMotion(id,true)", function("fsposeranimator.cpp", "bool FSPoserAnimator::stopAssistantPose"))
        authority = function("fsposingmotion.cpp", "bool FSPosingMotion::assistantAuthorityValid")
        self.assertIn("mAssistantRevoked=true", authority)
    def test_file_load_uses_bounded_parser_and_confined_store(self):
        source = (ROOT / "indra/newview/fsposelistener.cpp").read_text()
        self.assertNotIn("loadPoseFromXml", source)
        self.assertIn("parser->parse(input,pose,presetLimit,8)", source)
        self.assertIn("if(!presetXML(bytes))", source)
        self.assertLess(source.index("if(!presetXML(bytes))"), source.index("parser->parse(input,pose,presetLimit,8)"))
        self.assertIn("store.read(name,bytes,error)", source)
        self.assertIn("store.writeNew(name,bytes,error)", source)
        store = (ROOT / "indra/newview/fsposefile.h").read_text()
        self.assertIn("O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC", store)
        self.assertIn("O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC", store)
        self.assertIn("FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS", store)
        self.assertIn("FILE_READ_ATTRIBUTES,FILE_SHARE_READ", store)
        self.assertIn("GENERIC_WRITE,0,nullptr,CREATE_NEW", store)
        self.assertNotIn("LLFile::fopen", source)
        self.assertNotIn("llifstream", source)
        self.assertIn('"expected_preset_revision"].asString()!="absent"', source)
        self.assertIn('"pose_save"', source)
        sync = function("fsposelistener.cpp", "void syncSession")
        self.assertNotIn("stopAssistantPose(", sync)

if __name__ == "__main__": unittest.main()
