file(READ "${SOURCE_DIR}/.circleci/config.yml" config)
file(READ "${SOURCE_DIR}/ci/circleci-build-windows.ps1" windows)
file(READ "${SOURCE_DIR}/CMakeLists.txt" project)
file(READ "${SOURCE_DIR}/cmake/PluginSetup.cmake" setup)
file(READ "${SOURCE_DIR}/ci/build-android-arm64.sh" android)
file(READ "${SOURCE_DIR}/ci/windows64-sdk.json" sdk_pin)
string(JSON host_url GET "${sdk_pin}" host_url)
string(JSON host_hash GET "${sdk_pin}" host_sha256)
string(JSON host_version GET "${sdk_pin}" host_version)
string(LENGTH "${host_hash}" host_hash_length)
if(NOT host_version STREQUAL "5.14.2" OR
   NOT host_url MATCHES "opencpn_5\\.14\\.2.*setup_x64\\.exe$" OR
   NOT host_hash MATCHES "^[0-9a-f]+$" OR
   NOT host_hash_length EQUAL 64)
  message(FATAL_ERROR "Native x64 host pin is invalid")
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
foreach(platform IN ITEMS
    debian13-x86_64 debian12-x86_64 ubuntu22.04-x86_64
    ubuntu24.04-x86_64 debian12-arm64 flatpak25.08-x86_64
    flatpak25.08-arm64 windows-x86 windows-x64 macos-arm64 android-arm64)
  if(NOT config MATCHES "name: release-${platform}-api122" OR
     NOT config MATCHES "- release-${platform}-api122[\r\n]")
    message(FATAL_ERROR "API 1.22 ${platform} build must gate alpha approval")
  endif()
endforeach()
foreach(text IN ITEMS
    "-PluginArchitecture x64" "paths: [artifacts/android-arm64]"
    "paths: [artifacts/windows-x64]" "type: approval")
  string(FIND "${config}" "${text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing CI platform/publication guard: ${text}")
  endif()
endforeach()
if(NOT config MATCHES
   "run_workflow_deploy:[ \t\r\n]+type: boolean[ \t\r\n]+default: (false|true)")
  message(FATAL_ERROR
    "Deployment parameter must have an explicit Boolean default")
endif()
foreach(text IN ITEMS
    "[ValidateSet(\"x86\", \"x64\")][string] $PluginArchitecture = \"x86\""
    "Native host import library is not exclusively AMD64"
    "Pinned OpenCPN x64 installer checksum mismatch"
    "Checking xGRIB imports against upstream OpenCPN x64 host"
    "lib\\vc14x_x64_dll" "-A $pluginPlatform"
    "msvc-64" "$pluginHeaders -match $pluginMachine")
  string(FIND "${windows}" "${text}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Missing Windows architecture guard: ${text}")
  endif()
endforeach()
if(NOT project MATCHES "XGRIB_OPENCPN_IMPORT_LIBRARY" OR
   NOT setup MATCHES "msvc-64")
  message(FATAL_ERROR "x64 must use native host imports and a separate package identity")
endif()
string(FIND "${android}" "\"$artifacts/manual-import\"" manual_import)
if(manual_import EQUAL -1)
  message(FATAL_ERROR "Android manual import must be separate from catalogue packages")
endif()
