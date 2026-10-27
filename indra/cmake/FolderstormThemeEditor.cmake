include_guard(GLOBAL)
option(FOLDERSTORM_BUILD_THEME_EDITOR "Build/package the optional Windows theme lab" OFF)
set(FOLDERSTORM_THEME_EDITOR_VERSION "0.20.0" CACHE STRING "Theme lab application version")
set(FOLDERSTORM_THEME_EDITOR_PUBLISH_DIR "")
set(FOLDERSTORM_THEME_EDITOR_STAMP "")
get_filename_component(FOLDERSTORM_THEME_EDITOR_HELPER "${CMAKE_CURRENT_LIST_DIR}/../../scripts/folderstorm_theme_editor.py" ABSOLUTE)
if(NOT FOLDERSTORM_BUILD_THEME_EDITOR)
  return()
endif()
if(NOT WINDOWS OR NOT FS_RELEASE_ARCH STREQUAL "x86_64")
  message(FATAL_ERROR "The optional theme editor currently requires Windows x64")
endif()
find_program(FOLDERSTORM_THEME_EDITOR_DOTNET NAMES dotnet DOC "Optional theme lab .NET SDK executable" REQUIRED)
execute_process(COMMAND "${FOLDERSTORM_THEME_EDITOR_DOTNET}" --version OUTPUT_VARIABLE editor_sdk OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE editor_sdk_result)
if(NOT editor_sdk_result EQUAL 0 OR NOT editor_sdk MATCHES "^[1-9][0-9]+\\.")
  message(FATAL_ERROR "Theme lab requires .NET SDK 10 or newer; set FOLDERSTORM_THEME_EDITOR_DOTNET")
endif()
if(NOT FOLDERSTORM_THEME_EDITOR_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
  message(FATAL_ERROR "Use a three-part theme editor version")
endif()
string(LENGTH "${FS_SOURCE_COMMIT}" editor_source_length)
if(NOT editor_source_length EQUAL 40 OR NOT FS_SOURCE_COMMIT MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Theme lab packaging needs an exact FS_SOURCE_COMMIT")
endif()
get_filename_component(editor_root "${CMAKE_CURRENT_LIST_DIR}/../../tools/theme-editor" ABSOLUTE)
get_filename_component(editor_sealer "${CMAKE_CURRENT_LIST_DIR}/../../scripts/folderstorm_theme_editor.py" ABSOLUTE)
set(FOLDERSTORM_THEME_EDITOR_PUBLISH_DIR "${CMAKE_CURRENT_BINARY_DIR}/theme-editor-publish/${FS_SOURCE_COMMIT}/${FOLDERSTORM_THEME_EDITOR_VERSION}/$<CONFIG>")
set(FOLDERSTORM_THEME_EDITOR_STAMP "${FOLDERSTORM_THEME_EDITOR_PUBLISH_DIR}/.folderstorm-theme-editor.json")
file(GLOB_RECURSE editor_inputs CONFIGURE_DEPENDS "${editor_root}/Folderstorm.Theme.Core/*.cs" "${editor_root}/Folderstorm.Theme.Core/*.csproj"
  "${editor_root}/Folderstorm.Theme.Editor/*.cs" "${editor_root}/Folderstorm.Theme.Editor/*.csproj" "${editor_root}/Folderstorm.Theme.Editor/*.xaml"
  "${editor_root}/Folderstorm.Theme.Editor/*.resx" "${editor_root}/Folderstorm.Theme.Editor/*.ico" "${editor_root}/Folderstorm.Theme.Editor/*.png")
list(FILTER editor_inputs EXCLUDE REGEX "/(bin|obj)/")
add_custom_command(OUTPUT "${FOLDERSTORM_THEME_EDITOR_STAMP}"
  COMMAND "${CMAKE_COMMAND}" -E env --unset=LIB "${FOLDERSTORM_THEME_EDITOR_DOTNET}" publish "${editor_root}/Folderstorm.Theme.Editor/Folderstorm.Theme.Editor.csproj"
    --configuration Release --runtime win-x64 --self-contained true --nologo --output "${FOLDERSTORM_THEME_EDITOR_PUBLISH_DIR}"
    --artifacts-path "${CMAKE_CURRENT_BINARY_DIR}/theme-editor-artifacts/${FS_SOURCE_COMMIT}"
    -m:1 -nr:false -p:UseSharedCompilation=false -p:NuGetAudit=false "-p:Version=${FOLDERSTORM_THEME_EDITOR_VERSION}"
    "-p:InformationalVersion=${FOLDERSTORM_THEME_EDITOR_VERSION}+${FS_SOURCE_COMMIT}"
  COMMAND "${PYTHON_EXECUTABLE}" "${editor_sealer}" "${FOLDERSTORM_THEME_EDITOR_PUBLISH_DIR}" --source "${FS_SOURCE_COMMIT}" --version "${FOLDERSTORM_THEME_EDITOR_VERSION}"
  DEPENDS ${editor_inputs} "${editor_root}/Directory.Build.props" "${editor_root}/contracts/xui-edit-rules.json" "${editor_sealer}"
  COMMENT "Publishing optional self-contained theme lab" VERBATIM)
add_custom_target(folderstorm-theme-editor DEPENDS "${FOLDERSTORM_THEME_EDITOR_STAMP}")
