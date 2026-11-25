#include "AndroidServerDownload.h"
#include <fstream>
#include <iostream>
#include <limits>

void Require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
template<typename F> void Reject(F test, const char* message) {
  bool rejected = false;
  try { test(); } catch (const std::exception&) { rejected = true; }
  Require(rejected, message);
}
int main() {
  namespace eg = environmental_grib;
  try {
    std::string directory = (std::filesystem::temp_directory_path() / "xgrib-server-test-XXXXXX").string();
    Require(mkdtemp(directory.data()) != nullptr, "create isolated test directory");
    const eg::BoundingBox box{-8.5, 50.5, -2.5, 56.5};
    const xgrib::WorldGribModel model{"ecmwf0p25", "ECMWF IFS", 360};
    const xgrib::WorldGribModel current{"smoc0p083", "CMEMS SMOC", 239};
    Require(xgrib::ServerGribUrl(box, model, 37) ==
      "https://grib.bosun.io/grib?model=ecmwf0p25&latmin=50.5&latmax=56.5&lonmin=-8.5&lonmax=-2.5&length=37&encoding=ccsds", "desktop server request parity");
    Require(xgrib::ServerGribUrl(box, current, 72).find("smoc0p083") !=
                std::string::npos, "World current model supported");
    Reject([&] { xgrib::ServerGribUrl(box, current, 240); }, "reject model-specific maximum");
    Require(xgrib::ServerGribUrl({179, 0, -179, 1}, model, 24).find("lonmin=179&lonmax=-179") != std::string::npos,
            "server uses canonical crossing bounds");
    Reject([&] { xgrib::ServerGribUrl({10, 50, 10, 60}, model, 24); }, "reject zero-width area");
    Reject([&] { xgrib::ServerGribUrl({-8, 50, std::numeric_limits<double>::quiet_NaN(), 60}, model, 24); }, "reject NaN");
    eg::EnvironmentRequest request;
    request.bbox = box;
    request.output = std::filesystem::path(directory) / "untouched.grb2";
    request.execution.cancelled = std::make_shared<std::atomic<bool>>(true);
    Reject([&] { xgrib::DownloadServerGrib(request, model, 24); }, "cancel before network/files");
    Require(!std::filesystem::exists(request.output) && !std::filesystem::exists(request.output.string() + ".part"), "cancel must leave no partial files");
    const auto partial = request.output.string() + ".part";
    { std::ofstream out(partial); out << "sentinel"; }
    request.execution.cancelled->store(false);
    Reject([&] { xgrib::DownloadServerGrib(request, model, 24); }, "reject existing partial before network");
    std::ifstream in(partial); std::string value; in >> value;
    Require(value == "sentinel", "must not alter an existing partial file");
    in.close(); std::filesystem::remove(partial);
    { std::ofstream out(request.output); out << "existing forecast"; }
    Reject([&] { xgrib::DownloadServerGrib(request, model, 24); }, "reject existing forecast before network");
    Require(!std::filesystem::exists(partial), "existing forecast must not create partial file");
    std::ifstream original(request.output); std::getline(original, value);
    Require(value == "existing forecast", "must never overwrite original forecast");
    original.close(); std::filesystem::remove(request.output); std::filesystem::remove(directory);
    std::cout << "Android server URL, validation, cancellation and exclusive-file checks passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
