"""Focused source-boundary regression checks; no native viewer build required."""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(name):
    text = (ROOT / name).read_text()
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


class SocialNativeBoundaryTests(unittest.TestCase):
    def test_external_source_uses_only_opaque_bridge_reference(self):
        text = source("fsassistantsocial.cpp")
        model = source("fsassistantsocialmodel.h")
        method = text[text.index("LLSD publicSource()const"):text.index("void flush()")]
        self.assertIn("publicSourceFields(account.asString(),viewerSession,grid,privacyEpoch)", method)
        self.assertNotIn("login", method)
        self.assertNotIn("getSessionID", method)
        self.assertIn("FSEventAPIBridge::instance().sessionReference()", text)
        self.assertIn("!viewerSession.empty() && viewerSession==bridgeReference()", text)
        self.assertIn("else if(bindingChanged){flush();viewerSession=opaque;}", text)
        self.assertIn('reader.binding=viewerSession+":"+grid', text)
        self.assertNotIn("login.asString()", text)
        # Internal live-message checks may read login_id. No external LLSD field
        # may write any native credential, including through a renamed key.
        self.assertNotRegex(text, r'\["(?:login_id|native_session_id|agent_session_id)"\]\s*=(?!=)')
        self.assertNotRegex(text, r'\[[^\n]+\]\s*=\s*(?:login|gAgent\.getSessionID\(\))\s*;')
        self.assertNotRegex(text, r'\["source"\]\s*=\s*(?!publicSource\(|self->publicSource\(|mState->publicSource\()')
        fields = model[model.index("publicSourceFields("):model.index("template<class T> class Stream")]
        self.assertEqual(set(re.findall(r'\{"([a-z_]+)"\s*,', fields)), {"account_id", "viewer_session", "grid", "privacy_epoch"})

    def test_exact_registered_native_surface_and_authority(self):
        text = source("fsassistantsocial.cpp")
        expected = {
            "FSChat": {"conversations", "history", "search", "open", "draftGet", "draftSet", "sendNearby", "sendIM", "sendGroup", "sendConference", "markRead", "close"},
            "FSContacts": {"friends", "search", "profile"},
            "FSEvents": {"poll", "notifications", "getNotification", "reviewNotification"},
        }
        for api, operations in expected.items():
            registered = re.search(r'"' + api + r'","[^"]+",std::vector<std::string>\{([^}]+)', text)
            self.assertIsNotNone(registered)
            self.assertEqual(set(re.findall(r'"([^"]+)"', registered[1])), operations)
        dispatch = text[text.index("void dispatch(const LLSD& request)"):]
        self.assertLess(dispatch.index("fs_assistant_request_allowed(request)"), dispatch.index("fs_assistant_async_context(request)"))
        self.assertLess(dispatch.index("fs_assistant::AsyncScope scope(guard)"), dispatch.index("mState->readChat(request,error)"))
        self.assertNotIn("mMsgs", text)
        self.assertNotIn("loadHistory(", text)

    def test_profile_transport_captures_authority_and_generation(self):
        text = source("llavatarpropertiesprocessor.cpp")
        coro = text[text.index("void LLAvatarPropertiesProcessor::requestAvatarPropertiesCoro"):text.index("void LLAvatarPropertiesProcessor::processAvatarLegacyPropertiesReply")]
        self.assertLess(coro.index("asyncTaskSubmit(task)"), coro.index("getAndSuspend"))
        self.assertGreater(coro.index("asyncTaskAllowed(task)"), coro.index("getAndSuspend"))
        self.assertIn("mProfileLoads.finish({avatar_id,type},task)", coro)
        self.assertIn("account!=gAgentID || login!=gAgent.getSessionID()", coro)
        self.assertNotIn("AsyncScope", coro)  # TLS must never span a coroutine yield

    def test_translation_origin_and_close_fence_use_ingress(self):
        text = source("llimview.cpp")
        live = text[text.index("void LLIMModel::addMessage(const LLUUID& session_id"):text.index("void LLIMModel::processAddingMessage")]
        self.assertLess(live.index("++sAssistantLiveIngress"), live.index("LLTranslate::translateMessage"))
        self.assertEqual(live.count("source_account!=gAgentID || source_login!=gAgent.getSessionID()"), 2)
        self.assertIn('arg["ingress_sequence"] = std::to_string(live_sequence)', text)
        adapter = source("fsassistantsocial.cpp")
        self.assertIn('data["login_id"].asUUID()!=self->login', adapter)
        self.assertIn("fs_assistant_social::reopens", adapter)
        self.assertIn('closed[id]=LLIMModel::assistantLiveSequence()', adapter)

    def test_no_chatbar_commands_or_notification_responses(self):
        text = source("fsassistantsocial.cpp")
        self.assertIn("FSNearbyChat::sendChatFromViewer(text,text,CHAT_TYPE_NORMAL,false,0)", text)
        for forbidden in ("processChat(", "stripChannelNumber(", "triggerAndReviseString(", "->respond(", "forceResponse("):
            self.assertNotIn(forbidden, text)
        self.assertIn('operation.requiredClasses.push_back("notifications_read")', text)
        self.assertIn('operation.requiredClasses.push_back("chat_read")', text)

    def test_nearby_rechecks_native_receive_policy_before_every_delivery(self):
        text = source("fsassistantsocial.cpp")
        policy = text[text.index("bool nearbyAllowed("):text.index("LLSD publicChatRow(")]
        for behaviour in ("RECVCHAT", "RECVCHATFROM", "RECVEMOTE", "RECVEMOTEFROM"):
            self.assertIn("gRlvHandler.hasBehaviour(RLV_BHVR_" + behaviour + ")", policy)
            self.assertIn("gRlvHandler.isException(RLV_BHVR_" + behaviour + ",sender)", policy)
        self.assertIn("nearbyReceiveAllowed(nearbyOrigin(row),policy)", policy)
        enqueue = text[text.index("void FSAssistantSocial::nearbyMessage("):]
        self.assertLess(enqueue.index("if(!nearbyAllowed(row))return"), enqueue.index('streams["chat"].append'))
        for field in ("source_type", "type", "emote", "owned_attachment"):
            self.assertIn('row["__nearby"]["' + field + '"]', enqueue)
        self.assertIn("message.mChatStyle==CHAT_STYLE_IRC || RlvUtil::isEmote(message.mText)", enqueue)
        delivery = text[text.index("bool rowAllowed("):text.index("LLSD conversation(")]
        self.assertIn('if(row["topic"].asString()=="nearby")return nearbyAllowed(row)', delivery)
        self.assertLess(delivery.index("nearbyAllowed(row)"), delivery.index("canReceiveIM"))
        self.assertIn('name=="chat" && !rowAllowed(entry.value)', text)
        self.assertIn('if(!rowAllowed(entry.value)', text)
        self.assertIn('result.erase("__nearby")', text)
        self.assertIn('event["data"]=name=="chat"?publicChatRow(entry.value):entry.value', text)
        self.assertIn('auto row=publicChatRow(entry.value)', text)


if __name__ == "__main__":
    unittest.main()
