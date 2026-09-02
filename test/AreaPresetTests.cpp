#include <cstdlib>
#include <iostream>

#include <wx/fileconf.h>
#include <wx/filename.h>
#include <wx/init.h>

#include "AreaPreset.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

const xgrib::AreaPreset* FindById(const std::vector<xgrib::AreaPreset>& presets,
                                  const wxString& id) {
  for (const auto& preset : presets) {
    if (preset.id == id) return &preset;
  }
  return nullptr;
}

}  // namespace

int main() {
  wxInitializer initializer;
  Expect(initializer.IsOk(), "wxWidgets must initialize for config tests");

  auto supplied = xgrib::SuppliedAreaPresets();
  Expect(supplied.size() == 8, "eight supplied areas must remain available");
  Expect(xgrib::ValidateAreaPresetCollection(supplied).empty(),
         "supplied areas must validate");
  const auto* irish = FindById(supplied, "supplied-irish-sea");
  Expect(irish && irish->current_provider == "copernicus_nws",
         "Irish Sea must retain its initial current-provider recommendation");
  const auto* nordic = FindById(supplied, "supplied-nordic");
  Expect(nordic && nordic->weather_provider == "metno" &&
             nordic->current_provider == "none",
         "Nordic waters must retain its optional provider recommendations");

  xgrib::AreaPreset invalid{"bad", "Bad", 10.0, 0.0, -10.0, 5.0};
  Expect(!xgrib::ValidateAreaPreset(invalid).empty(),
         "west >= east must be rejected");
  invalid = {"bad", "Bad", -10.0, -95.0, 10.0, 5.0};
  Expect(!xgrib::ValidateAreaPreset(invalid).empty(),
         "out-of-range latitude must be rejected");
  invalid = {"bad", "Bad", -10.0, 0.0, 10.0, 5.0, "unknown", ""};
  Expect(!xgrib::ValidateAreaPreset(invalid).empty(),
         "unknown provider IDs must be rejected");

  auto duplicateNames = supplied;
  duplicateNames[1].name = duplicateNames[0].name.Lower();
  Expect(!xgrib::ValidateAreaPresetCollection(duplicateNames).empty(),
         "area names must be unique without regard to case");

  const wxString configFile =
      wxFileName::CreateTempFileName("xgrib-area-presets-");
  {
    wxFileConfig config("xgrib-area-tests", "", configFile, "",
                        wxCONFIG_USE_LOCAL_FILE);
    config.SetPath("/CallerPath");
    const wxString originalPath = config.GetPath();

    Expect(xgrib::LoadAreaPresets(&config).size() == supplied.size(),
           "missing stored data must load supplied defaults");
    Expect(config.GetPath() == originalPath,
           "loading presets must restore the caller's config path");

    config.SetPath("/PlugIns/xGRIB/EnvironmentalGenerator/AreaPresets");
    config.Write("schema_version", 1L);
    config.Write("count", 1L);
    config.SetPath("Preset0");
    config.Write("id", "incomplete");
    config.SetPath(originalPath);
    Expect(xgrib::LoadAreaPresets(&config).size() == supplied.size(),
           "incomplete stored data must safely fall back to supplied areas");
    Expect(config.GetPath() == originalPath,
           "failed loading must still restore the caller's config path");

    xgrib::AreaPreset custom{
        "user-1", "Local sailing area", -6.5, 52.0, -4.0, 54.5, "gfs", "auto"};
    supplied.push_back(custom);
    wxString error = "stale error";
    Expect(xgrib::SaveAreaPresets(&config, supplied, &error),
           "valid presets must save");
    Expect(error.empty(), "successful save must not report an error");
    Expect(config.GetPath() == originalPath,
           "saving presets must restore the caller's config path");
    const auto loaded = xgrib::LoadAreaPresets(&config);
    const auto* loadedCustom = FindById(loaded, "user-1");
    Expect(loadedCustom && loadedCustom->weather_provider == "gfs" &&
               loadedCustom->current_provider == "auto",
           "provider preferences must survive a config round trip");

    std::vector<xgrib::AreaPreset> empty;
    Expect(xgrib::SaveAreaPresets(&config, empty, &error),
           "deleting every named area must be supported");
    Expect(xgrib::LoadAreaPresets(&config).empty(),
           "an intentionally empty saved list must not reseed defaults");
  }
  wxRemoveFile(configFile);

  std::vector<xgrib::AreaPreset> changed = {
      {"supplied-irish-sea", "Changed", -1.0, 1.0, 2.0, 3.0},
      {"user-1", "My area", 20.0, 30.0, 21.0, 31.0}};
  xgrib::RestoreSuppliedAreaPresets(&changed);
  Expect(changed.size() == 9,
         "restoring supplied areas must preserve user-created areas");
  irish = FindById(changed, "supplied-irish-sea");
  Expect(irish && irish->name == "Irish Sea / North Channel" &&
             irish->west == -8.5,
         "restore must reset edited supplied values");
  Expect(FindById(changed, "user-1") != nullptr,
         "restore must not remove a user-created area");
  Expect(changed.front().id == "supplied-irish-sea" &&
             changed.back().id == "user-1",
         "restore must reinstate supplied ordering ahead of user areas");

  Expect(xgrib::NextAreaPresetId(changed) == "user-2",
         "new IDs must not collide with persisted areas");
  Expect(xgrib::UniqueAreaPresetName(changed, "My area") == "My area 2",
         "new names must be made unique predictably");
  std::cout << "xGRIB area preset tests passed\n";
  return 0;
}
