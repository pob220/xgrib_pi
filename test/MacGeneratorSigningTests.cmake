# Native regression: reproduce a relocated dependency with an invalid signature,
# then run the production installer and execute the repaired helper.
if(NOT CMAKE_HOST_APPLE)
  message(FATAL_ERROR "MacGeneratorSigningTests requires macOS")
endif()
find_program(CLANG clang REQUIRED)
find_program(CODESIGN codesign REQUIRED)
set(_root "${TEST_BINARY_DIR}/mac generator signing")
file(REMOVE_RECURSE "${_root}")
file(MAKE_DIRECTORY "${_root}/build")
set(CMAKE_INSTALL_PREFIX "${_root}/stage")
set(PACKAGE_NAME "xgrib_pi")
set(_bin "${CMAKE_INSTALL_PREFIX}/OpenCPN.app/Contents/SharedSupport/plugins/xgrib_pi/bin")
file(MAKE_DIRECTORY "${_bin}")
file(WRITE "${_root}/library.c" "int answer(void) { return 42; }\n")
file(WRITE "${_root}/main.c"
  "extern int answer(void); int main(void) { return answer() != 42; }\n")

function(run_checked)
  execute_process(COMMAND ${ARGV} RESULT_VARIABLE _status
    OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
  if(NOT "${_status}" STREQUAL "0")
    message(FATAL_ERROR "Command failed: ${ARGV}\n${_out}${_err}")
  endif()
endfunction()

set(_original "${_root}/build/libanswer.dylib")
run_checked("${CLANG}" -dynamiclib "${_root}/library.c"
  -Wl,-headerpad_max_install_names -install_name "${_original}" -o "${_original}")
run_checked("${CLANG}" "${_root}/main.c" "${_original}"
  -Wl,-headerpad_max_install_names -o "${_bin}/environmental-grib")
run_checked("${CODESIGN}" --force --sign - "${_original}")

include(BundleUtilities)
fixup_bundle("${_bin}/environmental-grib" "" "${_root}/build")
set(_bundled "${_bin}/../Frameworks/libanswer.dylib")
if(NOT EXISTS "${_bundled}")
  message(FATAL_ERROR "Fixture dependency was not bundled")
endif()
execute_process(COMMAND "${CODESIGN}" --verify --strict "${_bundled}"
  RESULT_VARIABLE _before OUTPUT_QUIET ERROR_QUIET)
if("${_before}" STREQUAL "0")
  message(FATAL_ERROR "Regression fixture did not reproduce the stale signature")
endif()

# Exercise exactly the script used by cmake --install and CPack, rather than
# reimplementing its signing commands in the test.
set(CMAKE_CURRENT_SOURCE_DIR "${SOURCE_DIR}")
set(CMAKE_PREFIX_PATH "${_root}/build")
configure_file("${SOURCE_DIR}/cmake/BundleMacGeneratorRuntime.cmake.in"
  "${_root}/install.cmake" @ONLY)
include("${_root}/install.cmake")
run_checked("${CODESIGN}" --verify --strict "${_bundled}")
run_checked("${CODESIGN}" --verify --strict "${_bin}/environmental-grib")

# Remove the original dependency and relocate the entire installed tree so
# the helper can succeed only with its repaired bundled copy.
file(REMOVE "${_original}")
file(RENAME "${CMAKE_INSTALL_PREFIX}" "${_root}/relocated installation")
run_checked("${_root}/relocated installation/OpenCPN.app/Contents/SharedSupport/plugins/xgrib_pi/bin/environmental-grib")
message(STATUS "Relocated macOS generator signature regression passed")
