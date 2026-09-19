file(READ "${SOURCE_DIR}/.circleci/config.yml" config)
file(READ "${SOURCE_DIR}/ci/circleci-build-windows.ps1" windows)
file(READ "${SOURCE_DIR}/CMakeLists.txt" project)
file(READ "${SOURCE_DIR}/cmake/PluginSetup.cmake" setup)
file(READ "${SOURCE_DIR}/ci/build-android-arm64.sh" android)
file(READ "${SOURCE_DIR}/ci/windows64-sdk.json" sdk_pin)
string(JSON sdk_archive GET "${sdk_pin}" archive)
string(JSON sdk_hash GET "${sdk_pin}" sha256)
file(SHA256 "${SOURCE_DIR}/${sdk_archive}" actual_hash)
if(NOT actual_hash STREQUAL sdk_hash)
  message(FATAL_ERROR "Native x64 host SDK archive checksum mismatch")
endif()

foreach(platform IN ITEMS windows-x86 windows-x64 android-arm64)
  string(REGEX MATCHALL "- ${platform}:" jobs "${config}")
  list(LENGTH jobs count)
  if(count LESS 2 OR NOT config MATCHES "name: release-${platform}")
    message(FATAL_ERROR "${platform} must run in both validation and release")
  endif()
endforeach()
foreach(platform IN ITEMS windows-x64 android-arm64)
  if(NOT config MATCHES "- release-${platform}[\r\n]")
    message(FATAL_ERROR "Alpha approval must depend on ${platform}")
  endif()
endforeach()
foreach(text IN ITEMS
    "-PluginArchitecture x64" "paths: [artifacts/android-arm64]"
    "paths: [artifacts/windows-x64]" "default: false" "type: approval")
  string(FIND "${config}" "${text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing CI platform/publication guard: ${text}")
  endif()
endforeach()
foreach(text IN ITEMS
    "[ValidateSet(\"x86\", \"x64\")][string] $PluginArchitecture = \"x86\""
    "Native host import library is not exclusively AMD64"
    "Native OpenCPN SDK archive checksum mismatch"
    "Native OpenCPN SDK file checksum mismatch"
    "lib\\vc14x_x64_dll" "-A $pluginPlatform"
    "msvc-wx32-x64" "$pluginHeaders -match $pluginMachine")
  string(FIND "${windows}" "${text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing Windows architecture guard: ${text}")
  endif()
endforeach()
if(NOT project MATCHES "XGRIB_OPENCPN_IMPORT_LIBRARY" OR
   NOT setup MATCHES "msvc-wx32-x64")
  message(FATAL_ERROR "x64 must use a native SDK and separate package identity")
endif()
string(FIND "${android}" "\"$artifacts/manual-import\"" manual_import)
if(manual_import EQUAL -1)
  message(FATAL_ERROR "Android manual import must be separate from catalogue packages")
endif()
