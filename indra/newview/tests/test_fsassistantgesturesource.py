"""Focused native boundaries; no native viewer build or network is required."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(name):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", (ROOT / name).read_text(), flags=re.S)


class GestureNativeBoundaryTests(unittest.TestCase):
    def test_exact_guarded_surface_and_literal_sender(self):
        text = source("fsgesturelistener.cpp")
        self.assertIn('LLEventAPI("FSGestures"', text)
        self.assertIn('for(const auto* op:{"list","status","play","stop"})', text)
        for forbidden in ("cmd_line_chat(", "playGesture(", "runStep(", "deserialize(", "stripChannelNumber(", "triggerAndReviseString(", "activateGesture(", "setMuted("):
            self.assertNotIn(forbidden, text)
        self.assertIn("FSNearbyChat::sendChatFromViewer(text,text,CHAT_TYPE_NORMAL,false,0)", text)
        self.assertIn('operation.requiredClasses.push_back("chat_send_nearby")', text)
        self.assertIn('operation.requiredClasses.push_back("gesture_read")', text)
        self.assertIn('operation.requiredClasses.push_back("gesture_play")', text)
        self.assertIn('operation.summary["effects"]=effects(frozen.plan)', text)

    def test_item_bound_load_freezes_bounded_asset_before_approval(self):
        text = source("fsgesturelistener.cpp")
        self.assertIn("getInvItemAsset(", text)
        self.assertNotIn("getAssetData(", text)
        self.assertLess(text.index("size>static_cast<S32>(maxAssetBytes)"), text.index("std::string bytes(size"))
        self.assertIn("entry.hash=fs_content::hash(bytes)", text)
        self.assertIn('request["asset_hash"].asString()!=frozen.hash', text)
        self.assertIn('operation.validation["effects"]=effects(frozen.plan)', text)
        self.assertIn("evidence(item)!=load->itemEvidence", text)
        self.assertIn("load->identity!=source()", text)
        self.assertIn("fs_assistant::asyncTaskAllowed(load->task)", text)
        self.assertIn("loads().erase(found)", text)  # opaque never-reused token makes duplicate callback inert
        model = source("fsassistantgesturemodel.h")
        self.assertIn("maxSteps=64, maxAssetBytes=16384", model)
        self.assertIn("integer(count,maxSteps)", model)
        self.assertNotIn("LLMultiGesture", model)

    def test_every_effect_retains_task_and_native_restrictions(self):
        text = source("fsgesturelistener.cpp")
        self.assertIn("task(fs_assistant::asyncTask())", text)
        self.assertIn("fs_assistant::AsyncScope scope(play.task->guard())", text)
        apply = text[text.index("bool apply(Playback& play"):text.index("    void tick()")]
        self.assertEqual(apply.count("asyncTaskSubmit(play.task)"), 3)
        for check in ("FSGesturesEnabled", "RlvActions::canPlayGestures()", "assistantActiveAllowed()", "backgroundAudioMuted()", "MuteSounds", "RLV_BHVR_SENDCHAT", "RLV_BHVR_RECVCHAT"):
            self.assertIn(check, text)
        self.assertIn("nativeAllowed(play.frozen.plan,error) && leasesIntact(play)", text)

    def test_real_animation_boundaries_and_manual_overlap(self):
        text = source("llagent.cpp")
        single = text[text.index("void LLAgent::sendAnimationRequest("):text.index("void LLAgent::sendAnimationStateReset()")]
        batch = text[text.index("void LLAgent::sendAnimationRequests("):text.index("void LLAgent::sendAnimationRequest(")]
        reset = text[text.index("void LLAgent::sendAnimationStateReset()"):text.index("void LLAgent::sendAnimationStateReset()")+1200]
        self.assertLess(single.index("fs_assistant_animation_request(anim_id)"), single.index("sendReliableMessage()"))
        self.assertIn("fs_assistant_animation_request(uuid)", batch)
        self.assertIn("fs_assistant_animation_reset()", reset)
        gestures = source("llgesturemgr.cpp")
        update = gestures[gestures.index("void LLGestureMgr::update()") :]
        self.assertLess(update.index("fs_assistant_gestures_tick()"), update.index("if (!are_gestures_enabled())"))
        replay = gestures[gestures.index("void LLGestureMgr::playGesture(LLMultiGesture*"):gestures.index("void LLGestureMgr::stopGesture(LLMultiGesture*")]
        self.assertLess(replay.index("fs_assistant_animation_request("), replay.index("gesture->reset()"))
        self.assertLess(replay.index("fs_assistant_animation_request("), replay.index("gAssetStorage->getAssetData("))
        incoming = source("llviewermessage.cpp")
        self.assertIn("assistant_source != gAgent.getID()", incoming)
        self.assertIn("fs_assistant_animation_request(animation_id)", incoming)

    def test_cleanup_never_stops_foreign_or_superseding_state(self):
        text = source("fsgesturelistener.cpp")
        self.assertIn("gAgentAvatarp->mAnimationSources", text)
        self.assertIn("entry.first!=gAgent.getID()", text)
        self.assertIn("cleanupAllowed(ready() && play.identity==source(),leases.owns(id.asString(),lease),foreign(id))", text)
        self.assertIn("if(owned(play,entry.first,entry.second))gAgent.sendAnimationRequest", text)
        self.assertIn("else play.incomplete=true", text)
        self.assertIn("leases.acquire(id.asString());gAgent.sendAnimationRequest", text)
        self.assertIn("play.animations[id]=leases.current(id.asString())", text)
        self.assertIn("gesture_request_tombstone_capacity", text)
        self.assertIn('result["cleanup_complete"]=!play.incomplete', text)

    def test_local_preview_acquisition_and_manual_claim_boundaries(self):
        text = source("fsgesturelistener.cpp")
        self.assertIn("!animationAvailable(foreign(id),gAgentAvatarp->mSignaledAnimations.count(id)", text)
        self.assertIn("gAgentAvatarp->mPlayingAnimations.count(id),gAgentAvatarp->isMotionActive(id)", text)
        preview = source("llpreviewanim.cpp")
        play = preview[preview.index("void LLPreviewAnim::play("):preview.index("void LLPreviewAnim::draw()")]
        self.assertLess(play.index("fs_assistant_animation_request(itemID)"), play.index("gAgentAvatarp->startMotion("))
        agent = source("llagentlistener.cpp")
        local = agent[agent.index("void LLAgentListener::playAnimation("):agent.index("void LLAgentListener::stopAnimation(")]
        self.assertLess(local.index("fs_assistant_animation_request(item->getAssetUUID())"), local.index("gAgentAvatarp->startMotion("))
        bvh = source("llfloaterbvhpreview.cpp")
        reset = bvh[bvh.index("void LLFloaterBvhPreview::resetMotion()"):bvh.index("bool LLFloaterBvhPreview::handleMouseDown(")]
        self.assertLess(reset.index("if (avatarp->isSelf()) fs_assistant_animation_reset()"), reset.index("avatarp->deactivateAllMotions()"))
        # Simulator acknowledgments are not manual local-preview intent.
        self.assertNotIn("fs_assistant_animation_request", source("llvoavatar.cpp"))

    def test_registered_builtin_states_cannot_invoke_agent_controls(self):
        text = source("fsgesturelistener.cpp")
        native = text[text.index("bool nativeAllowed("):text.index("bool foreign(")]
        self.assertIn("animationPolicy(plan", native)
        self.assertIn("gAnimLibrary.animStateToString(LLUUID(id))!=nullptr", native)
        model = source("fsassistantgesturemodel.h")
        policy = model[model.index("template<typename Builtin>"):model.index("class Timeline")]
        self.assertIn("Effect::AnimationStart || step.effect==Effect::AnimationStop", policy)
        self.assertIn("unsupported_builtin_animation", policy)
        # This native lookup does not register unknown custom asset IDs.
        library = (ROOT.parent / "llcharacter" / "llanimationstates.cpp").read_text()
        lookup = library[library.index("const char *LLAnimationLibrary::animStateToString"):library.index("LLUUID LLAnimationLibrary::stringToAnimState")]
        self.assertLess(lookup.index("mAnimMap.count(state)"), lookup.index("mAnimMap[state]"))


if __name__ == "__main__":
    unittest.main()
