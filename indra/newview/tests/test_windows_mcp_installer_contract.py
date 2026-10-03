# $LicenseInfo:firstyear=2026&license=viewerlgpl$
# Copyright (c) 2026 Folderstorm contributors.
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation;
# version 2.1 of the License only.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# Lesser General Public License for more details.
# $/LicenseInfo$

"""Installer integration invariants; native filesystem races are tested by Go on Windows."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[3]


class WindowsMCPInstallerContract(unittest.TestCase):
    def test_installers_never_create_or_retarget_shared_alias(self):
        for name in ["llvelopack.cpp", "installers/windows/installer_template.nsi"]:
            source = (ROOT / "indra/newview" / name).read_text()
            self.assertNotIn("CreateSymbolicLink", source)
            self.assertNotIn("create_fs_mcp_launch_link", source)
            self.assertNotIn("CreateFsMcpLaunchLink", source)
            self.assertNotIn("fs-mcp.exe.replacing", source)

    def test_nsis_cleanup_precedes_packaged_target_removal(self):
        source = (ROOT / "indra/newview/installers/windows/installer_template.nsi").read_text()
        self.assertLess(source.index("Call un.RemoveOwnedLegacyFsMcpLinks"), source.index("Call un.ProgramFiles"))
        cleanup = source[source.index("Function un.RemoveOwnedLegacyFsMcpLinks"):]
        cleanup = cleanup[:cleanup.index("FunctionEnd")]
        self.assertIn("fs-mcp-launcher-maintenance.exe", cleanup)
        self.assertIn('cleanup-owned-legacy --install-dir "$INSTDIR\\."', cleanup)
        self.assertIn("nsExec::ExecToStack /TIMEOUT=5000", cleanup)
        self.assertNotIn("DeleteFile", cleanup)
        self.assertNotIn("SetFileAttributes", cleanup)

    def test_velopack_uses_packaged_helper_only_before_uninstall(self):
        source = (ROOT / "indra/newview/llvelopack.cpp").read_text()
        uninstall = source[source.index("static void on_before_uninstall("):]
        uninstall = uninstall[:uninstall.index("static void on_log_message")]
        self.assertIn("cleanup_owned_legacy_mcp_links(get_install_dir())", uninstall)
        self.assertNotIn("vpkc_app_set_hook_after_update", source)
        self.assertIn("CreateProcessW(helper.c_str()", source)
        self.assertIn("WaitForSingleObject(process.hProcess, 5000)", source)
        self.assertNotIn("ShellExecute", source[source.index("static void cleanup_owned_legacy_mcp_links"):source.index("static void on_after_install")])

    def test_setup_never_selects_programdata_and_checks_update_manifest(self):
        source = (ROOT / "indra/newview/fspanelpreferencelocalassistant.cpp").read_text()
        configuration = source[source.index("void FSPanelPreferenceLocalAssistant::refreshConfiguration"):]
        configuration = configuration[:configuration.index("void FSPanelPreferenceLocalAssistant::copyConfiguration")]
        self.assertNotIn('getoptenv("ProgramData")', configuration)
        self.assertNotIn("filesystem::equivalent", configuration)
        self.assertIn("FSAssistantLaunchPath::resolve", configuration)
        self.assertIn("launch.canCopy()", configuration)
        self.assertIn("cursor_adapter = client ==", configuration)

    def test_build_staging_and_signing_include_helper(self):
        cmake = (ROOT / "indra/newview/CMakeLists.txt").read_text()
        manifest = (ROOT / "indra/newview/viewer_manifest.py").read_text()
        signing = (ROOT / "indra/newview/fs_viewer_manifest.py").read_text()
        self.assertIn("tools/windows-mcp-launcher", cmake)
        self.assertIn("add_custom_target(fs-mcp-launcher-maintenance ALL", cmake)
        self.assertIn("add_dependencies(copy_w_viewer_manifest migrate-settings fs-mcp fs-mcp-launcher-maintenance)", cmake)
        self.assertIn('self.path(self.stage_fs_mcp_launcher_maintenance(), "fs-mcp-launcher-maintenance.exe")', manifest)
        self.assertIn('fs-mcp-launcher-maintenance.exe', signing)


if __name__ == "__main__":
    unittest.main()
