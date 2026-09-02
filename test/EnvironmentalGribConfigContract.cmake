file(READ "${SOURCE_FILE}" source)

if(source MATCHES "wxConfigBase::Get\\(false\\)")
  message(FATAL_ERROR
    "Environmental GRIB settings must use OpenCPN's configuration service")
endif()

string(REGEX MATCHALL "GetOCPNConfigObject\\(\\)" host_config_calls "${source}")
list(LENGTH host_config_calls host_config_call_count)
if(host_config_call_count LESS 4)
  message(FATAL_ERROR
    "Environmental GRIB preset and generator settings must use the host configuration object")
endif()
