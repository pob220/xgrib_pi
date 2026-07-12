#include <cstdlib>
#include <iostream>

#include <wx/fileconf.h>

#include "XgribPluginPolicy.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main() {
  wxFileConfig config;
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

  wxFileConfig windowsConfig;
  windowsConfig.SetPath("/PlugIns/grib_pi.dll");
  windowsConfig.Write("bEnabled", true);
  windowsConfig.SetPath("/");
  Expect(IsBundledGribPluginEnabled(&windowsConfig),
         "enabled Windows bundled plugin must report a conflict");

  Expect(!IsBundledGribPluginEnabled(nullptr),
         "missing OpenCPN config must fail open without a false conflict");
  std::cout << "xGRIB plugin policy tests passed\n";
  return 0;
}
