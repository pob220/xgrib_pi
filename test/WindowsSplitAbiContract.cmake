file(READ "${CMAKE_SOURCE_FILE}" cmake_source)
file(READ "${PLUGIN_CONFIGURE_SOURCE}" plugin_configure_source)
file(READ "${GENERATOR_CMAKE_SOURCE}" generator_cmake_source)
file(READ "${WINDOWS_UTF8_MANIFEST}" windows_utf8_manifest)
file(READ "${WINDOWS_SCRIPT}" windows_script)
file(READ "${DIALOG_SOURCE}" dialog_source)

set(cmake_patterns
  "XGRIB_EXTERNAL_GENERATOR_DIR"
  "add_executable\\(environmental-grib IMPORTED GLOBAL\\)"
  "IMPORTED_LOCATION \"\\$\\{XGRIB_EXTERNAL_GENERATOR\\}\""
  "install\\(DIRECTORY \"\\$\\{XGRIB_EXTERNAL_GENERATOR_DIR\\}/bin/\""
  "\\$\\{VCPKG_INSTALLED_DIR\\}/\\$\\{VCPKG_TARGET_TRIPLET\\}/bin/"
  "FILES_MATCHING PATTERN \"\\*\\.dll\"")
foreach(pattern IN LISTS cmake_patterns)
  if(NOT cmake_source MATCHES "${pattern}")
    message(FATAL_ERROR "Windows split-ABI CMake contract is missing: ${pattern}")
  endif()
endforeach()

if(NOT plugin_configure_source MATCHES
   "target_compile_definitions\\(\\$\\{PACKAGE_NAME\\} PRIVATE MAKING_PLUGIN\\)")
  message(FATAL_ERROR
    "Windows plugin must import OpenCPN host symbols using MAKING_PLUGIN")
endif()

set(generator_cmake_patterns
  "environmental_grib_enable_windows_utf8"
  "/MANIFEST:EMBED"
  "/MANIFESTINPUT:"
  "environmental_grib_enable_windows_utf8\\(environmental-grib\\)"
  "environmental_grib_enable_windows_utf8\\(environmental_grib_tests\\)"
  "environmental_grib_enable_windows_utf8\\(environmental_grib_xtd_tests\\)"
  "environmental_grib_enable_windows_utf8\\(environmental_grib_merge_tests\\)")
foreach(pattern IN LISTS generator_cmake_patterns)
  if(NOT generator_cmake_source MATCHES "${pattern}")
    message(FATAL_ERROR "Windows UTF-8 generator contract is missing: ${pattern}")
  endif()
endforeach()
if(NOT windows_utf8_manifest MATCHES
   "<activeCodePage[^>]*>UTF-8</activeCodePage>")
  message(FATAL_ERROR "Windows generator manifest does not select UTF-8")
endif()

set(script_patterns
  "generatorTriplet = \"x64-windows-release\""
  "pluginTriplet = \"x86-windows-release\""
  "-A x64"
  "-A Win32"
  "-DXGRIB_EXTERNAL_GENERATOR_DIR=\\$generatorStage"
  "Assert-PowerShellSyntax"
  "Find-Dumpbin"
  "\\$dumpbin = Find-Dumpbin"
  "& \\$dumpbin /dependents"
  "\\$env:PATH = \"\\$wxLib;\\$env:PATH\""
  "plugin-test-dependencies.log"
  "--timeout 120"
  "helper_architecture = \"x86_64\"")
foreach(pattern IN LISTS script_patterns)
  if(NOT windows_script MATCHES "${pattern}")
    message(FATAL_ERROR "Windows split-ABI CI contract is missing: ${pattern}")
  endif()
endforeach()

string(FIND "${windows_script}"
  "$env:PROJ_DATA = Join-Path $generatorInstalled \"share\\proj\""
  proj_data_position)
string(FIND "${windows_script}"
  "ctest --test-dir $generatorBuild"
  generator_ctest_position)
if(proj_data_position EQUAL -1 OR generator_ctest_position EQUAL -1 OR
   NOT proj_data_position LESS generator_ctest_position)
  message(FATAL_ERROR
    "Windows generator CTest must receive PROJ_DATA before it starts")
endif()

set(dialog_patterns
  "environmental-grib\\.exe"
  "ECCODES_DEFINITION_PATH"
  "ECCODES_SAMPLES_PATH"
  "PROJ_DATA")
foreach(pattern IN LISTS dialog_patterns)
  if(NOT dialog_source MATCHES "${pattern}")
    message(FATAL_ERROR "Packaged Windows helper contract is missing: ${pattern}")
  endif()
endforeach()
