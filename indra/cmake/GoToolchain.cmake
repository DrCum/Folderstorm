# -*- cmake -*-
#
# Find Go 1.25 or newer for migrate-settings and fs-mcp.
# Both programs are built into the viewer package and shipped by the installer.
#
# Search PATH first so an explicit go wins, then the usual install locations.
# GO_EXECUTABLE is the absolute path. Later build steps must call that path
# rather than relying on PATH.

set(_fs_go_min_minor 25)
set(_fs_go_hints)

# CMAKE_HOST_SYSTEM_NAME is set before project(), so this check can run
# at the very start of configure. CMAKE_HOST_WIN32 is not set until project().
if(CMAKE_HOST_WIN32 OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  if(NOT "$ENV{LOCALAPPDATA}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}/Programs/go/bin" _fs_go_localapp)
    list(APPEND _fs_go_hints "${_fs_go_localapp}")
  endif()
  list(APPEND _fs_go_hints "C:/Program Files/Go/bin")
  if(NOT "$ENV{ProgramFiles}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{ProgramFiles}/Go/bin" _fs_go_program_files)
    string(TOLOWER "${_fs_go_program_files}" _fs_go_program_files_l)
    if(NOT _fs_go_program_files_l STREQUAL "c:/program files/go/bin")
      list(APPEND _fs_go_hints "${_fs_go_program_files}")
    endif()
  endif()
elseif(CMAKE_HOST_APPLE OR CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  list(APPEND _fs_go_hints
    "/usr/local/go/bin"
    "/opt/homebrew/bin"
    "/usr/local/bin")
else()
  list(APPEND _fs_go_hints
    "/usr/local/go/bin"
    "/usr/lib/go/bin")
endif()

# Search again every configure. A cached NOTFOUND or a Go that was replaced
# on disk should not stick. The temporary cache names are cleared below.
unset(_FS_GO_ON_PATH CACHE)
find_program(_FS_GO_ON_PATH NAMES go)
set(_fs_go_found "${_FS_GO_ON_PATH}")
unset(_FS_GO_ON_PATH CACHE)

if(NOT _fs_go_found)
  unset(_FS_GO_IN_HINTS CACHE)
  find_program(_FS_GO_IN_HINTS NAMES go HINTS ${_fs_go_hints} NO_DEFAULT_PATH)
  set(_fs_go_found "${_FS_GO_IN_HINTS}")
  unset(_FS_GO_IN_HINTS CACHE)
endif()

if(NOT _fs_go_found)
  unset(GO_EXECUTABLE CACHE)
  string(REPLACE ";" "\n  " _fs_go_hint_text "${_fs_go_hints}")
  message(FATAL_ERROR
    "Go 1.25 or newer is required to build migrate-settings and fs-mcp.\n"
    "Searched PATH and:\n"
    "  ${_fs_go_hint_text}\n"
    "Install Go 1.25 or newer, or add it to PATH.")
endif()

execute_process(
  COMMAND "${_fs_go_found}" version
  RESULT_VARIABLE _fs_go_version_rc
  OUTPUT_VARIABLE _fs_go_version_out
  ERROR_VARIABLE _fs_go_version_err
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_STRIP_TRAILING_WHITESPACE
)
if(NOT _fs_go_version_rc EQUAL 0)
  message(FATAL_ERROR
    "Could not run '${_fs_go_found} version' (exit ${_fs_go_version_rc}).\n"
    "${_fs_go_version_out}\n${_fs_go_version_err}\n"
    "Go 1.25 or newer is required to build migrate-settings and fs-mcp.")
endif()
if(NOT "${_fs_go_version_err}" STREQUAL "")
  string(APPEND _fs_go_version_out "\n${_fs_go_version_err}")
endif()

# "go version go1.25.0 linux/amd64" or "go version devel go1.26-xxxx ..."
if(NOT _fs_go_version_out MATCHES "go([0-9]+)\\.([0-9]+)")
  message(FATAL_ERROR
    "Could not parse the Go version from '${_fs_go_version_out}' (${_fs_go_found}).\n"
    "Go 1.25 or newer is required to build migrate-settings and fs-mcp.")
endif()
set(_fs_go_major "${CMAKE_MATCH_1}")
set(_fs_go_minor "${CMAKE_MATCH_2}")

if(_fs_go_major LESS 1 OR (_fs_go_major EQUAL 1 AND _fs_go_minor LESS ${_fs_go_min_minor}))
  message(FATAL_ERROR
    "Found ${_fs_go_version_out} at ${_fs_go_found}.\n"
    "Go 1.25 or newer is required to build migrate-settings and fs-mcp.\n"
    "Install Go 1.25 or newer, or add it to PATH.")
endif()

if(NOT "${GO_EXECUTABLE}" STREQUAL "${_fs_go_found}")
  set(GO_EXECUTABLE "${_fs_go_found}" CACHE FILEPATH
    "Absolute path to Go 1.25 or newer, used to build migrate-settings and fs-mcp."
    FORCE)
endif()
message(STATUS "Go ${_fs_go_major}.${_fs_go_minor}: ${GO_EXECUTABLE}")

unset(_fs_go_hints)
unset(_fs_go_found)
unset(_fs_go_localapp)
unset(_fs_go_program_files)
unset(_fs_go_program_files_l)
unset(_fs_go_hint_text)
unset(_fs_go_version_rc)
unset(_fs_go_version_out)
unset(_fs_go_version_err)
unset(_fs_go_major)
unset(_fs_go_minor)
unset(_fs_go_min_minor)
