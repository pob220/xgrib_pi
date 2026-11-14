#include <cstdlib>
#include <iostream>
#include <limits>

#include <wx/fileconf.h>
#include <wx/init.h>
#include <wx/sstream.h>
#include <wx/jsonwriter.h>

#include "XgribPluginPolicy.h"
#include "GribVectorPolicy.h"
#include "GribOpenRequest.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main() {
  wxInitializer initializer;
  Expect(initializer.IsOk(), "wxWidgets must initialize for config tests");

  wxStringInputStream emptyConfig("");
  wxFileConfig config(emptyConfig);
  config.SetPath("/Unrelated");
  const wxString originalPath = config.GetPath();

  Expect(!IsBundledGribPluginEnabled(&config),
         "missing bundled plugin must not report a conflict");
  Expect(config.GetPath() == originalPath,
         "policy check must restore the caller's config path");

  config.SetPath("/PlugIns/libgrib_pi.so");
  config.Write("bEnabled", false);
  config.SetPath(originalPath);
  Expect(!IsBundledGribPluginEnabled(&config),
         "disabled bundled plugin must not report a conflict");

  config.SetPath("/PlugIns/libgrib_pi.so");
  config.Write("bEnabled", true);
  config.SetPath(originalPath);
  Expect(IsBundledGribPluginEnabled(&config),
         "enabled Linux bundled plugin must report a conflict");
  Expect(config.GetPath() == originalPath,
         "positive policy check must restore the config path");

  wxStringInputStream emptyWindowsConfig("");
  wxFileConfig windowsConfig(emptyWindowsConfig);
  windowsConfig.SetPath("/PlugIns/grib_pi.dll");
  windowsConfig.Write("bEnabled", true);
  windowsConfig.SetPath("/");
  Expect(IsBundledGribPluginEnabled(&windowsConfig),
         "enabled Windows bundled plugin must report a conflict");

  Expect(!IsBundledGribPluginEnabled(nullptr),
         "missing OpenCPN config must fail open without a false conflict");

  Expect(xgrib::IsRenderableDirectionVector(12.0, 180.0, false),
         "a genuine 12 m/s current must remain renderable");
  Expect(!xgrib::IsRenderableDirectionVector(12.001, 180.0, false),
         "an implausible current must be rejected before arrow scaling");
  Expect(!xgrib::IsRenderableDirectionVector(
             std::numeric_limits<double>::infinity(), 180.0, false),
         "an infinite current must not reach the renderer");
  Expect(xgrib::IsRenderableDirectionVector(8.0, 180.0, true),
         "the current guard must not reject finite wave heights");
  Expect(!xgrib::IsRenderableDirectionVector(100.001, 180.0, true),
         "implausible wave heights must be rejected before symbol scaling");
  Expect(!xgrib::IsRenderableDirectionVector(8.0, 361.0, true),
         "out-of-range wave directions must not reach the renderer");
  const wxString externalPath = wxString("C:\\Weather files\\pr") +
                                wxUniChar(0x00e9) + "visions.grb";
  wxJSONValue openRequest;
  openRequest["grib_file"] = externalPath;
  openRequest["show"] = true;
  wxJSONWriter writer;
  wxString openBody;
  writer.Write(openRequest, openBody);
  const auto external = xgrib::ParseGribOpenRequest(openBody);
  Expect(external.RequestsDisplay(), "explicit open-and-show must activate");
  Expect(external.file == externalPath,
         "external Unicode paths must survive JSON writer/reader handoff");
  for (const auto& json : {R"({"grib_file":"forecast.grb"})",
                          R"({"grib_file":"forecast.grb","show":false})",
                          R"({"grib_file":"forecast.grb","show":"true"})",
                          R"({"show":true})", R"({"grib_file":"","show":true})"})
    Expect(!xgrib::ParseGribOpenRequest(json).RequestsDisplay(),
           "legacy, false, invalid-type and fileless requests must not activate");
  for (const auto& json : {"{", "[]", "true", R"({"grib_file":12,"show":true})"})
    Expect(!xgrib::ParseGribOpenRequest(json).valid,
           "malformed or wrongly typed requests must be rejected");
  Expect(xgrib::ParseGribOpenRequest(R"({"WindBarbedArrows":false})").valid,
         "legacy settings-only messages must remain valid");

  std::cout << "xGRIB plugin policy tests passed\n";
  return 0;
}
