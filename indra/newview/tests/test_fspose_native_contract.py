"""Portable architecture checks for the actual native pose submission paths.

These complement policy/dispatch tests; native scheduler and two-viewer consent
acceptance still require the viewer runtime.
"""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET
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
    def test_shared_typed_service_preserves_native_consent_and_channels(self):
        prepare = function("fssharedposemanager.cpp", "bool FSSharedPoseManager::prepareAssistantPose")
        execute = function("fssharedposemanager.cpp", "LLSD FSSharedPoseManager::executeAssistantPose")
        self.assertIn('source["can_edit"].asBoolean()', prepare)
        self.assertIn('request["session_generation"]', prepare)
        self.assertIn('source["joints"][joint.first]["revision"]', prepare)
        for forbidden in ("requestPermission(", "accept(", "LLNotificationsUtil", "setBOOL(", "edit(", "stopAll(", "history("):
            self.assertNotIn(forbidden, execute)
        rotation = function("fsposeranimator.cpp", "bool FSPoserAnimator::applyAssistantSharedRotations")
        self.assertIn("sameSharedJoint", rotation)
        self.assertIn("if(!allowed())return false", rotation)
        self.assertIn("setAssistantTargetRotation", rotation)
        for forbidden in ("applySharedTransform", "setPosition", "setScale", "setPriority", "addJointToState", "tryPosingAvatar", "addStateToUndo"):
            self.assertNotIn(forbidden, rotation)
    def test_publication_keeps_original_authority_across_native_edits(self):
        native = function("fssharedposemanager.cpp", "bool FSSharedPoseManager::assistantNativeValid")
        self.assertIn("if(work->publishing)return mAssistantPublishing==work", native)
        self.assertNotIn("mOwner.pose()!=work->before", native)
        execute = function("fssharedposemanager.cpp", "LLSD FSSharedPoseManager::executeAssistantPose")
        self.assertNotIn("mAssistantPublishing->task=task", execute)
        self.assertNotIn("mAssistantPublishing->before=", execute)
        validate = function("fssharedposemanager.cpp", "bool FSSharedPoseManager::validateAssistantPose")
        self.assertIn('operation.validation["native_pose"]', validate)
        takeover = function("fssharedposemanager.cpp", "void FSSharedPoseManager::ensureSharedSelf")
        branch = takeover[takeover.index("if (mOwner.active())"):takeover.index("mAnimator.tryPosingAvatar")]
        self.assertIn("nativeTakeover && mAssistantPublishing", branch)
        self.assertIn("mAssistantPublishing.reset()", branch)
        self.assertNotIn("mOwner.clear()", branch)
        self.assertNotIn("stopPosingAvatar", branch)
        accept = function("fssharedposemanager.cpp", "void FSSharedPoseManager::accept")
        self.assertIn("ensureSharedSelf(false)", accept)
    def test_shared_queue_rechecks_at_actual_post_without_retry_or_tls_suspend(self):
        transport = function("fslslbridge.cpp", "bool FSLSLBridge::sharedPoseToLSLGuarded")
        post = transport.index("postAndSuspend")
        for required in ('transport->claim("post")', "!bound()", "!nativeAuthority(false)", "!nativeAuthority(true)", "asyncTaskSubmit(transport)", "options->setRetries(0)", "options->setFollowRedirects(false)"):
            self.assertLess(transport.index(required), post)
        self.assertNotIn("AsyncScope ", transport)
        self.assertNotIn('enqueueCoprocedure("General"', transport)
        self.assertIn("pool.enqueueCoprocedure(ASSISTANT_POSE_HTTP_POOL", transport)
        self.assertLess(transport.index("pool.count(ASSISTANT_POSE_HTTP_POOL)>=ASSISTANT_POSE_HTTP_LIMIT"), transport.index("enqueueCoprocedure"))
        self.assertIn("mAssistantPoseLifetime==lifetime", transport)
        constructor = function("fslslbridge.cpp", "FSLSLBridge::FSLSLBridge()")
        self.assertIn("initializePool(ASSISTANT_POSE_HTTP_POOL,ASSISTANT_POSE_HTTP_QUEUE)", constructor)
        destructor = function("fslslbridge.cpp", "FSLSLBridge::~FSLSLBridge()")
        self.assertIn("mAssistantPoseLifetime.setNull()", destructor)
        for binding in ("gAgent.getSessionID()==login", "getGrid()==grid", "assistantIdentity()==worker", "getRegionID()==region", "getAttachedID()==bridge", "mCurrentURL==url"):
            self.assertIn(binding, transport)
        queue = function("fssharedposemanager.cpp", "void FSSharedPoseManager::tick()")
        self.assertIn("sharedPoseToLSLGuarded", queue)
        self.assertIn("submit && edit", queue)
        self.assertIn("entry.packets.pop_front()", queue)
    def test_shared_queue_coalesces_joint_revisions_with_pose_entries(self):
        send = function("fssharedposemanager.cpp", "void FSSharedPoseManager::send(")
        self.assertIn("fs_pose::coalescePoseUpdate", send)
        self.assertIn('pending->data["jr"][joint.first]', send)
        self.assertIn('data["jr"][joint.first]', send)
        self.assertIn('outgoing["jr"][joint.first]', send)
        self.assertIn("poseData(merged.pose)", send)
    def test_shared_duplicate_reservation_and_private_payload_gates(self):
        callback = function("fssharedposemanager.cpp", "bool FSSharedPoseManager::handleAssistant")
        self.assertLess(callback.index("if(work->granted)return true"), callback.index('message("edit"'))
        self.assertIn("work->binding!=assistantBinding()", callback)
        self.assertIn("work->sourceSubmitted", callback)
        self.assertIn("fs_pose::rotationResult", callback)
        self.assertIn("fs_pose::updateAcknowledgedCache", callback)
        self.assertIn("remote->second.session.asString()", callback)
        self.assertIn("remote->second.generation.asString()", callback)
        self.assertIn("approval->second.session.asString()", callback)
        self.assertIn("approval->second.generation.asString()", callback)
        self.assertIn("approval->second.token.asString()", callback)
        self.assertNotIn("mRemotes[sender].pose", callback)
        read = function("fssharedposemanager.cpp", "LLSD FSSharedPoseManager::executeAssistantPose")
        self.assertLess(read.index("!assistantReadable()"), read.index("return found->second->result"))
        self.assertIn("found->second->epoch!=epoch", read)
        self.assertIn("found->second->binding!=assistantBinding()", read)
        native = function("fssharedposemanager.cpp", "bool FSSharedPoseManager::assistantNativeValid")
        self.assertIn("approval->second.generation!=work->grantGeneration", native)
        self.assertIn("remote->second.generation!=work->generation", native)
        self.assertIn("nativePoseGeneration(avatarFor(gAgentID))!=work->nativeMotionGeneration", native)
        self.assertIn("mActivationGeneration=LLUUID::generateNewID()", function("fsposingmotion.cpp", "bool FSPosingMotion::onActivate"))
        snapshot = function("fssharedposemanager.cpp", "LLSD FSSharedPoseManager::assistantSnapshot")
        for forbidden in ('["token"]', '["login"]', '["simulator_session"]'):
            self.assertNotIn(forbidden, snapshot)
        ctor = function("fssharedposemanager.cpp", "FSSharedPoseManager::FSSharedPoseManager()")
        self.assertIn('getControl("FSSharedPosingEnabled")', ctor)
        self.assertIn('getControl("UseLSLBridge")', ctor)
        self.assertIn('if(!value.asBoolean())invalidateNativeSharing("native_sharing_disabled")', ctor)
        invalidation = function("fssharedposemanager.cpp", "void FSSharedPoseManager::invalidateNativeSharing")
        self.assertIn("mOwner.clear()", invalidation)
        self.assertIn("mApprovals.clear()", invalidation)
        self.assertIn("mQueue.clear()", invalidation)
        self.assertIn("mReady=false", invalidation)
        for forbidden in ("stopAll(", "stopRemote(", "stopPosingAvatar(", "applySharedPose("):
            self.assertNotIn(forbidden, invalidation)
        self.assertIn("gAgent.addRegionChangedCallback", ctor)
        self.assertIn("mAssistantRegionGeneration.advance()", ctor)
        self.assertIn('cancel("native_region_changed")', ctor)
        dtor = function("fssharedposemanager.cpp", "FSSharedPoseManager::~FSSharedPoseManager()")
        self.assertIn("mAssistantRegionConnection.disconnect()", dtor)
        self.assertIn("mSharingConnection.disconnect()", dtor)
        self.assertIn("mBridgeUseConnection.disconnect()", dtor)
        binding = function("fssharedposemanager.cpp", "LLSD assistantBinding()")
        self.assertIn('"region_generation"', binding)
        transport = function("fslslbridge.cpp", "bool FSLSLBridge::sharedPoseToLSLGuarded")
        self.assertIn("assistantRegionGeneration()==regionGeneration", transport)
        self.assertLess(transport.index("assistantRegionGeneration()==regionGeneration"), transport.index("postAndSuspend"))
    def test_shared_approval_consequences_have_native_strings(self):
        prepare = function("fssharedposemanager.cpp", "bool FSSharedPoseManager::prepareAssistantPose")
        ui = ET.parse(ROOT / "indra/newview/skins/default/xui/en/floater_local_assistant_approval.xml")
        available = {entry.attrib["name"] for entry in ui.getroot().findall("string")}
        warnings = re.findall(r'\["consequences"\]\.append\("([a-zA-Z0-9_]+)"\)', prepare)
        self.assertTrue(warnings)
        for warning in warnings:
            self.assertIn(warning, available, f"Missing native approval string: {warning}")
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
