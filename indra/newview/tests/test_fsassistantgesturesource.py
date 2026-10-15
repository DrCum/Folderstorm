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
        gesture = text[text.index("static void loaded(const LLUUID&"):text.index("static void nextAnimation(")]
        self.assertLess(gesture.index("size>static_cast<S32>(maxAssetBytes)"), gesture.index("std::string bytes(size"))
        self.assertIn("entry.hash=fs_content::hash(bytes)", text)
        self.assertIn('request["asset_hash"].asString()!=frozen.hash', text)
        self.assertIn('operation.validation["effects"]=effects(frozen.plan)', text)
        self.assertIn("evidence(item)==load->itemEvidence", text)
        self.assertIn("load->identity==source()", text)
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
        for state in ("AWAY", "STANDUP", "PRE_JUMP", "LAND", "MEDIUM_LAND", "STAND", "SIT_GROUND_CONSTRAINED", "DO_NOT_DISTURB", "TYPE"):
            self.assertIn("mAnimMap[ANIM_AGENT_" + state + "]", library)

    def test_referenced_animation_assets_are_guarded_and_frozen_before_play(self):
        text = source("fsgesturelistener.cpp")
        next_load = text[text.index("static void nextAnimation("):text.index("static void loadedAnimation(")]
        self.assertLess(next_load.index("loadAllowed(load)"), next_load.index("gAssetStorage->getAssetData("))
        self.assertIn("load->references[load->nextAnimation]", next_load)
        self.assertIn("inspectionsComplete(load->material,error)", next_load)
        self.assertLess(next_load.index("inspectionsComplete(load->material,error)"), next_load.index("self->cache[load->item]"))
        callback = text[text.index("static void loadedAnimation("):text.index("FSGestureListener::FSGestureListener()")]
        self.assertIn("fs_assistant::AsyncScope scope(load->task->guard())", callback)
        self.assertIn("loadAllowed(load)", callback)
        self.assertIn("load->task->claim", callback)
        self.assertIn("asset!=LLUUID(load->references[load->nextAnimation])", callback)
        self.assertIn("animationBytesAllowed(static_cast<std::size_t>(size),load->material.animationBytes)", callback)
        self.assertIn("animationHeader(bytes,error)", callback)
        self.assertIn("{fs_content::hash(bytes),size}", callback)
        prep = text[text.index("bool FSGestureListener::prepareAssistant("):text.index("bool FSGestureListener::validateAssistant(")]
        self.assertIn("State::animationMaterial(frozen,animation.first,error)", prep)
        self.assertIn('operation.validation["animation_assets"]=State::animationAssets(frozen)', prep)
        self.assertIn('row["after_name"]="SHA256 "+animation.second.hash', prep)
        apply = text[text.index("bool apply(Playback& play"):text.index("    void tick()")]
        self.assertLess(apply.index("animationMaterial(play.frozen,id,error)"), apply.index("asyncTaskSubmit(play.task)"))
        material = text[text.index("static bool animationMaterial("):text.index("static bool loadAllowed(")]
        self.assertIn("fs_content::hash(bytes)!=found->second.hash", material)
        self.assertIn("return animationHeader(bytes,error)", material)

    def test_animation_header_matches_native_emote_dispatch(self):
        model = source("fsassistantgesturemodel.h")
        header = model[model.index("inline bool animationHeader("):model.index("class Timeline")]
        self.assertIn("u16(0)!=1 || u16(2)!=0", header)
        self.assertIn("bytes[12]!='\\0'", header)
        native = (ROOT.parent / "llcharacter" / "llkeyframemotion.cpp").read_text()
        decode = native[native.index("bool LLKeyframeMotion::deserialize("):native.index("bool LLKeyframeMotion::deserialize(")+6000]
        fields = re.findall(r'dp\.unpack(?:U16|S32|F32|String)\([^\n]*"([a-z_]+)"', decode)
        self.assertEqual(fields[:5], ["version", "sub_version", "base_priority", "duration", "emote_name"])
        activate = native[native.index("bool LLKeyframeMotion::onActivate()"):native.index("bool LLKeyframeMotion::onActivate()")+700]
        self.assertIn("mCharacter->startMotion(mJointMotionList->mEmoteID)", activate)

    def test_native_region_signal_prevents_region_uuid_aba(self):
        text = source("fsgesturelistener.cpp")
        constructor = text[text.index("FSGestureListener::FSGestureListener()"):text.index("FSGestureListener::~FSGestureListener()")]
        self.assertLess(constructor.index("gAgent.addRegionChangedCallback("), constructor.index('for(const auto* op:'))
        self.assertIn("self->regionChanged()", constructor)
        self.assertIn("boost::signals2::scoped_connection regionConnection", text)
        self.assertIn('result["region_generation"]=std::to_string(regionEpoch.generation())', text)
        event = text[text.index("void regionChanged()"):text.index("LLSD row(")]
        self.assertLess(event.index("regionEpoch.changed()"), event.index("sync()"))
        self.assertIn('playing->task->guard()->cancel("region_changed")', event)
        self.assertIn('load->task->guard()->cancel("region_changed")', event)
        self.assertIn("it=loads().erase(it)", event)

    def test_reads_must_return_every_byte_before_hash_or_parse(self):
        text=source("fsgesturelistener.cpp")
        for start,end in (("static bool animationMaterial(","static bool loadAllowed("),
                          ("static void loaded(const LLUUID&","static void nextAnimation("),
                          ("static void loadedAnimation(","FSGestureListener::FSGestureListener()")):
            method=text[text.index(start):text.index(end)]
            self.assertIn("file.getLastBytesRead()!=size",method)
            self.assertLess(method.index("file.getLastBytesRead()!=size"),method.index("fs_content::hash(bytes)"))
        filesystem=(ROOT.parent / "llfilesystem" / "llfilesystem.cpp").read_text()
        self.assertIn("mBytesRead = static_cast<S32>(fread(buffer, 1, bytes, file))",filesystem)

    def test_both_loaded_motion_and_global_keyframe_emotes_are_read_only_checked(self):
        text=source("fsgesturelistener.cpp")
        check=text[text.index("bool cachedAnimationAllowed("):text.index("bool nativeAllowed(")]
        self.assertIn("LLKeyframeDataCache::hasEmbeddedEmote(id)",check)
        self.assertIn("gAgentAvatarp->findMotion(id)",check)
        self.assertIn("keyframe->hasEmbeddedEmote()",check)
        self.assertNotIn("setEmote(",check)
        material=text[text.index("static bool animationMaterial("):text.index("static bool loadAllowed(")]
        self.assertIn("cachedAnimationAllowed(id,error)",material)
        native=(ROOT.parent / "llcharacter" / "llkeyframemotion.cpp").read_text()
        live=native[native.index("bool LLKeyframeMotion::hasEmbeddedEmote()"):native.index("bool LLKeyframeMotion::hasEmbeddedEmote()")+250]
        cache=native[native.index("bool LLKeyframeDataCache::hasEmbeddedEmote("):native.index("bool LLKeyframeDataCache::hasEmbeddedEmote(")+350]
        self.assertIn("mJointMotionList->mEmoteName.empty()",live)
        self.assertIn("mJointMotionList->mEmoteID.notNull()",live)
        self.assertIn("sKeyframeDataMap.find(id)",cache)
        self.assertIn("data->mEmoteName.empty()",cache)
        self.assertIn("data->mEmoteID.notNull()",cache)
        self.assertNotIn("sKeyframeDataMap[id]",cache)


if __name__ == "__main__":
    unittest.main()
