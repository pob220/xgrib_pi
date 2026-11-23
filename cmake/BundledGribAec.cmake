# SPDX-License-Identifier: GPL-3.0-or-later
set(_grib_aec_source "${CMAKE_CURRENT_LIST_DIR}/../third_party/libaec-decoder")
add_library(grib_aec_decoder STATIC
  "${_grib_aec_source}/decode.c"
  "${_grib_aec_source}/vector.c")
target_include_directories(grib_aec_decoder PUBLIC "${_grib_aec_source}")
set_target_properties(grib_aec_decoder PROPERTIES
  C_STANDARD 99
  C_VISIBILITY_PRESET hidden
  POSITION_INDEPENDENT_CODE ON)

if(QT_ANDROID OR ANDROID)
  set(_grib_aec_licenses "${PREFIX_PARENTDATA}/plugins/${PACKAGE_NAME}/data/android-licenses")
elseif(WIN32)
  set(_grib_aec_licenses "plugins/${PACKAGE_NAME}/runtime/licenses")
elseif(APPLE)
  set(_grib_aec_licenses "OpenCPN.app/Contents/SharedSupport/plugins/${PACKAGE_NAME}/runtime/licenses")
else()
  set(_grib_aec_licenses "share/opencpn/plugins/${PACKAGE_NAME}/runtime/licenses")
endif()
install(FILES "${_grib_aec_source}/LICENSE.txt"
  DESTINATION "${_grib_aec_licenses}/libaec")
