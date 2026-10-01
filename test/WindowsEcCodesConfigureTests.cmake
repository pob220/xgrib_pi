# Execute the production generator-selection block in a minimal Windows
# configure. A deliberately unavailable pkg-config reproduces the local VS
# configure failure without requiring a Windows compiler or all dependencies.
file(READ "${SOURCE_FILE}" source)
string(FIND "${source}" "if(XGRIB_EXTERNAL_GENERATOR_DIR)" begin)
string(FIND "${source}"
  "if(NOT XGRIB_EXTERNAL_GENERATOR_DIR)\n  add_dependencies" end)
if(begin LESS 0 OR end LESS begin)
  message(FATAL_ERROR "Could not find the generator-selection CMake block")
endif()
math(EXPR length "${end} - ${begin}")
string(SUBSTRING "${source}" ${begin} ${length} selection)

set(root "${TEST_BINARY_DIR}/windows-eccodes-configure-test")
file(MAKE_DIRECTORY "${root}/generator" "${root}/modules" "${root}/eccodes")
file(WRITE "${root}/selection.cmake" "${selection}")
file(WRITE "${root}/modules/FindPkgConfig.cmake"
  "message(FATAL_ERROR \"Windows must not require pkg-config for ecCodes\")\n")
file(WRITE "${root}/eccodes/eccodes-config.cmake"
  "add_library(eccodes INTERFACE IMPORTED GLOBAL)\n")
file(WRITE "${root}/generator/CMakeLists.txt" [[
find_package(eccodes CONFIG REQUIRED)
add_library(environmental_grib_engine INTERFACE)
]])
file(WRITE "${root}/CMakeLists.txt" [[
cmake_minimum_required(VERSION 3.20)
project(windows_eccodes_configure NONE)
set(WIN32 TRUE)
set(XGRIB_EXTERNAL_GENERATOR_DIR "")
set(CMLOC "")
set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/modules")
set(eccodes_DIR "${CMAKE_CURRENT_SOURCE_DIR}/eccodes")
include("${CMAKE_CURRENT_SOURCE_DIR}/selection.cmake")
if(NOT TARGET eccodes OR NOT TARGET environmental_grib_engine)
  message(FATAL_ERROR "Windows must configure the generator with ecCodes")
endif()
]])
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${root}" -B "${root}/build"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Windows ecCodes configure failed:\n${output}\n${error}")
endif()
