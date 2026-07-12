#include "XgribPaths.h"

#include <wx/filename.h>

#include "ocpn_plugin.h"

wxString GetXgribDataDirectory() {
  wxFileName directory(GetPluginDataDir("xgrib_pi"), "");
  directory.AppendDir("data");
  return directory.GetPathWithSep();
}
