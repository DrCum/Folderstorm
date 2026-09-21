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

    # OpenJPEG 2.5.3 uses option(BUILD_SHARED_LIBS ON). A local
    # set(BUILD_SHARED_LIBS OFF) is not enough on MSVC: option() writes ON
    # into the cache and emits a DLL whose bin/ then collects viewer EXEs.
    if (DEFINED CACHE{BUILD_SHARED_LIBS})
      set(_openjpeg_saved_shared_cache "${BUILD_SHARED_LIBS}")
      set(_openjpeg_had_shared_cache TRUE)
    else ()
      set(_openjpeg_had_shared_cache FALSE)
    endif ()
    if (DEFINED BUILD_SHARED_LIBS)
      set(_openjpeg_saved_shared "${BUILD_SHARED_LIBS}")
      set(_openjpeg_had_shared TRUE)
    else ()
      set(_openjpeg_had_shared FALSE)
    endif ()

    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    # OpenJPEG 2.5.3 also honors BUILD_STATIC_LIBS (default ON).
    set(BUILD_STATIC_LIBS ON CACHE BOOL "Build OpenJPEG static library" FORCE)
    set(BUILD_CODEC OFF CACHE BOOL "Build OpenJPEG CLI tools" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "Build OpenJPEG tests" FORCE)
    set(BUILD_DOC OFF CACHE BOOL "Build OpenJPEG docs" FORCE)
    set(BUILD_JAVA OFF CACHE BOOL "Build OpenJPEG Java" FORCE)
    set(BUILD_LUTS_GENERATOR OFF CACHE BOOL "Build OpenJPEG LUT generator" FORCE)
    set(BUILD_JPIP OFF CACHE BOOL "Build OpenJPEG JPIP" FORCE)
    # OpenJPEG 2.5.3 still declares cmake_minimum_required(3.5).
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

    if (WINDOWS)
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} /arch:AVX2")
      set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /arch:AVX2")

      # Isolate output dirs so OpenJPEG cannot become the viewer's runtime dir.
      # 2.5.3 CACHE-sets EXECUTABLE_OUTPUT_PATH / LIBRARY_OUTPUT_PATH to its
      # own bin/, which then collects slplugin.exe and media plugins.
      set(_openjpeg_isolated_out "${CMAKE_CURRENT_BINARY_DIR}/openjpeg-avx2-out")
      file(MAKE_DIRECTORY "${_openjpeg_isolated_out}")
      set(_openjpeg_output_vars
        CMAKE_RUNTIME_OUTPUT_DIRECTORY
        CMAKE_LIBRARY_OUTPUT_DIRECTORY
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY
        CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE
        CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE
        CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
        CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO
        CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO
        EXECUTABLE_OUTPUT_PATH
        LIBRARY_OUTPUT_PATH)
      foreach(_openjpeg_var ${_openjpeg_output_vars})
        if (DEFINED CACHE{${_openjpeg_var}})
          set(_openjpeg_saved_${_openjpeg_var}_cache "${${_openjpeg_var}}")
          set(_openjpeg_had_${_openjpeg_var}_cache TRUE)
        else ()
          set(_openjpeg_had_${_openjpeg_var}_cache FALSE)
        endif ()
        if (DEFINED ${_openjpeg_var})
          set(_openjpeg_saved_${_openjpeg_var} "${${_openjpeg_var}}")
          set(_openjpeg_had_${_openjpeg_var} TRUE)
        else ()
          set(_openjpeg_had_${_openjpeg_var} FALSE)
        endif ()
        set(${_openjpeg_var} "${_openjpeg_isolated_out}" CACHE PATH
          "Isolated OpenJPEG output directory" FORCE)
      endforeach()
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
    if (_openjpeg_had_shared_cache)
      set(BUILD_SHARED_LIBS "${_openjpeg_saved_shared_cache}" CACHE BOOL
        "Build shared libraries" FORCE)
    else ()
      unset(BUILD_SHARED_LIBS CACHE)
    endif ()
    if (_openjpeg_had_shared)
      set(BUILD_SHARED_LIBS ${_openjpeg_saved_shared})
    else ()
      unset(BUILD_SHARED_LIBS)
    endif ()

    if (WINDOWS)
      foreach(_openjpeg_var ${_openjpeg_output_vars})
        if (_openjpeg_had_${_openjpeg_var}_cache)
          set(${_openjpeg_var} "${_openjpeg_saved_${_openjpeg_var}_cache}" CACHE PATH
            "Output directory" FORCE)
        else ()
          unset(${_openjpeg_var} CACHE)
        endif ()
        if (_openjpeg_had_${_openjpeg_var})
          set(${_openjpeg_var} "${_openjpeg_saved_${_openjpeg_var}}")
        else ()
          unset(${_openjpeg_var})
        endif ()
      endforeach()
    endif ()

    if (NOT TARGET openjp2)
      message(FATAL_ERROR
        "AVX2 OpenJPEG FetchContent did not create target openjp2")
    endif ()
    get_target_property(_openjpeg_type openjp2 TYPE)
    if (NOT _openjpeg_type STREQUAL "STATIC_LIBRARY")
      message(FATAL_ERROR
        "AVX2 OpenJPEG target openjp2 is ${_openjpeg_type}, expected STATIC_LIBRARY. "
        "MSVC OpenJPEG 2.5.3 still builds a DLL unless BUILD_SHARED_LIBS is cache-forced OFF; "
        "that DLL's bin/ would collect viewer executables and break packaging.")
    endif ()
    if (WINDOWS)
      set_target_properties(openjp2 PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${_openjpeg_isolated_out}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${_openjpeg_isolated_out}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_openjpeg_isolated_out}"
        LIBRARY_OUTPUT_DIRECTORY "${_openjpeg_isolated_out}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${_openjpeg_isolated_out}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_openjpeg_isolated_out}"
        ARCHIVE_OUTPUT_DIRECTORY "${_openjpeg_isolated_out}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${_openjpeg_isolated_out}"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${_openjpeg_isolated_out}")
    endif ()

    # Link the static FetchContent target; do not copy OpenJPEG's runtime bin/.
    target_link_libraries(ll::openjpeg INTERFACE openjp2)
    target_include_directories(ll::openjpeg SYSTEM INTERFACE
      "${openjpeg_avx2_SOURCE_DIR}/src/lib/openjp2"
      "${openjpeg_avx2_BINARY_DIR}/src/lib/openjp2"
    )
    message(STATUS "OpenJPEG: building v2.5.3 from source with AVX2 (static)")
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
