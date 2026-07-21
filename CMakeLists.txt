# ---------------------------------------------------------------------------
# Frontend2 Author:   jongough aka. Jon Gough Copyright: License:  wxWidgets
# License
# ---------------------------------------------------------------------------
# - Copyright (C)  by Jon Gough                            *
# - License:     GPLv3+
# - This program is free software; you can redistribute it and/or modify  *
# - it under the terms of the GNU General Public License as published by  *
# - the Free Software Foundation; either version 2 of the License, or     *
# - (at your option) any later version.                                   *
# - *
# - This program is distributed in the hope that it will be useful,       *
# - but WITHOUT ANY WARRANTY; without even the implied warranty of        *
# - MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
# - GNU General Public License for more details.                          *
# - *
# - You should have received a copy of the GNU General Public License     *
# - along with this program; if not, write to the                         *
# - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx,                                       *
# - xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx                                       *
# -----------------------------------------------------------------------------
# Frontend2 Author: Jon Gough Environmental_grib(TP) v1.0.362.0 19/06/2026 DD/MM/YYYY
# format buildCI:yes useODAPI:yes validateJSON:yes
# ---------------------------------------------------------------------------
# -- When changing this file do NOT change the order of the sections.    -- ##
# -- Make changes only between the section blocks identified             -- ##
# -- The whole configuration process relies on this sequence for success.-- ##
#
# -- Modify section below to include all the details for your plugin.    -- ##

set(CMLOC "CMakeLists: ")

# define minimum cmake version
cmake_minimum_required(VERSION 3.15)
if(COMMAND cmake_policy)
  if (POLICY CMP0043)
    cmake_policy(SET CMP0043 NEW)
  endif (POLICY CMP0043)
  cmake_policy(SET CMP0048 NEW)
  if (POLICY CMP0068)
    cmake_policy(SET CMP0068 NEW)
  endif (POLICY CMP0068)
  if (POLICY CMP0076)
    cmake_policy(SET CMP0076 NEW)
  endif (POLICY CMP0076)
  if (POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
  endif (POLICY CMP0077)
  if (POLICY CMP0079)
    cmake_policy(SET CMP0079 NEW)
  endif (POLICY CMP0079)
  if (POLICY CMP0175)
    cmake_policy(SET CMP0175 NEW)
  endif (POLICY CMP0175)
  if (POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
  endif (POLICY CMP0177)
endif (COMMAND cmake_policy)

# set for vscode/vscodium
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# define plugin name, owner and versions
set(VERBOSE_NAME "xGRIB")
set(COMMON_NAME "xGRIB")
set(TITLE_NAME "xgrib")
set(PACKAGE_CONTACT "Paul")
set(PACKAGE "xgrib_pi")
set(SHORT_DESCRIPTION "Display, download, and generate environmental GRIB files.")
set(LONG_DESCRIPTION
    "A catalogue-installable GRIB viewer with integrated generation of combined weather, wave, and current GRIB files using an isolated native helper."
)

set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_PATCH "0")
set(VERSION_TWEAK "1")
set(VERSION_DATE "19/07/2026") # DD/MM/YYYY format
set(OCPN_MIN_VERSION "ov511")

set(OCPN_API_VERSION_MAJOR "1")
set(OCPN_API_VERSION_MINOR "21")

set(WX_VER "32")
set(WX_VER_PATCH "8")

set(TP_COMMENT "  * Release using CI")

set(PARENT "opencpn")

# Use to set up a local webserver with git installed. Default is github.com.
# Now set in PluginSetup.cmake set(GIT_REPOSITORY_SERVER "github.com")


# Specifies Cloudsmith upload repository suffix for each catalog
set(PROD "prod") # Standard Repos
set(BETA "beta") # Standard Repos
set(ALPHA "alpha") # Standard Repos

# Set if  Cloudsmith Base Repository name does not match your Git Repository
# name. set(CLOUDSMITH_BASE_REPOSITORY "${GIT_REPOSITORY_NAME}")
# set(CLOUDSMITH_BASE_REPOSITORY "opencpn-plugins")  #Ricks's customization
set(CLOUDSMITH_BASE_REPOSITORY "xgrib")
set(PLUGIN_GIT_REPOSITORY "pob220/xgrib_pi")

# Set the following to use OpenCPN cloudsmith repositories instead of own
# repositories
set(CLOUDSMITH_USER "opencpn")
# set(CLOUDSMITH_USER "jon-gough")

set(XML_INFO_URL "https://github.com/pob220/xgrib_pi")
set(XML_SUMMARY ${SHORT_DESCRIPTION})
set(XML_DESCRIPTION ${LONG_DESCRIPTION})

# ----- Modify section above to include all the details for your plugin
#
# ----- Modify section below if there are special requirements for the plugin
#

option(PLUGIN_USE_SVG "Use SVG graphics" ON)
option(BUNDLE_GENERATOR_RUNTIME
       "Bundle the native helper's non-system runtime dependencies" ON)
option(XGRIB_USE_BUNDLED_JASPER
       "Build the pinned static Jasper dependency" ON)
set(XGRIB_EXTERNAL_GENERATOR_DIR "" CACHE PATH
    "Prebuilt generator root containing bin/ and runtime/ (Windows split-ABI builds)")

set(CMAKE_CXX_STANDARD 17)

# Use local version of GLU library requires libs/glu directory
set(USE_LOCAL_GLU FALSE)
# Prefer libGL.so to libOpenGL.so, see CMP0072
set(OpenGL_GL_PREFERENCE "LEGACY") # not operational

option(USE_GL "Enable OpenGL support" ON)
message(STATUS "${CMLOC}USE_GL: ${USE_GL}")

if ("${CMAKE_BUILD_TYPE}" STREQUAL "")
  set(CMAKE_BUILD_TYPE
      "Release"
      CACHE STRING "Choose the type of build" FORCE
  )
endif ("${CMAKE_BUILD_TYPE}" STREQUAL "")
set_property(
  CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "" "Debug" "Release" "RelWithDebInfo"
                                  "MinSizeRel"
)
message(STATUS "${CMLOC}Build type: ${CMAKE_BUILD_TYPE}")

# ----- Statements below used to setup standard variables required by the CMAKE
# ----- process -- Do not remove --

project(${PACKAGE})

set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ========================================================

include("PluginSetup")

# ========================================================
# ----- Above sets up standard variables required by CMAKE - Do not remove

# ----- Modify section below if there are special requirements for the plugin

set(CMAKE_VERBOSE_MAKEFILE "Activate verbose mode for make files" ON)

option(Plugin_CXX11 "Use c++11" OFF)

# ----- Modify section above if there are special requirements for the plugin

# ----- Do not change next section - needed to configure build process
# ===========================================================

include("PluginConfigure")

# ===========================================================
# ----- Do not change section above - needed to configure build process

# ----- Change below to match project requirements for source, headers, etc.

add_definitions(-DUSE_S57)

# ---- Section Below - Add your project SET(SRCS and SET(HDRS  etc..

set(SRCS
    src/grib_pi.cpp
    src/GribOverlayFactory.cpp
    src/GribVectorPolicy.cpp
    src/GribUIDialogBase.cpp
    src/GrabberWin.cpp
    src/GribUIDialog.cpp
    src/EnvironmentalGribDialog.cpp
    src/GeneratorJobJson.cpp
    src/XgribPaths.cpp
    src/XgribPluginPolicy.cpp
    src/CursorData.cpp
    src/GribSettingsDialog.cpp
    src/GribRequestDialog.cpp
    src/XyGribPanel.cpp
    src/XyGribModelDef.cpp
    src/icons.cpp
    src/email.cpp
    src/GribTable.cpp
    src/CustomGrid.cpp
    src/GribReader.cpp
    src/GribRecord.cpp
    src/GribV1Record.cpp
    src/GribV2Record.cpp
    src/zuFile.cpp
    src/IsoLine.cpp
    src/pi_ocpndc.cpp
    src/pi_shaders.cpp
    libs/manual/src/manual.cpp
    libs/manual/src/manual_dlg.cpp)

set(HDRS
    src/grib_pi.h
    src/GribOverlayFactory.h
    src/GribVectorPolicy.h
    src/GribUIDialogBase.h
    src/GrabberWin.h
    src/GribUIDialog.h
    src/EnvironmentalGribDialog.h
    src/GeneratorJobJson.h
    src/XgribPaths.h
    src/XgribPluginPolicy.h
    src/CursorData.h
    src/GribSettingsDialog.h
    src/GribRequestDialog.h
    src/XyGribPanel.h
    src/XyGribModelDef.h
    src/GribTable.h
    src/CustomGrid.h
    src/GribReader.h
    src/GribRecord.h
    src/GribRecordSet.h
    src/GribV1Record.h
    src/GribV2Record.h
    src/IsoLine.h
    src/pi_ocpndc.h
    src/pi_shaders.h
    libs/manual/include/manual.h
    libs/manual/include/manual_dlg.h)

if(USE_GL)
  list(APPEND SRCS src/pi_TexFont.cpp)
  list(APPEND HDRS src/pi_TexFont.h)
endif()

if(WIN32)
  list(APPEND SRCS src/smapi.cpp)
  list(APPEND HDRS src/smapi.h)
endif()

add_definitions(-DPLUGIN_USE_SVG)

# ----- Section above - Add your project SET(SRCS and SET(HDRS  etc.

# ----- If using JSON validation in plugin section below is needed

if (APPLE)
  option(OD_JSON_SCHEMA_VALIDATOR "Use JSON Schema validator" OFF)
else (APPLE)
  option(OD_JSON_SCHEMA_VALIDATOR "Use JSON Schema validator" ON)
endif (APPLE)

set(OD_JSON_SCHEMA_VALIDATOR OFF)

if (OD_JSON_SCHEMA_VALIDATOR)
  message(
    STATUS
      "${CMLOC}Using JSON Schema validation - warning this changes the 'flavour' of the executable and it may not be compatible with OpenCPN"
  )
  add_definitions(-DOD_JSON_SCHEMA_VALIDATOR)
  message(STATUS "${CMLOC}Adding JSON Schema validation")

# removed for testing
  add_subdirectory(opencpn-libs/json-schema-validator)
  target_link_libraries(${PACKAGE_NAME} ocpn::json-schema-validator)

else (OD_JSON_SCHEMA_VALIDATOR)
  message(
    STATUS
      "${CMLOC}Not using JSON Schema validation - warning invalid JSON 'may' cause unexpected crashes"
  )
endif (OD_JSON_SCHEMA_VALIDATOR)

# ----- If using JSON validation in plugin section above is needed

if (QT_ANDROID)

endif (QT_ANDROID)

# =============================================================
# Change as required to collect all the set( headers and SRCS This add_library
# can be used in conditionals below.
# =============================================================

set(SRC ${SRCS} ${HDRS})

# =============================================================
# Modify/configure 'Include' Directories below for the Plugin
# =============================================================

include_directories(BEFORE ${PROJECT_SOURCE_DIR}/include)
include_directories(BEFORE ${PROJECT_SOURCE_DIR}/ocpninclude)
include_directories(BEFORE ${PROJECT_SOURCE_DIR}/src)
include_directories(BEFORE ${PROJECT_SOURCE_DIR}/libs/manual/include)
# Change target_sources to match your project needs.
# ==============================================================

target_sources(${PACKAGE_NAME} PUBLIC ${SRC})

# The generator is a separate native process.  Keeping it out of the plugin
# address space isolates network/model parsing failures from OpenCPN while
# sharing one pinned implementation across development and catalogue builds.
if(NOT OCPN_FLATPAK_CONFIG)
include(CTest)

if(XGRIB_EXTERNAL_GENERATOR_DIR)
  if(NOT WIN32)
    message(FATAL_ERROR
      "XGRIB_EXTERNAL_GENERATOR_DIR is supported only for Windows split-ABI builds")
  endif()
  set(XGRIB_EXTERNAL_GENERATOR
      "${XGRIB_EXTERNAL_GENERATOR_DIR}/bin/environmental-grib${CMAKE_EXECUTABLE_SUFFIX}")
  if(NOT EXISTS "${XGRIB_EXTERNAL_GENERATOR}")
    message(FATAL_ERROR
      "Prebuilt environmental GRIB generator not found: ${XGRIB_EXTERNAL_GENERATOR}")
  endif()
  add_executable(environmental-grib IMPORTED GLOBAL)
  set_target_properties(environmental-grib PROPERTIES
    IMPORTED_LOCATION "${XGRIB_EXTERNAL_GENERATOR}")
  message(STATUS
    "${CMLOC}Using external environmental GRIB generator: ${XGRIB_EXTERNAL_GENERATOR}")
else()

# Debian 12's ecCodes package expands its multiarch pkg-config prefix to
# /usr/lib instead of /usr. CMake rejects the resulting /usr/lib/include path
# before compiling, although the headers are correctly installed in
# /usr/include. Keep the workaround in the parent packaging project so the
# generator submodule stays an independently buildable upstream checkout.
find_package(PkgConfig REQUIRED)
execute_process(
  COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=includedir eccodes
  OUTPUT_VARIABLE XGRIB_ECCODES_INCLUDEDIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE XGRIB_ECCODES_PKGCONFIG_RESULT)
if(XGRIB_ECCODES_PKGCONFIG_RESULT EQUAL 0
   AND NOT IS_DIRECTORY "${XGRIB_ECCODES_INCLUDEDIR}")
  execute_process(
    COMMAND ${PKG_CONFIG_EXECUTABLE} --variable=pcfiledir eccodes
    OUTPUT_VARIABLE XGRIB_ECCODES_PCDIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  file(READ "${XGRIB_ECCODES_PCDIR}/eccodes.pc" XGRIB_ECCODES_PC)
  string(REGEX REPLACE "prefix=[^\n]*" "prefix=/usr"
         XGRIB_ECCODES_PC "${XGRIB_ECCODES_PC}")
  set(XGRIB_PKGCONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/pkgconfig")
  file(MAKE_DIRECTORY "${XGRIB_PKGCONFIG_DIR}")
  file(WRITE "${XGRIB_PKGCONFIG_DIR}/eccodes.pc" "${XGRIB_ECCODES_PC}")
  set(ENV{PKG_CONFIG_PATH}
      "${XGRIB_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}")
  unset(__pkg_config_checked_ECCODES CACHE)
  message(STATUS "${CMLOC}Corrected Debian ecCodes pkg-config prefix")
endif()

# Ubuntu 22.04 ships ecCodes 2.24, whose public constructor still uses the
# legacy grib_ prefix.  Keep the generator source portable without carrying a
# distro-specific patch in the pinned submodule.
pkg_check_modules(XGRIB_ECCODES REQUIRED eccodes)
include(CheckCXXSymbolExists)
set(CMAKE_REQUIRED_INCLUDES "${XGRIB_ECCODES_INCLUDE_DIRS}")
set(CMAKE_REQUIRED_LIBRARIES "${XGRIB_ECCODES_LINK_LIBRARIES}")
check_cxx_symbol_exists(codes_handle_new_from_samples "eccodes.h"
                        XGRIB_HAVE_CODES_HANDLE_NEW_FROM_SAMPLES)
unset(CMAKE_REQUIRED_INCLUDES)
unset(CMAKE_REQUIRED_LIBRARIES)

add_subdirectory(generator EXCLUDE_FROM_ALL)
if(NOT XGRIB_HAVE_CODES_HANDLE_NEW_FROM_SAMPLES)
  target_compile_definitions(environmental_grib_engine PRIVATE
    codes_handle_new_from_samples=grib_handle_new_from_samples)
  message(STATUS "${CMLOC}Using legacy ecCodes sample constructor")
endif()
endif()

if(NOT XGRIB_EXTERNAL_GENERATOR_DIR)
  add_dependencies(${PACKAGE_NAME} environmental-grib)
endif()
if(BUILD_TESTING AND TARGET environmental_grib_tests)
  add_dependencies(${PACKAGE_NAME} environmental_grib_tests)
endif()
if(BUILD_TESTING AND TARGET environmental_grib_xtd_tests)
  add_dependencies(${PACKAGE_NAME} environmental_grib_xtd_tests)
endif()
if(BUILD_TESTING AND TARGET environmental_grib_merge_tests)
  add_dependencies(${PACKAGE_NAME} environmental_grib_merge_tests)
endif()

if(UNIX AND NOT APPLE)
  set_target_properties(environmental-grib PROPERTIES
    OUTPUT_NAME "environmental-grib.bin"
  )
  install(TARGETS environmental-grib RUNTIME DESTINATION
          "share/opencpn/plugins/${PACKAGE_NAME}/libexec")
  install(PROGRAMS scripts/environmental-grib
          DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/bin")

  if(BUNDLE_GENERATOR_RUNTIME)
    find_path(ECCODES_DEFINITIONS_DIR NAMES grib1/boot.def
      PATHS /usr/share/eccodes/definitions /usr/local/share/eccodes/definitions
            ${CMAKE_INSTALL_PREFIX}/share/eccodes/definitions)
    find_path(ECCODES_SAMPLES_DIR NAMES regular_ll_sfc_grib1.tmpl
      PATHS /usr/share/eccodes/samples /usr/local/share/eccodes/samples
            ${CMAKE_INSTALL_PREFIX}/share/eccodes/samples)
    find_path(PROJ_DATA_DIR NAMES proj.db
      PATHS /usr/share/proj /usr/local/share/proj
            ${CMAKE_INSTALL_PREFIX}/share/proj)
    if(NOT IS_DIRECTORY "${ECCODES_DEFINITIONS_DIR}" OR
       NOT IS_DIRECTORY "${ECCODES_SAMPLES_DIR}" OR
       NOT IS_DIRECTORY "${PROJ_DATA_DIR}")
      message(FATAL_ERROR "ecCodes or PROJ runtime data was not found")
    endif()

    install(DIRECTORY "${ECCODES_DEFINITIONS_DIR}"
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/share/eccodes")
    install(DIRECTORY "${ECCODES_SAMPLES_DIR}"
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/share/eccodes")
    install(DIRECTORY "${PROJ_DATA_DIR}"
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/share")
    install(FILES generator/LICENSE data/THIRD_PARTY_NOTICES.md
      DESTINATION "share/opencpn/plugins/${PACKAGE_NAME}/runtime/licenses")

    configure_file(cmake/BundleGeneratorRuntime.cmake.in
      "${CMAKE_CURRENT_BINARY_DIR}/BundleGeneratorRuntime.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleGeneratorRuntime.cmake")
  endif()
elseif(WIN32)
  if(XGRIB_EXTERNAL_GENERATOR_DIR)
    install(DIRECTORY "${XGRIB_EXTERNAL_GENERATOR_DIR}/bin/"
      DESTINATION "plugins/${PACKAGE_NAME}/bin")
    if(IS_DIRECTORY "${XGRIB_EXTERNAL_GENERATOR_DIR}/runtime")
      install(DIRECTORY "${XGRIB_EXTERNAL_GENERATOR_DIR}/runtime/"
        DESTINATION "plugins/${PACKAGE_NAME}/runtime")
    endif()
  elseif(BUNDLE_GENERATOR_RUNTIME AND CMAKE_VERSION VERSION_GREATER_EQUAL 3.21)
    install(TARGETS environmental-grib
      RUNTIME_DEPENDENCIES
        DIRECTORIES
          "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
          "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
        PRE_EXCLUDE_REGEXES
          "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES
          ".*[\\/]Windows[\\/]System32[\\/].*"
      RUNTIME DESTINATION "plugins/${PACKAGE_NAME}/bin")
  else()
    install(TARGETS environmental-grib RUNTIME DESTINATION
            "plugins/${PACKAGE_NAME}/bin")
  endif()
  if(BUNDLE_GENERATOR_RUNTIME AND
     IS_DIRECTORY
       "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    install(DIRECTORY
      "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/"
      DESTINATION "plugins"
      FILES_MATCHING PATTERN "*.dll")
  endif()
elseif(APPLE)
  install(TARGETS environmental-grib RUNTIME DESTINATION
          "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}/bin")
  if(BUNDLE_GENERATOR_RUNTIME)
    configure_file(cmake/BundleMacGeneratorRuntime.cmake.in
      "${CMAKE_CURRENT_BINARY_DIR}/BundleMacGeneratorRuntime.cmake" @ONLY)
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/BundleMacGeneratorRuntime.cmake")
  endif()
endif()
endif()

# ==============================================================

if (NOT OCPN_FLATPAK_CONFIG)
  # Build environment not available when flatpak is being configured so
  # following statements will not work
  message(STATUS "${CMLOC}Adding target link libraries to ${PACKAGE_NAME}")

  if (WIN32)
    add_subdirectory(opencpn-libs/WindowsHeaders)
    find_package(GLEW REQUIRED)
    target_link_libraries(${PACKAGE_NAME} windows::headers GLEW::GLEW glu32)
  endif (WIN32)

  add_subdirectory(opencpn-libs/api-${OCPN_API_VERSION_MINOR})
  target_link_libraries(${PACKAGE_NAME} ocpn::api)

  add_subdirectory(opencpn-libs/wxJSON)
  target_link_libraries(${PACKAGE_NAME} ocpn::wxjson)

  if(XGRIB_USE_BUNDLED_JASPER)
    include(BundledJasper)
  else()
    find_package(Jasper REQUIRED)
    set(XGRIB_JASPER_TARGET Jasper::Jasper)
  endif()
  find_package(BZip2 REQUIRED)
  find_package(ZLIB REQUIRED)
  target_link_libraries(${PACKAGE_NAME} ${XGRIB_JASPER_TARGET} BZip2::BZip2
                        ZLIB::ZLIB)

  if(XGRIB_USE_BUNDLED_JASPER AND EXISTS "${XGRIB_JASPER_LICENSE}")
    install(FILES "${XGRIB_JASPER_LICENSE}"
      DESTINATION
        "share/opencpn/plugins/${PACKAGE_NAME}/runtime/licenses/jasper"
      RENAME LICENSE.txt)
  endif()

  if(BUILD_TESTING)
    add_executable(xgrib_generator_job_json_tests
      test/GeneratorJobJsonTests.cpp src/GeneratorJobJson.cpp)
    target_include_directories(xgrib_generator_job_json_tests PRIVATE
      ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(xgrib_generator_job_json_tests PRIVATE
      ocpn::wxjson ${wxWidgets_LIBRARIES})
    add_test(NAME xgrib_generator_job_json_tests
             COMMAND xgrib_generator_job_json_tests)

    add_executable(xgrib_plugin_policy_tests
      test/XgribPluginPolicyTests.cpp src/XgribPluginPolicy.cpp)
    target_include_directories(xgrib_plugin_policy_tests PRIVATE
      ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(xgrib_plugin_policy_tests PRIVATE
      ${wxWidgets_LIBRARIES})
    add_test(NAME xgrib_plugin_policy_tests COMMAND xgrib_plugin_policy_tests)

    add_test(NAME xgrib_environmental_process_output_contract
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/EnvironmentalGribDialog.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/EnvironmentalGribDialogContract.cmake)

    add_test(NAME xgrib_selected_file_path_contract
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/src/GribUIDialog.cpp
        -DENVIRONMENTAL_DIALOG_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/EnvironmentalGribDialog.cpp
        -DPOTFILES_FILE=${CMAKE_CURRENT_SOURCE_DIR}/po/POTFILES.in
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/SelectedFilePathContract.cmake)

    add_test(NAME xgrib_plugin_version_contract
      COMMAND ${CMAKE_COMMAND}
        -DPLUGIN_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/src/grib_pi.h
        -DGENERATED_VERSION_HEADER=${CMAKE_BINARY_DIR}/CMakeFiles/include/version.h
        -DEXPECTED_MAJOR=${VERSION_MAJOR}
        -DEXPECTED_MINOR=${VERSION_MINOR}
        -DEXPECTED_PATCH=${VERSION_PATCH}
        -DEXPECTED_TWEAK=${VERSION_TWEAK}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/PluginVersionContract.cmake)

    add_test(NAME xgrib_windows_split_abi_contract
      COMMAND ${CMAKE_COMMAND}
        -DCMAKE_SOURCE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt
        -DPLUGIN_CONFIGURE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/PluginConfigure.cmake
        -DGENERATOR_CMAKE_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/generator/CMakeLists.txt
        -DWINDOWS_UTF8_MANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/generator/windows-utf8.manifest
        -DWINDOWS_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/ci/circleci-build-windows.ps1
        -DDIALOG_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/src/EnvironmentalGribDialog.cpp
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/WindowsSplitAbiContract.cmake)

    add_test(NAME xgrib_windows_runtime_contract
      COMMAND ${CMAKE_COMMAND}
        -DRUNTIME_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/ci/test-windows-opencpn-runtime.ps1
        -DRUNTIME_JOB_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/ci/circleci-test-windows-opencpn.ps1
        -DDIAGNOSTIC_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/ci/diagnose-windows-generator.ps1
        -DCIRCLE_CONFIG=${CMAKE_CURRENT_SOURCE_DIR}/.circleci/config.yml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/test/WindowsRuntimeContract.cmake)

    add_executable(xgrib_reader_integration_tests
      test/XgribReaderIntegrationTests.cpp
      src/GribVectorPolicy.cpp
      src/GribReader.cpp src/GribRecord.cpp src/GribV1Record.cpp
      src/GribV2Record.cpp src/zuFile.cpp)
    target_include_directories(xgrib_reader_integration_tests PRIVATE
      ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(xgrib_reader_integration_tests PRIVATE
      ${XGRIB_JASPER_TARGET} BZip2::BZip2 ZLIB::ZLIB
      ${wxWidgets_LIBRARIES})

    set(XGRIB_TEST_GRIB "${CMAKE_CURRENT_BINARY_DIR}/xgrib-test-current.grb")
    add_test(NAME xgrib_generate_reader_fixture
      COMMAND $<TARGET_FILE:environmental-grib> generate
        --bbox -6.3 53.0 -5.9 53.4
        --start 2026-07-12T00:00:00Z --hours 6 --step-hours 3
        --grid-spacing-deg 0.1 --source synthetic
        --output ${XGRIB_TEST_GRIB} --overwrite)
    set_tests_properties(xgrib_generate_reader_fixture PROPERTIES
      FIXTURES_SETUP xgrib_reader_fixture)
    add_test(NAME xgrib_reader_integration_tests
      COMMAND xgrib_reader_integration_tests ${XGRIB_TEST_GRIB})
    set_tests_properties(xgrib_reader_integration_tests PROPERTIES
      FIXTURES_REQUIRED xgrib_reader_fixture)

    if(XGRIB_EXTERNAL_GENERATOR_DIR)
      set(XGRIB_COMBINED_TEST_GRIB
        "${CMAKE_BINARY_DIR}/combined-known.grb2")
      add_test(NAME environmental_grib_merge_tests
        COMMAND $<TARGET_FILE:environmental-grib>
          merge-environment-gribs
          --weather ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/wind-known.grb2
          --current ${CMAKE_CURRENT_SOURCE_DIR}/test/fixtures/current-differing.grb
          --output ${XGRIB_COMBINED_TEST_GRIB} --overwrite)
      set_tests_properties(environmental_grib_merge_tests PROPERTIES
        FIXTURES_SETUP environmental_grib_combined_fixture)
    else()
      set(XGRIB_COMBINED_TEST_GRIB
        "${CMAKE_BINARY_DIR}/functional-fixtures/combined-known.grb2")
    endif()
    add_test(NAME xgrib_combined_reader_integration_tests
      COMMAND xgrib_reader_integration_tests ${XGRIB_COMBINED_TEST_GRIB}
        --combined)
    set_tests_properties(xgrib_combined_reader_integration_tests PROPERTIES
      FIXTURES_REQUIRED environmental_grib_combined_fixture)
  endif()

endif (NOT OCPN_FLATPAK_CONFIG)

add_definitions(-DTIXML_USE_STL)

#
# ----- If using JSON validation section below is needed
#

if (UNIX
    AND NOT APPLE
    AND NOT QT_ANDRIOD
    AND NOT OCPN_FLATPAK_CONFIG
    AND NOT OCPN_FLATPAK_BUILD
)
  if (OD_JSON_SCHEMA_VALIDATOR)
    # Change OS/ABI to be UNIX - System V to make it run with OCPN
    set(lib_name lib${PACKAGE_NAME}.so)
    message(STATUS "${CMLOC}lib-name: ${lib_name}")
    find_file(elfedit elfedit)
    if (NOT (${elfedit} STREQUAL "elfedit-NOTFOUND"))
      message(STATUS "${CMLOC}Will ensure OS/ABI is set to 0 to match OCPN")
      add_custom_command(
        TARGET ${PACKAGE_NAME}
        POST_BUILD
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT " Running post build action on ${lib_name}."
        COMMAND
          sh -c
          'elfedit --input-osabi=Linux --output-osabi=none ${lib_name} || (echo 0) '
      )
    else (NOT (${elfedit} STREQUAL "elfedit-NOTFOUND"))
      message(
        STATUS
          "${CMLOC}Cannot correct OS/ABI to match OCPN if it is generated incorrectly. Package binutils needed"
      )
    endif (NOT (${elfedit} STREQUAL "elfedit-NOTFOUND"))
  endif (OD_JSON_SCHEMA_VALIDATOR)
endif (
  UNIX
  AND NOT APPLE
  AND NOT QT_ANDRIOD
  AND NOT OCPN_FLATPAK_CONFIG
  AND NOT OCPN_FLATPAK_BUILD
)

# ------- Change below to match project requirements

# Needed for android builds
if (QT_ANDROID)
  include_directories(BEFORE ${qt_android_include})
endif (QT_ANDROID)

# Needed for all builds Do not change - needed to build app
# ===============================================================

include("PluginInstall")
include("PluginLocalization")
include("PluginPackage")

# ================================================================
# ----- Do not change section above - needed to configure build process

message(STATUS "${CMLOC}include directories: ")
get_property(
  dirs
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  PROPERTY INCLUDE_DIRECTORIES
)
foreach (dir ${dirs})
  message(STATUS "${CMLOC}dir='${dir}'")
endforeach ()
