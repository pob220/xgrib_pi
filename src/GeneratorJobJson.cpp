#include "GeneratorJobJson.h"

#include <wx/string.h>

wxJSONValue CreateGeneratorJobEnvelope() {
  wxJSONValue root;
  root["schemaVersion"] = 1;
  root["operation"] = wxString("generateEnvironment");
  root["request"]["cycle"] = wxString("auto");
  root["credentials"]["copernicusPasswordEnvironment"] =
      wxString("ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD");
  return root;
}
