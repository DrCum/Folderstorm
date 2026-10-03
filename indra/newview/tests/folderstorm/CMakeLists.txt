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
    target_compile_options(${name} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -Werror)
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
