#include <cstdlib>
#include <iostream>
#include <limits>

#include <wx/fileconf.h>
#include <wx/init.h>
#include <wx/sstream.h>

#include "XgribPluginPolicy.h"
#include "GribVectorPolicy.h"

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
  std::cout << "xGRIB plugin policy tests passed\n";
  return 0;
}
