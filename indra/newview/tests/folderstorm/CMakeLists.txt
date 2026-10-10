cmake_minimum_required(VERSION 3.16)
project(FolderstormRegressionTests LANGUAGES CXX)

# These production helpers have no viewer/prebuilt dependencies. This project
# can run in ordinary CI without configuring or downloading the entire viewer.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

function(folderstorm_check name)
  add_executable(${name} ${ARGN})
  if(MSVC)
    target_compile_options(${name} PRIVATE /W4 /utf-8 /UNDEBUG)
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  endif()
  add_test(NAME ${name} COMMAND ${name})
endfunction()

folderstorm_check(chrome_layout
  ../test_fschromelayout.cpp ../../fschromelayout.cpp)
folderstorm_check(snapshot_upload ../../fssnapshotupload_test.cpp)
folderstorm_check(assistant_policy ../../fsassistantpolicy_test.cpp)
folderstorm_check(world_view_geometry ../test_fsworldviewgeometry.cpp)
folderstorm_check(link_replacement
  ../test_fslinkreplacement.cpp ../../fslinkreplacement.cpp)
folderstorm_check(assistant_permissions ../test_fsassistantpermissions.cpp)
folderstorm_check(inventory_bulk
  ../test_fsinventorybulkmodel.cpp ../../fsinventorybulkmodel.cpp)
folderstorm_check(workspace_layout
  ../test_fsworkspacelayout.cpp ../../fsworkspacelayout.cpp)
folderstorm_check(snapshot_geometry ../test_fssnapshotgeometry.cpp)
folderstorm_check(workspace_usability
  ../test_fsworkspaceusability.cpp)

# Session protocol/model/controller checks are portable; they do not build the
# Win32 host or viewer. Keep assertions enabled even with Release's NDEBUG.
foreach(check IN ITEMS protocol chat contact_policy options preview transition restart registry monitors controller presentation usability alerts shortcuts monitor_chat)
  folderstorm_check(session_${check}
    ../../../../tools/session-host/tests/test_${check}.cpp)
  target_include_directories(session_${check} PRIVATE ../..)
endforeach()

# Coroutine provenance uses the viewer's Boost fibers. Ordinary portable CI
# without Boost still runs the inventory model/executor suite above.
cmake_policy(PUSH)
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()
if(POLICY CMP0167)
  # Keep the FindBoost fallback for the project's CMake 3.16 compatibility.
  cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost QUIET COMPONENTS fiber context)
cmake_policy(POP)
if(Boost_FOUND AND TARGET Boost::fiber AND TARGET Boost::context)
  find_package(Threads REQUIRED)
  folderstorm_check(inventory_bulk_scope ../test_fsinventorybulkscope.cpp)
  target_link_libraries(inventory_bulk_scope PRIVATE Boost::fiber Boost::context Threads::Threads)
endif()
