file(READ "${PLUGIN_SETUP}" plugin_setup)
file(READ "${FLATPAK_ARCHIVE_TEST}" flatpak_archive_test)
file(READ "${DEPLOY_SCRIPT}" deploy_script)
file(READ "${WINDOWS_BUILD_SCRIPT}" windows_build_script)

if(plugin_setup MATCHES
    "OCPN_FLATPAK_CONFIG OR OCPN_FLATPAK_BUILD[^\n]*\n([^\n]*\n){0,12}[^\n]*set\\(PKG_TARGET_WX_VER")
  message(FATAL_ERROR
    "Flatpak metadata must not encode the wx version in its target")
endif()

foreach(target IN ITEMS flatpak-x86_64 flatpak-aarch64)
  if(NOT flatpak_archive_test MATCHES "${target}")
    message(FATAL_ERROR
      "Flatpak archive validation is missing catalogue target ${target}")
  endif()
endforeach()

foreach(pattern IN ITEMS
    "XGRIB_DEPLOY_ARTIFACT_ROOT"
    "plugin_version=.*<version>"
    "target_version=.*<target-version>"
    "package_name=.*plugin_version.*target.*target_version.*tarball"
    "metadata_name=.*plugin_version.*target.*target_version.*metadata")
  if(NOT deploy_script MATCHES "${pattern}")
    message(FATAL_ERROR "Alpha deployment contract is missing: ${pattern}")
  endif()
endforeach()

if(deploy_script MATCHES "xgrib_pi-[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+")
  message(FATAL_ERROR
    "Alpha deployment must derive the release version from package metadata")
endif()

if(NOT windows_build_script MATCHES
    "SelectSingleNode\\('/plugin/version'\\)")
  message(FATAL_ERROR
    "Windows packaging must select the child plugin version element explicitly")
endif()
if(windows_build_script MATCHES
    "metadataXml\\.plugin\\.version")
  message(FATAL_ERROR
    "PowerShell XML property access confuses the plugin version attribute and element")
endif()
