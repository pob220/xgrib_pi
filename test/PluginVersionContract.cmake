file(READ "${PLUGIN_HEADER}" plugin_header)

if(NOT plugin_header MATCHES "#include[ \t]+\"version\\.h\"")
  message(FATAL_ERROR
    "The plugin interface must use the generated package version header")
endif()

if(plugin_header MATCHES
    "#[ \t]*define[ \t]+PLUGIN_VERSION_(MAJOR|MINOR|PATCH|TWEAK)")
  message(FATAL_ERROR
    "The plugin interface must not override the generated package version")
endif()

file(READ "${GENERATED_VERSION_HEADER}" generated_version)
foreach(component IN ITEMS MAJOR MINOR PATCH TWEAK)
  if(NOT generated_version MATCHES
      "#[ \t]*define[ \t]+PLUGIN_VERSION_${component}[ \t]+${EXPECTED_${component}}([^0-9]|$)")
    message(FATAL_ERROR
      "Generated plugin ${component} version does not match the package version")
  endif()
endforeach()

file(READ "${PROTOCOL_HEADER}" protocol_header)
if(NOT protocol_header MATCHES
    "kGribProtocolVersionMajor[ \t]*=[ \t]*5[ \t]*;")
  message(FATAL_ERROR "xGRIB must advertise GRIB timeline protocol major 5")
endif()
if(NOT protocol_header MATCHES
    "kGribProtocolVersionMinor[ \t]*=[ \t]*0[ \t]*;")
  message(FATAL_ERROR "xGRIB must advertise GRIB timeline protocol minor 0")
endif()

file(READ "${PLUGIN_SOURCE}" plugin_source)
if(NOT plugin_source MATCHES
    "GribVersionMajor[^\n]*kGribProtocolVersionMajor")
  message(FATAL_ERROR
    "Timeline records must use the GRIB protocol major, not package version")
endif()
if(NOT plugin_source MATCHES
    "GribVersionMinor[^\n]*kGribProtocolVersionMinor")
  message(FATAL_ERROR
    "Timeline records must use the GRIB protocol minor, not package version")
endif()
