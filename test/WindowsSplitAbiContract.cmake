file(READ "${CMAKE_SOURCE_FILE}" cmake_source)
file(READ "${WINDOWS_SCRIPT}" windows_script)
file(READ "${DIALOG_SOURCE}" dialog_source)

set(cmake_patterns
  "XGRIB_EXTERNAL_GENERATOR_DIR"
  "add_executable\\(environmental-grib IMPORTED GLOBAL\\)"
  "IMPORTED_LOCATION \"\\$\\{XGRIB_EXTERNAL_GENERATOR\\}\""
  "install\\(DIRECTORY \"\\$\\{XGRIB_EXTERNAL_GENERATOR_DIR\\}/bin/\"")
foreach(pattern IN LISTS cmake_patterns)
  if(NOT cmake_source MATCHES "${pattern}")
    message(FATAL_ERROR "Windows split-ABI CMake contract is missing: ${pattern}")
  endif()
endforeach()

set(script_patterns
  "generatorTriplet = \"x64-windows-release\""
  "pluginTriplet = \"x86-windows-release\""
  "-A x64"
  "-A Win32"
  "-DXGRIB_EXTERNAL_GENERATOR_DIR=\\$generatorStage"
  "helper_architecture = \"x86_64\"")
foreach(pattern IN LISTS script_patterns)
  if(NOT windows_script MATCHES "${pattern}")
    message(FATAL_ERROR "Windows split-ABI CI contract is missing: ${pattern}")
  endif()
endforeach()

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
