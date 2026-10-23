# -*- cmake -*-

project(llcharacter)

include(00-Common)
include(LLCommon)

set(llcharacter_SOURCE_FILES
    llanimationstates.cpp
    llbvhloader.cpp
    llcharacter.cpp
    lleditingmotion.cpp
    llgesture.cpp
    llhandmotion.cpp
    llheadrotmotion.cpp
    lljoint.cpp
    lljointsolverrp3.cpp
    llkeyframefallmotion.cpp
    llkeyframemotion.cpp
    llkeyframestandmotion.cpp
    llkeyframewalkmotion.cpp
    llmotioncontroller.cpp
    llmotion.cpp
    llmultigesture.cpp
    llpose.cpp
    lltargetingmotion.cpp
    llvisualparam.cpp
    )

set(llcharacter_HEADER_FILES
    CMakeLists.txt

    llanimationstates.h
    llbvhloader.h
    llbvhconsts.h
    llcharacter.h
    lleditingmotion.h
    llgesture.h
    llhandmotion.h
    llheadrotmotion.h
    lljoint.h
    lljointsolverrp3.h
    lljointstate.h
    llkeyframefallmotion.h
    llkeyframemotion.h
    llkeyframestandmotion.h
    llkeyframewalkmotion.h
    llmotion.h
    llmotioncontroller.h
    llmultigesture.h
    llpose.h
    lltargetingmotion.h
    llvisualparam.h
    )

list(APPEND llcharacter_SOURCE_FILES ${llcharacter_HEADER_FILES})

add_library (llcharacter ${llcharacter_SOURCE_FILES})
target_include_directories( llcharacter  INTERFACE   ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
        llcharacter
        llcommon
        llmath
        llmessage
        llfilesystem
        llxml
    )

# <FS:Perf> Exercise actual joint caches and SIMD skinning math when testing.
if(LL_TESTS)
  add_executable(avatar_joint_matrix_cache tests/lljoint_matrix_cache_test.cpp)
  target_link_libraries(avatar_joint_matrix_cache PRIVATE llcharacter)
  target_include_directories(avatar_joint_matrix_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_custom_command(TARGET avatar_joint_matrix_cache POST_BUILD
    COMMAND $<TARGET_FILE:avatar_joint_matrix_cache> VERBATIM)

  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    foreach(mode IN ITEMS tracked benchmark)
      set(skinning_source "${CMAKE_CURRENT_BINARY_DIR}/skinning_generation_${mode}.cpp")
      add_custom_command(OUTPUT "${skinning_source}"
        COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/../newview/tests/test_skinning_generation.py"
          --mode "${mode}" --output "${skinning_source}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/../newview/tests/test_skinning_generation.py"
          "${CMAKE_CURRENT_SOURCE_DIR}/../newview/llskinningutil.cpp"
          "${CMAKE_CURRENT_SOURCE_DIR}/lljoint.h"
        VERBATIM)
      add_executable(avatar_skinning_generation_${mode} "${skinning_source}")
      target_link_libraries(avatar_skinning_generation_${mode} PRIVATE llmath llcommon)
    endforeach()
    target_compile_definitions(avatar_skinning_generation_tracked PRIVATE FS_NATIVE_SKINNING_MATH=1)
    add_custom_command(TARGET avatar_skinning_generation_tracked POST_BUILD
      COMMAND $<TARGET_FILE:avatar_skinning_generation_tracked> VERBATIM)
  endif()
endif()
# </FS:Perf>
