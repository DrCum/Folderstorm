# -*- cmake -*-
include_guard()

include(Prebuilt)
include(Linking)

add_library( ll::openjpeg INTERFACE IMPORTED )

# AVX2 viewer builds use OpenJPEG 2.5.3 compiled with named AVX2 DWT/MCT
# kernels (not -march=native). Non-AVX2 configurations keep Linden's generic
# 3p-openjpeg from autobuild.xml. Darwin is unchanged.
if (USE_AVX2_OPTIMIZATION AND NOT DARWIN)
  set(_openjpeg_avx2_stage "${CMAKE_SOURCE_DIR}/../scripts/3p-openjpeg-avx2/stage")
  if (EXISTS "${_openjpeg_avx2_stage}/lib/release/libopenjp2.a" OR
      EXISTS "${_openjpeg_avx2_stage}/lib/release/openjp2.lib")
    find_library(OPENJPEG_AVX2_LIBRARY
      NAMES openjp2 libopenjp2.a openjp2.lib
      PATHS "${_openjpeg_avx2_stage}/lib/release" REQUIRED NO_DEFAULT_PATH)
    target_link_libraries(ll::openjpeg INTERFACE ${OPENJPEG_AVX2_LIBRARY})
    target_include_directories(ll::openjpeg SYSTEM INTERFACE
      "${_openjpeg_avx2_stage}/include/openjpeg")
    message(STATUS "OpenJPEG: using AVX2 3p at ${_openjpeg_avx2_stage}")
  else ()
    include(FetchContent)

    set(_openjpeg_saved_c_flags "${CMAKE_C_FLAGS}")
    set(_openjpeg_saved_c_flags_release "${CMAKE_C_FLAGS_RELEASE}")
    if (DEFINED BUILD_SHARED_LIBS)
      set(_openjpeg_saved_shared "${BUILD_SHARED_LIBS}")
      set(_openjpeg_had_shared TRUE)
    else ()
      set(_openjpeg_had_shared FALSE)
    endif ()

    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_CODEC OFF CACHE BOOL "Build OpenJPEG CLI tools" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Build OpenJPEG tests" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "Build OpenJPEG docs" FORCE)
    set(BUILD_JAVA OFF CACHE BOOL "Build OpenJPEG Java" FORCE)
    set(BUILD_LUTS_GENERATOR OFF CACHE BOOL "Build OpenJPEG LUT generator" FORCE)
    # OpenJPEG 2.5.3 still declares cmake_minimum_required(3.5).
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

    if (WINDOWS)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:AVX2")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /arch:AVX2")
    else ()
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -mavx2 -DNDEBUG")
    endif ()

    FetchContent_Declare(openjpeg_avx2
      GIT_REPOSITORY https://github.com/uclouvain/openjpeg.git
      GIT_TAG v2.5.3
      GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(openjpeg_avx2)

    set(CMAKE_C_FLAGS "${_openjpeg_saved_c_flags}")
    set(CMAKE_C_FLAGS_RELEASE "${_openjpeg_saved_c_flags_release}")
    if (_openjpeg_had_shared)
      set(BUILD_SHARED_LIBS ${_openjpeg_saved_shared})
    else ()
      unset(BUILD_SHARED_LIBS)
    endif ()

    target_link_libraries(ll::openjpeg INTERFACE openjp2)
    target_include_directories(ll::openjpeg SYSTEM INTERFACE
      "${openjpeg_avx2_SOURCE_DIR}/src/lib/openjp2"
      "${openjpeg_avx2_BINARY_DIR}/src/lib/openjp2"
    )
    message(STATUS "OpenJPEG: building v2.5.3 from source with AVX2")
  endif ()

  target_compile_definitions(ll::openjpeg INTERFACE LL_OPENJPEG_AVX2=1)
  if (UNIX)
    target_link_libraries(ll::openjpeg INTERFACE pthread m)
  endif ()
else ()
  use_system_binary(openjpeg)
  use_prebuilt_binary(openjpeg)

  find_library(OPENJPEG_LIBRARY
      NAMES
      openjp2
      openjp2.lib
      libopenjp2.a
      libopenjp2.so
      PATHS "${ARCH_PREBUILT_DIRS_RELEASE}" REQUIRED NO_DEFAULT_PATH)

  target_link_libraries(ll::openjpeg INTERFACE ${OPENJPEG_LIBRARY})
  target_include_directories(ll::openjpeg SYSTEM INTERFACE ${LIBS_PREBUILT_DIR}/include/openjpeg)
endif ()
