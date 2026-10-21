"""Focused login support and Windows shortcut integration checks.

FS_MINGW_CXX optionally names a MinGW compiler for the affected runtime only.
These checks never run an installer, launch Windows binaries or change shortcuts.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
VIEWER = ROOT / "indra/newview"


def function(source, signature):
    begin = source.index(signature)
    start = source.index("{", begin)
    end, depth = start + 1, 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[begin:end]


class InstallSupport(unittest.TestCase):
    def test_login_support_rewrites_and_idempotence(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node is required for the extracted splash JavaScript check")
        native = (VIEWER / "fspanellogin.cpp").read_text()
        script = native.split('R"JS(', 1)[1].split(')JS";', 1)[0]
        patch = function(script, "function patchSupportLinks()")
        test = r'''
const assert = require('node:assert/strict');
let writes=0;
function element(href) {
  const attrs=href?{href,target:'_blank'}:{};
  return {html:'',getAttribute:k=>attrs[k] || null,
    setAttribute:(k,v)=>{assert.notEqual(attrs[k],v,'avoid unchanged mutation');attrs[k]=v;++writes;},
    removeAttribute:k=>{delete attrs[k];++writes;},
    set innerHTML(v){this.html=v;++writes;},get innerHTML(){return this.html;},
    parentNode:{querySelector:()=>heading}};
}
const heading={textContent:'Wiki & JIRA'};
const latest=element();
const legacy=element('https://phoenixviewer.com/app/file-a-jira/');
const relative=element('/app/file-a-jira/');
const unrelated=element('https://phoenixviewer.com.evil.test/app/file-a-jira/');
const unrelatedBefore=unrelated.getAttribute('href');
const window={location:{href:'https://phoenixviewer.com/app/loginV3/'}};
const document={querySelector:()=>latest,querySelectorAll:()=>[legacy,relative,unrelated]};
''' + patch + r'''
patchSupportLinks();
assert.match(latest.innerHTML,/Folderstorm updates/);
assert.match(latest.innerHTML,/folderstorm_support\/updates/);
assert.doesNotMatch(latest.innerHTML,/current-firestorm-release|Latest version/);
assert.equal(legacy.getAttribute('href'),'secondlife:///app/folderstorm_support/report-bug');
assert.equal(relative.getAttribute('href'),legacy.getAttribute('href'));
assert.equal(legacy.getAttribute('target'),null);
assert.equal(unrelated.getAttribute('href'),unrelatedBefore);
assert.equal(heading.textContent,'Help & bug reports');
const before=writes;for(let i=0;i<100;++i)patchSupportLinks();assert.equal(writes,before);
'''
        with tempfile.TemporaryDirectory(prefix="folderstorm-login-") as temporary:
            path = Path(temporary) / "test.js"
            path.write_text(script)
            subprocess.run([node, "--check", str(path)], check=True)
            path.write_text(test)
            subprocess.run([node, str(path)], check=True)

    def test_native_wiring_and_non_destructive_coexistence(self):
        notifications = ET.parse(VIEWER / "skins/default/xui/en/notifications.xml").getroot()
        names = {entry.get("name") for entry in notifications.findall("notification")}
        self.assertIn("FolderstormSeparateInstallation", names)
        menu = ET.parse(VIEWER / "skins/default/xui/en/menu_viewer.xml").getroot()
        self.assertTrue(any(entry.get("function") == "Help.FolderstormShortcuts" for entry in menu.iter()))
        startup = function((VIEWER / "llstartup.cpp").read_text(), "void uninstall_nsis_if_required()\n{")
        self.assertIn('"FolderstormSeparateInstallation"', startup)
        self.assertNotIn("clear_nsis_links", startup)
        hooks = (VIEWER / "llvelopack.cpp").read_text()
        first = function(hooks, "static void on_first_run(")
        self.assertNotIn("Firestorm_x64", first)
        self.assertNotIn("saveToFile", first)
        after = function(hooks, "static void on_after_install(")
        self.assertNotIn("create_shortcuts", after)
        self.assertNotIn("offerShortcuts", after)
        self.assertIn("fs_update::removeShortcuts", function(hooks, "static void on_before_uninstall("))
        viewer = (VIEWER / "llappviewer.cpp").read_text()
        self.assertNotIn("fs_update::offerShortcuts()", viewer)
        self.assertNotIn("fs_crash::offerConsent()", viewer)
        self.assertIn("FSInstallViewer::poll();", viewer)
        viewer_menu = (VIEWER / "llviewermenu.cpp").read_text()
        self.assertIn("FSInstallViewer::shortcuts();", viewer_menu)
        self.assertNotIn("fs_update::configureShortcuts()", viewer_menu)
        host = (ROOT / "tools/session-host/main.cpp").read_text()
        self.assertIn("fs_update::offerShortcuts();", host)
        self.assertIn("fs_update::configureShortcuts()", host)
        handler = (VIEWER / "fsreleaseviewer.cpp").read_text()
        self.assertIn('LLCommandHandler("folderstorm_support",UNTRUSTED_CLICK_ONLY)', handler)
        for name in ("fscrashviewer.cpp", "llviewermenu.cpp"):
            self.assertIn("fs_crash::reportUrl()", (VIEWER / name).read_text())
        self.assertIn("fs_crash::reportUrl()", host)
        shortcuts = (VIEWER / "fsinstallwindows.h").read_text()
        removal = function(shortcuts, "inline void remove()")
        self.assertIn('RegDeleteValueW(registry,L"ShortcutsCommit")', removal)
        self.assertNotIn('RegDeleteValueW(registry,L"ShortcutsV1")', removal)
        migration = function(shortcuts, "inline void removeLegacyRegistration(")
        self.assertIn("CompareStringOrdinal(command,-1,expected.c_str(),-1,TRUE)", migration)
        self.assertIn("legacyRegistrationOwned(name,matches)", migration)

    def test_update_target_link_signatures(self):
        cmake = os.environ.get("CMAKE", "cmake")
        if not shutil.which(cmake):
            self.skipTest("CMake is required for the configure-only target check")
        with tempfile.TemporaryDirectory(prefix="folderstorm-install-cmake-") as temporary:
            directory = Path(temporary)
            for name in ("fsupdateconfig.h.in", "fssessionprotocol.h", "fsupdate.cpp"):
                shutil.copyfile(VIEWER / name, directory / name)
            (directory / "main.cpp").write_text("int main(){return 0;}\n")
            (directory / "CMakeLists.txt").write_text(r'''
cmake_minimum_required(VERSION 3.16)
project(InstallTargetCheck LANGUAGES CXX)
set(WINDOWS ON)
set(USE_VELOPACK ON)
set(FS_SOURCE_COMMIT "0123456789abcdef0123456789abcdef01234567")
set(VIEWER_BINARY_NAME viewer)
include("${FOLDERSTORM_ROOT}/indra/cmake/FolderstormUpdates.cmake")
add_library(dependency INTERFACE)
add_executable(viewer main.cpp)
target_link_libraries(viewer dependency)
folderstorm_update_target(viewer)
target_link_libraries(viewer dependency)
add_executable(host main.cpp)
target_link_libraries(host PRIVATE dependency)
folderstorm_update_target(host)
target_link_libraries(host PRIVATE dependency)
foreach(target IN ITEMS viewer host)
  get_target_property(libraries ${target} LINK_LIBRARIES)
  foreach(required IN ITEMS ole32 shell32 uuid advapi32 user32)
    if(NOT "${required}" IN_LIST libraries)
      message(FATAL_ERROR "Missing Windows shortcut dependency: ${required}")
    endif()
  endforeach()
endforeach()
''')
            result = subprocess.run([cmake,"-S",str(directory),"-B",str(directory/"build"),
                                     "-DFOLDERSTORM_ROOT="+str(ROOT)],text=True,capture_output=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_win64_affected_runtime_compile_and_link(self):
        compiler = os.environ.get("FS_MINGW_CXX") or shutil.which("x86_64-w64-mingw32-g++")
        if not compiler:
            self.skipTest("Set FS_MINGW_CXX for a focused Win64 cross-compile/link check")
        with tempfile.TemporaryDirectory(prefix="folderstorm-install-win64-") as temporary:
            directory = Path(temporary)
            values = {"VIEWER_SHORT_VERSION":"7.2.5","VIEWER_VERSION_REVISION":"82153",
                      "FS_RELEASE_PLATFORM":"windows","FS_RELEASE_ARCH":"x86_64","FS_RELEASE_CPU":"avx2",
                      "FS_RELEASE_FLAVOR":"opensim","FS_RELEASE_INSTALLER":"velopack",
                      "FS_RELEASE_DEFAULT_CHANNEL":"nightly","FS_RELEASE_FEED_URL":"https://example.test/feed",
                      "FS_RELEASE_DOWNLOAD_HOSTS":"example.test","FS_SOURCE_COMMIT":"a"*40}
            for name in ("fsupdateconfig.h", "fsreleasebuild.h"):
                content = (VIEWER / (name + ".in")).read_text()
                for key, value in values.items():
                    content = content.replace("@"+key+"@", value)
                (directory / name).write_text(content)
            (directory / "main.cpp").write_text('#include "fsupdate.h"\nint main(){return fs_update::lastError().empty()?0:1;}\n')
            subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                            "-DWINVER=0x0600","-D_WIN32_WINNT=0x0600","-DNOMINMAX","-DWIN32_LEAN_AND_MEAN",
                            "-I"+str(directory),"-I"+str(VIEWER),str(VIEWER/"fsupdate.cpp"),str(directory/"main.cpp"),
                            "-lole32","-lshell32","-luuid","-ladvapi32","-luser32","-o",str(directory/"check.exe")],check=True)
            self.assertTrue((directory / "check.exe").is_file())
            # Also compile the actual changed early-hook functions without the
            # viewer: their executable lookup must work before gDirUtilp exists.
            hooks = (VIEWER / "llvelopack.cpp").read_text()
            names = ("static std::wstring get_viewer_exe_name()", "static void register_uninstall_info(",
                     "static void unregister_uninstall_info()")
            source = '#include <windows.h>\n#include <filesystem>\n#include <string>\n#include <vector>\n#include <ctime>\n#include "fsupdateconfig.h"\n'
            source += "\n".join(function(hooks,name) for name in names)
            source += '\nint main(){(void)&get_viewer_exe_name;(void)&register_uninstall_info;(void)&unregister_uninstall_info;}\n'
            (directory / "hooks.cpp").write_text(source)
            subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-pedantic",
                            "-DWINVER=0x0600","-D_WIN32_WINNT=0x0600","-DNOMINMAX","-DWIN32_LEAN_AND_MEAN",
                            "-I"+str(directory),"-fsyntax-only",str(directory/"hooks.cpp")],check=True)


if __name__ == "__main__":
    unittest.main()
