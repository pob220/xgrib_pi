# BundleUtilities rewrites Mach-O load commands, invalidating the signatures
# on copied Homebrew libraries. Sign the final bytes, dependencies first.
function(xgrib_sign_mac_generator_runtime helper)
  if(NOT EXISTS "${helper}")
    message(FATAL_ERROR "Installed Environmental GRIB helper not found: ${helper}")
  endif()
  find_program(_xgrib_codesign codesign REQUIRED)
  get_filename_component(_bin_dir "${helper}" DIRECTORY)
  file(GLOB_RECURSE _libraries "${_bin_dir}/../Frameworks/*.dylib")
  set(_sign_files "")
  foreach(_library IN LISTS _libraries)
    # Homebrew supplies both versioned libraries and symlink aliases. Sign
    # each physical file once without replacing the aliases.
    get_filename_component(_real_library "${_library}" REALPATH)
    list(APPEND _sign_files "${_real_library}")
  endforeach()
  list(REMOVE_DUPLICATES _sign_files)
  list(APPEND _sign_files "${helper}")
  foreach(_file IN LISTS _sign_files)
    execute_process(
      COMMAND "${_xgrib_codesign}" --force --sign - --timestamp=none "${_file}"
      RESULT_VARIABLE _status OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT "${_status}" STREQUAL "0")
      message(FATAL_ERROR "Cannot sign ${_file}: ${_output}${_error}")
    endif()
  endforeach()
  foreach(_file IN LISTS _sign_files)
    execute_process(
      COMMAND "${_xgrib_codesign}" --verify --strict --verbose=2 "${_file}"
      RESULT_VARIABLE _status OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(NOT "${_status}" STREQUAL "0")
      message(FATAL_ERROR "Invalid signature on ${_file}: ${_output}${_error}")
    endif()
  endforeach()
endfunction()
