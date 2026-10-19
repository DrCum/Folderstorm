"""Configure affected CMake modules without building the viewer or SDK.

Optional FS_SENTRY_NATIVE_HEADER points at the verified 0.17.3 sentry.h to
syntax-check the Windows API declarations used by fscrash.cpp with a minimal
Windows type shim. This does not compile/link the native runtime or Crashpad.
"""
from pathlib import Path
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SentryBuildCompatibility(unittest.TestCase):
    def configure(self, directory, project, *options):
        cmake = os.environ.get("CMAKE", "cmake")
        if not shutil.which(cmake):
            self.skipTest("CMake is required for the configure-only regression")
        (directory / "CMakeLists.txt").write_text(project, encoding="utf-8")
        result = subprocess.run([cmake, "-S", str(directory), "-B", str(directory / "build"),
                                 f"-DFOLDERSTORM_ROOT={ROOT}", *options], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_prebuilt_boost_exports_its_headers(self):
        with tempfile.TemporaryDirectory(prefix="folderstorm-boost-") as temporary:
            source = Path(temporary)
            (source / "Prebuilt.cmake").write_text("macro(use_prebuilt_binary name)\nendmacro()\n")
            self.configure(source, r'''
cmake_minimum_required(VERSION 3.16)
project(BoostIncludes NONE)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(LIBS_PREBUILT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/prebuilt")
set(ARCH_PREBUILT_DIRS_RELEASE "${LIBS_PREBUILT_DIR}/lib")
set(ADDRESS_SIZE 64)
file(MAKE_DIRECTORY "${LIBS_PREBUILT_DIR}/include" "${ARCH_PREBUILT_DIRS_RELEASE}")
foreach(name IN ITEMS context fiber filesystem program_options thread url wave)
  file(WRITE "${ARCH_PREBUILT_DIRS_RELEASE}/libboost_${name}.a" "")
  file(WRITE "${ARCH_PREBUILT_DIRS_RELEASE}/boost_${name}.lib" "")
endforeach()
include("${FOLDERSTORM_ROOT}/indra/cmake/Boost.cmake")
get_target_property(headers ll::boost INTERFACE_INCLUDE_DIRECTORIES)
get_target_property(system_headers ll::boost INTERFACE_SYSTEM_INCLUDE_DIRECTORIES)
if(NOT "${headers}" STREQUAL "${LIBS_PREBUILT_DIR}/include" OR
   NOT "${system_headers}" STREQUAL "${headers}")
  message(FATAL_ERROR "Boost consumers must inherit the prebuilt system headers")
endif()
''')

    def test_sdk_flag_isolation_and_target_link_signatures(self):
        with tempfile.TemporaryDirectory(prefix="folderstorm-sentry-cmake-") as temporary:
            source = Path(temporary)
            sdk = source / "sdk"
            sdk.mkdir()
            (source / "stub.cpp").write_text("int main() { return 0; }\n")
            shutil.copyfile(ROOT / "indra/newview/fscrash.cpp", source / "fscrash.cpp")
            shutil.copyfile(ROOT / "indra/newview/fscrashconfig.h.in", source / "fscrashconfig.h.in")
            (sdk / "CMakeLists.txt").write_text(r'''
get_directory_property(sdk_options COMPILE_OPTIONS)
if(sdk_options OR CMAKE_C_FLAGS MATCHES "WINVER|_WIN32_WINNT" OR
   CMAKE_CXX_FLAGS MATCHES "WINVER|_WIN32_WINNT")
  message(FATAL_ERROR "Viewer language/API flags leaked into the SDK")
endif()
if(NOT CMAKE_C_FLAGS MATCHES "/DFOO=1" OR NOT CMAKE_C_FLAGS MATCHES "/Z7" OR
   NOT CMAKE_CXX_FLAGS MATCHES "/DBAR=1" OR NOT CMAKE_CXX_FLAGS MATCHES "/Z7")
  message(FATAL_ERROR "Unrelated flags or SDK debug information were lost")
endif()
if(NOT SENTRY_BACKEND STREQUAL "crashpad" OR NOT SENTRY_TRANSPORT STREQUAL "winhttp")
  message(FATAL_ERROR "Native backend/transport changed")
endif()
add_library(sentry INTERFACE)
add_executable(crashpad_handler "${CMAKE_CURRENT_SOURCE_DIR}/../stub.cpp")
''')
            self.configure(source, r'''
cmake_minimum_required(VERSION 3.16)
project(SentryCompatibility LANGUAGES C CXX)
set(WINDOWS ON)
set(FS_SOURCE_COMMIT "0123456789abcdef0123456789abcdef01234567" CACHE STRING "")
set(FETCHCONTENT_SOURCE_DIR_FOLDERSTORM_SENTRY_NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
set(FETCHCONTENT_FULLY_DISCONNECTED ON)
set(CMAKE_C_FLAGS "/DWINVER=0x0600 -D_WIN32_WINNT=0x0600 /DFOO=1")
set(CMAKE_CXX_FLAGS "-DWINVER=0x0600 /D_WIN32_WINNT=0x0600 /DBAR=1")
set(original_c "${CMAKE_C_FLAGS}")
set(original_cxx "${CMAKE_CXX_FLAGS}")
add_compile_options(/TP /W3)
add_link_options(/NODEFAULTLIB:LIBCMT)
get_directory_property(original_options COMPILE_OPTIONS)
get_directory_property(original_link_options LINK_OPTIONS)
include("${FOLDERSTORM_ROOT}/indra/cmake/FolderstormSentry.cmake")
get_directory_property(restored_options COMPILE_OPTIONS)
get_directory_property(restored_link_options LINK_OPTIONS)
if(NOT "${CMAKE_C_FLAGS}" STREQUAL "${original_c}" OR
   NOT "${CMAKE_CXX_FLAGS}" STREQUAL "${original_cxx}" OR
   NOT "${restored_options}" STREQUAL "${original_options}" OR
   NOT "${restored_link_options}" STREQUAL "${original_link_options}")
  message(FATAL_ERROR "Viewer flags were changed by SDK configuration")
endif()
set(VIEWER_BINARY_NAME viewer)
add_library(viewer_dependency INTERFACE)
add_executable(viewer stub.cpp)
# These intentionally match the native viewer's plain and host's keyword style.
target_link_libraries(viewer viewer_dependency)
folderstorm_crash_target(viewer)
target_link_libraries(viewer viewer_dependency)
add_executable(host stub.cpp)
target_link_libraries(host PRIVATE viewer_dependency)
folderstorm_crash_target(host)
target_link_libraries(host PRIVATE viewer_dependency)
foreach(target IN ITEMS viewer host)
  get_target_property(libraries ${target} LINK_LIBRARIES)
  if(FS_ENABLE_SENTRY)
    if(NOT FS_HAS_SENTRY EQUAL 1 OR NOT "sentry" IN_LIST libraries)
      message(FATAL_ERROR "Enabled target lost its SDK")
    endif()
    get_target_property(dependencies ${target} MANUALLY_ADDED_DEPENDENCIES)
    if(NOT "crashpad_handler" IN_LIST dependencies)
      message(FATAL_ERROR "Enabled target lost its matching handler")
    endif()
  elseif(FS_HAS_SENTRY OR "sentry" IN_LIST libraries OR TARGET sentry)
    message(FATAL_ERROR "Disabled build unexpectedly enables the SDK")
  endif()
endforeach()
''', "-DFS_ENABLE_SENTRY=ON")
            # Reconfigure the same tree: the disabled path must not add SDK targets.
            self.configure(source, (source / "CMakeLists.txt").read_text(), "-DFS_ENABLE_SENTRY=OFF")

    def test_windows_sentry_calls_exist_in_pinned_header(self):
        header = os.environ.get("FS_SENTRY_NATIVE_HEADER")
        if not header:
            self.skipTest("Set FS_SENTRY_NATIVE_HEADER to the verified SDK 0.17.3 header")
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        if not shutil.which(compiler[0]):
            self.skipTest("C++ compiler is required for the SDK declaration check")
        runtime = (ROOT / "indra/newview/fscrash.cpp").read_text()
        functions = sorted(set(re.findall(r"\b(sentry_[a-z0-9_]+)\s*\(", runtime)))
        source = '''#define _WIN32 1
#define SENTRY_BUILD_STATIC 1
#include "sentry.h"
#if !defined(SENTRY_PLATFORM_WINDOWS) || defined(SENTRY_PLATFORM_NX)
#error Test must exercise the Windows SDK API surface
#endif
void check_declarations() {
''' + "".join(f"    (void)&{name};\n" for name in functions) + "}\n"
        with tempfile.TemporaryDirectory(prefix="folderstorm-sentry-api-") as temporary:
            directory = Path(temporary)
            shutil.copyfile(header, directory / "sentry.h")
            # The header uses this type only in the context struct. No Windows
            # runtime behavior is simulated or linked by this declaration check.
            (directory / "windows.h").write_text("typedef struct { void* record; void* context; } EXCEPTION_POINTERS;\n")
            cpp = directory / "test.cpp"
            cpp.write_text(source)
            subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror", "-pedantic",
                                      "-I", str(directory), "-fsyntax-only", str(cpp)], check=True)


if __name__ == "__main__":
    unittest.main()
