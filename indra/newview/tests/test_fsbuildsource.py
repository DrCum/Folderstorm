"""Native boundary contracts; portable model and MCP tests cover state/inputs."""
import pathlib
import re
import unittest
import xml.etree.ElementTree as ET
ROOT = pathlib.Path(__file__).resolve().parents[1]
class BuildingNativeTests(unittest.TestCase):
    def test_guarded_exact_source_and_native_inventory_parity(self):
        text=(ROOT/'fsbuildlistener.cpp').read_text()
        dispatch=text[text.index('void FSBuildListener::dispatch'):]
        self.assertLess(dispatch.index('fs_assistant_request_allowed(request)'),dispatch.index('fs_assistant_async_context(request)'))
        self.assertIn('request["__assistant_epoch"].asString()!=FSEventAPIBridge::instance().sessionReference()',dispatch)
        self.assertIn('!llsd_equals(state,entry->value["state"])',dispatch)
        for gate in ('result->getIsLinkType()', 'LLInventoryType::cannotRestrictPermissions', 'GP_OBJECT_SET_SALE', 'GP_OBJECT_MANIPULATE', 'II_FLAGS_OBJECT_SLAM_PERM','PERM_COPY','PERM_MODIFY','PERM_TRANSFER'):
            self.assertIn(gate,text)
        self.assertNotRegex(text,r'set(?:Base|Owner|Group|Everyone)Bits\(')
        self.assertIn('entry->value["after"]=itemState(changed)',dispatch)
        self.assertIn('target_changed: inventory changed before deferred submission',text)
    def test_world_native_limits_and_selection_independence(self):
        text=(ROOT/'fsbuildlistener.cpp').read_text()
        for guard in ('permYouOwner()', 'permModify()', 'permMove()', 'flagUsePhysics()', 'RlvActions::canEdit(target)', 'RlvActions::canInteract(target)', 'RLV_MODIFIER_FARTOUCHDIST', 'RlvActions::canRez()', 'inAgentParcel(', 'allowAgentBuild()', 'getRegionMinPrimScale()', 'getRegionMaxPrimScaleNoMesh()', 'assistantActiveAllowed()', 'UPD_LINKED_SETS'):
            self.assertIn(guard,text)
        self.assertNotIn('LLSelectMgr::getInstance()',text)
        self.assertNotIn('FLAGS_CREATE_SELECTED',text)
        self.assertIn('addBOOLFast(_PREHASH_RemoveItem,false)',text)
        self.assertIn('pack_permissions_slam(msg,target->getFlags(),target->getPermissions())',text)
        self.assertIn('!target->getPermissions().allowCopyBy(gAgent.getID())',text)
    def test_pbr_captures_guard_and_checks_at_transport(self):
        text=(ROOT/'fsbuildlistener.cpp').read_text()
        coro=text[text.index('LLCoros::instance().launch("assistantMaterial"'):text.index('result["status"]="queued"')]
        self.assertLess(coro.index('fs_assistant::asyncTaskSubmit(task)'),coro.index('postAndSuspend('))
        self.assertLess(coro.index('fs_assistant::asyncTaskSubmit(task)'),coro.index('prepared->executed=true'))
        self.assertNotIn('AsyncScope',coro)
        self.assertIn('setRetries(0)',coro)
        self.assertIn('setFollowRedirects(false)',coro)
        self.assertNotIn('queueApply(',text)
    def test_permissions_widgets_and_capability(self):
        ET.parse(ROOT/'skins/default/xui/en/panel_preferences_local_assistant.xml')
        classes=(ROOT/'fsassistantpermissions.h').read_text()
        xml=(ROOT/'skins/default/xui/en/panel_preferences_local_assistant.xml').read_text()
        for name in ('build_edit','build_rez','inventory_permissions'):
            self.assertIn(f'"{name}"',classes)
            self.assertIn(f'name="local_assistant_perm_{name}"',xml)
        features=(ROOT/'fsassistantfeatures.h').read_text()
        self.assertEqual(len(re.findall(r'\{"FSBuild",',features)),8)
