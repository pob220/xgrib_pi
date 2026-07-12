#include "XgribPluginPolicy.h"

#include <array>

#include <wx/config.h>
#include <wx/string.h>

bool IsBundledGribPluginEnabled(wxConfigBase* config) {
  if (!config) return false;

  static const std::array<wxString, 3> pluginGroups = {
      "/PlugIns/libgrib_pi.so", "/PlugIns/libgrib_pi.dylib",
      "/PlugIns/grib_pi.dll"};

  const wxString originalPath = config->GetPath();
  bool enabled = false;
  for (const wxString& group : pluginGroups) {
    if (!config->HasGroup(group)) continue;
    config->SetPath(group);
    if (config->ReadBool("bEnabled", false)) {
      enabled = true;
      break;
    }
  }
  config->SetPath(originalPath);
  return enabled;
}
