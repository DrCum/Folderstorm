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

    fsanimationkeys.h

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
# <FS:Perf> Curve layout must match in every consumer, including viewer/PCH targets.
target_compile_definitions(llcharacter PUBLIC
    FS_CONTIGUOUS_ANIMATION_KEYS=$<BOOL:${FS_CONTIGUOUS_ANIMATION_KEYS}>)
# </FS:Perf>

target_link_libraries(
        llcharacter
        llcommon
        llmath
        llmessage
        llfilesystem
        llxml
    )

# <FS:Perf> Exercise actual joint caches, curve sampling and SIMD skinning math when testing.
if(LL_TESTS)
  add_executable(avatar_hand_pose_lifetime tests/hand_pose_lifetime_test.cpp)
  target_link_libraries(avatar_hand_pose_lifetime PRIVATE llcharacter)
  add_custom_command(TARGET avatar_hand_pose_lifetime POST_BUILD
    COMMAND $<TARGET_FILE:avatar_hand_pose_lifetime> VERBATIM)

  foreach(storage IN ITEMS map contiguous)
    add_executable(avatar_animation_keys_${storage} tests/test_fsanimationkeys.cpp)
    target_link_libraries(avatar_animation_keys_${storage} PRIVATE llcommon)
    target_include_directories(avatar_animation_keys_${storage} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(storage STREQUAL "contiguous")
      target_compile_definitions(avatar_animation_keys_${storage} PRIVATE FS_CONTIGUOUS_ANIMATION_KEYS=1)
    else()
      target_compile_definitions(avatar_animation_keys_${storage} PRIVATE FS_CONTIGUOUS_ANIMATION_KEYS=0)
    endif()
    add_custom_command(TARGET avatar_animation_keys_${storage} POST_BUILD
      COMMAND $<TARGET_FILE:avatar_animation_keys_${storage}> VERBATIM)
  endforeach()

  add_executable(avatar_joint_matrix_cache tests/lljoint_matrix_cache_test.cpp)
  target_link_libraries(avatar_joint_matrix_cache PRIVATE llcharacter)
  target_include_directories(avatar_joint_matrix_cache PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_custom_command(TARGET avatar_joint_matrix_cache POST_BUILD
    COMMAND $<TARGET_FILE:avatar_joint_matrix_cache> VERBATIM)

  find_package(Python3 QUIET COMPONENTS Interpreter)
  if(Python3_Interpreter_FOUND)
    set(curve_source "${CMAKE_CURRENT_BINARY_DIR}/curve_sampling.cpp")
    add_custom_command(OUTPUT "${curve_source}"
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_sampling.py" --output "${curve_source}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_sampling.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/llkeyframemotion.cpp"
      VERBATIM)
    add_executable(avatar_curve_sampling "${curve_source}")
    target_link_libraries(avatar_curve_sampling PRIVATE llmath llcommon)
    target_include_directories(avatar_curve_sampling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(avatar_curve_sampling PRIVATE FS_EXPECT_CONST_CURVES=1
      FS_CONTIGUOUS_ANIMATION_KEYS=$<BOOL:${FS_CONTIGUOUS_ANIMATION_KEYS}>)
    add_custom_command(TARGET avatar_curve_sampling POST_BUILD
      COMMAND $<TARGET_FILE:avatar_curve_sampling> VERBATIM)

    set(storage_source "${CMAKE_CURRENT_BINARY_DIR}/curve_storage.cpp")
    add_custom_command(OUTPUT "${storage_source}"
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_storage.py" --output "${storage_source}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_storage.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_sampling.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/llkeyframemotion.cpp"
      VERBATIM)
    add_executable(avatar_curve_storage_experiment "${storage_source}")
    target_link_libraries(avatar_curve_storage_experiment PRIVATE llmath llcommon)
    target_compile_definitions(avatar_curve_storage_experiment PRIVATE FS_EXPECT_CONST_CURVES=1)
    add_custom_command(TARGET avatar_curve_storage_experiment POST_BUILD
      COMMAND $<TARGET_FILE:avatar_curve_storage_experiment> VERBATIM)

    set(bulk_source "${CMAKE_CURRENT_BINARY_DIR}/curve_bulk_loading.cpp")
    add_custom_command(OUTPUT "${bulk_source}"
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_storage.py" --refined --output "${bulk_source}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_storage.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_curve_sampling.py"
        "${CMAKE_CURRENT_SOURCE_DIR}/llkeyframemotion.cpp"
      VERBATIM)
    add_executable(avatar_curve_bulk_loading_experiment "${bulk_source}")
    target_link_libraries(avatar_curve_bulk_loading_experiment PRIVATE llmath llcommon)
    target_compile_definitions(avatar_curve_bulk_loading_experiment PRIVATE FS_EXPECT_CONST_CURVES=1)
    add_custom_command(TARGET avatar_curve_bulk_loading_experiment POST_BUILD
      COMMAND $<TARGET_FILE:avatar_curve_bulk_loading_experiment> VERBATIM)

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
