# -*- cmake -*-

project(llcharacter)

include(00-Common)
include(LLCommon)

set(llcharacter_SOURCE_FILES
    fssamplingcontext.cpp
    fssamplingshadow.cpp
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
    fssamplingcontext.h
    fssamplingidentity.h
    fssamplingshadow.h

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
  find_package(Threads REQUIRED)
  add_executable(avatar_sampling_shadow tests/sampling_shadow_test.cpp)
  target_link_libraries(avatar_sampling_shadow PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_sampling_shadow POST_BUILD
    COMMAND $<TARGET_FILE:avatar_sampling_shadow> VERBATIM)
  add_executable(avatar_sampling_shadow_allocations tests/sampling_shadow_test.cpp)
  target_link_libraries(avatar_sampling_shadow_allocations PRIVATE llcharacter Threads::Threads)
  target_compile_definitions(avatar_sampling_shadow_allocations PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
  add_executable(avatar_sampling_shadow_cost tests/sampling_shadow_cost_benchmark.cpp)
  target_link_libraries(avatar_sampling_shadow_cost PRIVATE llcharacter Threads::Threads)

  foreach(probe IN ITEMS cost allocations)
    add_executable(avatar_current_motion_${probe} tests/current_motion_cost_benchmark.cpp)
    target_link_libraries(avatar_current_motion_${probe} PRIVATE llcharacter Threads::Threads)
    if(probe STREQUAL "allocations")
      target_compile_definitions(avatar_current_motion_${probe} PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
    endif()
  endforeach()

  add_executable(avatar_current_motion_context tests/current_motion_context_test.cpp)
  target_link_libraries(avatar_current_motion_context PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_current_motion_context POST_BUILD
    COMMAND $<TARGET_FILE:avatar_current_motion_context> VERBATIM)

  add_executable(avatar_sampling_context_cost tests/sampling_context_cost_benchmark.cpp)
  target_link_libraries(avatar_sampling_context_cost PRIVATE llcharacter Threads::Threads)

  add_executable(avatar_sampling_fp_math tests/sampling_fp_math_test.cpp)
  target_link_libraries(avatar_sampling_fp_math PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_sampling_fp_math POST_BUILD
    COMMAND $<TARGET_FILE:avatar_sampling_fp_math> VERBATIM)

  add_executable(avatar_sampling_numeric_context tests/sampling_numeric_context_test.cpp)
  target_link_libraries(avatar_sampling_numeric_context PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_sampling_numeric_context POST_BUILD
    COMMAND $<TARGET_FILE:avatar_sampling_numeric_context> VERBATIM)

  add_executable(avatar_initial_motion_mask tests/initial_motion_mask_test.cpp)
  target_link_libraries(avatar_initial_motion_mask PRIVATE llcharacter)
  add_custom_command(TARGET avatar_initial_motion_mask POST_BUILD
    COMMAND $<TARGET_FILE:avatar_initial_motion_mask> VERBATIM)

  add_executable(avatar_blender_pool_failures tests/blender_pool_failure_test.cpp)
  target_link_libraries(avatar_blender_pool_failures PRIVATE llcharacter)
  target_compile_definitions(avatar_blender_pool_failures PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
  add_custom_command(TARGET avatar_blender_pool_failures POST_BUILD
    COMMAND $<TARGET_FILE:avatar_blender_pool_failures> --allocations VERBATIM)

  # Explicit experiments; neither target changes the production pool.
  add_executable(avatar_blender_pool_index_experiment EXCLUDE_FROM_ALL tests/blender_pool_index_experiment.cpp)
  target_link_libraries(avatar_blender_pool_index_experiment PRIVATE llcharacter)
  add_executable(avatar_blender_pool_index_allocations EXCLUDE_FROM_ALL tests/blender_pool_index_experiment.cpp)
  target_link_libraries(avatar_blender_pool_index_allocations PRIVATE llcharacter)
  target_compile_definitions(avatar_blender_pool_index_allocations PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
  add_executable(avatar_blender_pool_hint_experiment EXCLUDE_FROM_ALL tests/blender_pool_hint_experiment.cpp)
  target_link_libraries(avatar_blender_pool_hint_experiment PRIVATE llcharacter)

  add_executable(avatar_active_blender tests/active_blender_test.cpp)
  target_link_libraries(avatar_active_blender PRIVATE llcharacter)
  add_custom_command(TARGET avatar_active_blender POST_BUILD
    COMMAND $<TARGET_FILE:avatar_active_blender> VERBATIM)

  add_executable(avatar_active_blender_controller tests/active_blender_controller_test.cpp)
  target_link_libraries(avatar_active_blender_controller PRIVATE llcharacter)
  add_custom_command(TARGET avatar_active_blender_controller POST_BUILD
    COMMAND $<TARGET_FILE:avatar_active_blender_controller> VERBATIM)

  add_executable(avatar_active_blender_failures tests/active_blender_test.cpp)
  target_link_libraries(avatar_active_blender_failures PRIVATE llcharacter)
  target_compile_definitions(avatar_active_blender_failures PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)

  foreach(probe IN ITEMS benchmark allocations)
    add_executable(avatar_active_blender_${probe} tests/active_blender_cost_benchmark.cpp)
    target_link_libraries(avatar_active_blender_${probe} PRIVATE llcharacter)
    if(probe STREQUAL "allocations")
      target_compile_definitions(avatar_active_blender_${probe} PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
    endif()
  endforeach()

  add_executable(avatar_motion_purge tests/motion_purge_test.cpp)
  target_link_libraries(avatar_motion_purge PRIVATE llcharacter)
  add_custom_command(TARGET avatar_motion_purge POST_BUILD
    COMMAND $<TARGET_FILE:avatar_motion_purge> VERBATIM)

  foreach(probe IN ITEMS benchmark allocations)
    add_executable(avatar_motion_purge_${probe} tests/motion_purge_cost_benchmark.cpp)
    target_link_libraries(avatar_motion_purge_${probe} PRIVATE llcharacter)
    if(probe STREQUAL "allocations")
      target_compile_definitions(avatar_motion_purge_${probe} PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
    endif()
  endforeach()

  add_executable(avatar_keyframe_phase tests/keyframe_phase_test.cpp)
  target_link_libraries(avatar_keyframe_phase PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_keyframe_phase POST_BUILD
    COMMAND $<TARGET_FILE:avatar_keyframe_phase> VERBATIM)

  add_executable(avatar_animation_constraint_names tests/animation_constraint_names_test.cpp)
  target_link_libraries(avatar_animation_constraint_names PRIVATE llcharacter)
  add_custom_command(TARGET avatar_animation_constraint_names POST_BUILD
    COMMAND $<TARGET_FILE:avatar_animation_constraint_names> VERBATIM)

  add_executable(avatar_readonly_animation_serialization tests/animation_serialization_test.cpp)
  target_link_libraries(avatar_readonly_animation_serialization PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_readonly_animation_serialization POST_BUILD
    COMMAND $<TARGET_FILE:avatar_readonly_animation_serialization> VERBATIM)

  add_executable(avatar_sampling_batch tests/sampling_batch_test.cpp)
  target_link_libraries(avatar_sampling_batch PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_sampling_batch POST_BUILD
    COMMAND $<TARGET_FILE:avatar_sampling_batch> VERBATIM)

  foreach(probe IN ITEMS benchmark allocations)
    add_executable(avatar_sampling_cost_${probe} tests/sampling_cost_benchmark.cpp)
    target_link_libraries(avatar_sampling_cost_${probe} PRIVATE llcharacter Threads::Threads)
    if(probe STREQUAL "allocations")
      target_compile_definitions(avatar_sampling_cost_${probe} PRIVATE FS_SAMPLING_ALLOCATION_PROBE=1)
    endif()
  endforeach()

  add_executable(avatar_bounded_sampling tests/bounded_sampling_test.cpp)
  target_link_libraries(avatar_bounded_sampling PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_bounded_sampling POST_BUILD
    COMMAND $<TARGET_FILE:avatar_bounded_sampling> VERBATIM)

  # Explicit --benchmark only; timing must not become a build-time pass/fail check.
  add_executable(avatar_queue_wake_benchmark tests/queue_wake_benchmark.cpp)
  target_link_libraries(avatar_queue_wake_benchmark PRIVATE llcommon Threads::Threads)

  add_executable(avatar_owned_sampling tests/owned_sampling_test.cpp)
  target_link_libraries(avatar_owned_sampling PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_owned_sampling POST_BUILD
    COMMAND $<TARGET_FILE:avatar_owned_sampling> VERBATIM)

  add_executable(avatar_keyframe_asset_ownership tests/keyframe_asset_ownership_test.cpp)
  target_link_libraries(avatar_keyframe_asset_ownership PRIVATE llcharacter Threads::Threads)
  add_custom_command(TARGET avatar_keyframe_asset_ownership POST_BUILD
    COMMAND $<TARGET_FILE:avatar_keyframe_asset_ownership> VERBATIM)

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
