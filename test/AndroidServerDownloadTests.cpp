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
    Require(xgrib::ServerGribUrl(box, "ecmwf0p25", 24) ==
      "https://grib.bosun.io/grib?model=ecmwf0p25&latmin=50.5&latmax=56.5&lonmin=-8.5&lonmax=-2.5&length=24", "desktop server request parity");
    for (int hours : {24, 72, 999})
      Require(xgrib::ServerGribUrl(box, "ecmwfaifs0p25", hours).find("ecmwfaifs0p25") != std::string::npos, "AIFS supported");
    Reject([&] { xgrib::ServerGribUrl(box, "untrusted&url=http://example.org", 24); }, "reject model injection");
    Reject([&] { xgrib::ServerGribUrl(box, "ecmwf0p25", 0); }, "reject unsupported duration");
    Reject([&] { xgrib::ServerGribUrl({10, 50, -10, 60}, "ecmwf0p25", 24); }, "reject inverted area");
    Reject([&] { xgrib::ServerGribUrl({-8, 50, std::numeric_limits<double>::quiet_NaN(), 60}, "ecmwf0p25", 24); }, "reject NaN");
    eg::EnvironmentRequest request;
    request.bbox = box;
    request.output = std::filesystem::path(directory) / "untouched.grb2";
    request.execution.cancelled = std::make_shared<std::atomic<bool>>(true);
    Reject([&] { xgrib::DownloadServerGrib(request, "ecmwf0p25", 24); }, "cancel before network/files");
    Require(!std::filesystem::exists(request.output) && !std::filesystem::exists(request.output.string() + ".part"), "cancel must leave no partial files");
    const auto partial = request.output.string() + ".part";
    { std::ofstream out(partial); out << "sentinel"; }
    request.execution.cancelled->store(false);
    Reject([&] { xgrib::DownloadServerGrib(request, "ecmwf0p25", 24); }, "reject existing partial before network");
    std::ifstream in(partial); std::string value; in >> value;
    Require(value == "sentinel", "must not alter an existing partial file");
    in.close(); std::filesystem::remove(partial);
    { std::ofstream out(request.output); out << "existing forecast"; }
    Reject([&] { xgrib::DownloadServerGrib(request, "ecmwf0p25", 24); }, "reject existing forecast before network");
    Require(!std::filesystem::exists(partial), "existing forecast must not create partial file");
    std::ifstream original(request.output); std::getline(original, value);
    Require(value == "existing forecast", "must never overwrite original forecast");
    original.close(); std::filesystem::remove(request.output); std::filesystem::remove(directory);
    std::cout << "Android server URL, validation, cancellation and exclusive-file checks passed\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
