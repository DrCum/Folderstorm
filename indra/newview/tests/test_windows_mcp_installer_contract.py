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
import os
import shutil
import subprocess
import tempfile
import textwrap
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

    def test_reusable_nsis_action_signs_helper_before_packaging(self):
        workflow = (ROOT / ".github/workflows/build.yaml").read_text()
        job = workflow[workflow.index("  sign-and-package-windows:"):]
        job = job[:job.index("  sign-and-package-mac:")]
        self.assertLess(job.index("uses: actions/checkout@"), job.index("uses: ./.github/actions/sign-pkg-windows"))
        action = (ROOT / ".github/actions/sign-pkg-windows/action.yaml").read_text()
        signing = action[action.index("    - name: Sign the executables"):]
        signing = signing[:signing.index("    - name: Setup Velopack CLI")]
        self.assertIn("inputs.installer_type != 'velopack'", signing)
        self.assertIn(".app/fs-mcp-launcher-maintenance.exe", signing)
        self.assertLess(action.index("    - name: Sign the executables"), action.index("    - name: Build the installer"))

    @unittest.skipIf(os.name == "nt" or not shutil.which("bash"), "POSIX bash fixture; Windows signing requires credentials")
    def test_actual_nsis_signing_loop_includes_helper_and_stops_on_failure(self):
        action = (ROOT / ".github/actions/sign-pkg-windows/action.yaml").read_text()
        signing = action[action.index("    - name: Sign the executables"):]
        signing = signing[:signing.index("    - name: Setup Velopack CLI")]
        script = textwrap.dedent(signing[signing.index("      run: |\n") + len("      run: |\n"):])
        # Substitute only workflow expressions; run the real vendored loop with
        # a fake signer, never real credentials or AzureSignTool.
        import re
        script = re.sub(r"\$\{\{[^}]*\}\}", "fixture", script)
        stub = '''python() {
          printf '%s\\n' "${@: -1}" >> "$SIGNING_TEST_LOG"
          if [[ "$FAIL_HELPER_SIGNING" == 1 && "${@: -1}" == *.app/fs-mcp-launcher-maintenance.exe ]]; then
            return 17
          fi
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "signed.log"
            env = dict(os.environ, SIGNING_TEST_LOG=str(log), FAIL_HELPER_SIGNING="0")
            result = subprocess.run(["bash", "-e", "-o", "pipefail", "-c", stub + script + "\necho packaged\n"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(log.read_text().splitlines(), [".app/SecondLifeViewer.exe", ".app/llplugin/dullahan_host.exe", ".app/fs-mcp-launcher-maintenance.exe"])
            self.assertIn("packaged", result.stdout)
            env["FAIL_HELPER_SIGNING"] = "1"
            result = subprocess.run(["bash", "-e", "-o", "pipefail", "-c", stub + script + "\necho packaged\n"], env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 17)
            self.assertNotIn("packaged", result.stdout)


if __name__ == "__main__":
    unittest.main()
