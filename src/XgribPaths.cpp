#include "XgribPaths.h"

#include <wx/filename.h>
#include <wx/utils.h>

#include "ocpn_plugin.h"

wxString GetXgribDataDirectory() {
  wxString smokeTestEnabled;
  wxString smokeTestDataDirectory;
  if (wxGetEnv("XGRIB_TEST_OPEN_GENERATOR", &smokeTestEnabled) &&
      smokeTestEnabled == "1" &&
      wxGetEnv("XGRIB_TEST_PLUGIN_DATA_DIR", &smokeTestDataDirectory) &&
      !smokeTestDataDirectory.empty()) {
    wxFileName directory(smokeTestDataDirectory, "");
    directory.AppendDir("data");
    return directory.GetPathWithSep();
  }
  wxFileName directory(GetPluginDataDir("xgrib_pi"), "");
  directory.AppendDir("data");
  return directory.GetPathWithSep();
}
