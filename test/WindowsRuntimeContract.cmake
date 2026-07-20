file(READ "${RUNTIME_SCRIPT}" runtime_script)
file(READ "${DIAGNOSTIC_SCRIPT}" diagnostic_script)
file(READ "${CIRCLE_CONFIG}" circle_config)

set(runtime_patterns
  "opencpn_5\\.14\\.0-0\\+4418\\.91f3b67_setup\\.exe"
  "f049075bd3411dc3d5ba2954229ecebf8510abd36529fd27d961d37f275c1076"
  "PackageArchive"
  "tar\\.exe -xf"
  "XGRIB_TEST_OPEN_GENERATOR"
  "XGRIB_TEST_WEATHER_FILE"
  "XGRIB_TEST_CURRENT_FILE"
  "Register-WmiEvent -Class Win32_ProcessStartTrace"
  "environmental-grib\\.exe"
  "Generate Complete GRIB"
  "xGRIB: opened generated GRIB:"
  "runtime-combined-inspection\\.json"
  "01-opencpn-running\\.png"
  "04-merge-success\\.png")
foreach(pattern IN LISTS runtime_patterns)
  if(NOT runtime_script MATCHES "${pattern}")
    message(FATAL_ERROR "Windows OpenCPN runtime contract is missing: ${pattern}")
  endif()
endforeach()

set(circle_patterns
  "windows-focused-validation"
  "windows-focused:"
  "only: windows-focused-validation"
  "windows-generator-diagnostics:"
  "XGRIB_WINDOWS_DIAGNOSTICS_ONLY")
foreach(pattern IN LISTS circle_patterns)
  if(NOT circle_config MATCHES "${pattern}")
    message(FATAL_ERROR "Focused Windows workflow contract is missing: ${pattern}")
  endif()
endforeach()

set(diagnostic_patterns
  "OptionId.WindowsDesktopDebuggers"
  "Get-AuthenticodeSignature"
  "download\\.sysinternals\\.com/files/Procdump\\.zip"
  "procdump64\\.exe"
  "-ma -e -x"
  "!analyze -v"
  "\\.ecxr"
  "kv 100"
  "environmental_grib_tests"
  "environmental_grib_xtd_tests"
  "diagnostic-result\\.json")
foreach(pattern IN LISTS diagnostic_patterns)
  if(NOT diagnostic_script MATCHES "${pattern}")
    message(FATAL_ERROR "Windows CDB diagnostic contract is missing: ${pattern}")
  endif()
endforeach()
