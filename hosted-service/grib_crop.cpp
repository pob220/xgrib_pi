// Regional subsets of cached NOAA GFS regular latitude/longitude GRIB2 grids.
// Metadata is cloned from the source. No interpolation or model substitution.
#include <eccodes.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

void check(int err, const std::string& key) {
  if (err) throw std::runtime_error(key + ": " + codes_get_error_message(err));
}
long integer(codes_handle* h, const char* key) {
  long v; check(codes_get_long(h, key, &v), key); return v;
}
double number(codes_handle* h, const char* key) {
  double v; check(codes_get_double(h, key, &v), key); return v;
}
std::string string(codes_handle* h, const char* key) {
  char v[128]; size_t n = sizeof v;
  check(codes_get_string(h, key, v, &n), key); return v;
}
void set(codes_handle* h, const char* key, long v) {
  check(codes_set_long(h, key, v), key);
}
void set(codes_handle* h, const char* key, double v) {
  check(codes_set_double(h, key, v), key);
}
void set(codes_handle* h, const char* key, const std::string& v) {
  size_t n = v.size(); check(codes_set_string(h, key, v.c_str(), &n), key);
}
double normal(double x) { return x - 360.0 * std::floor(x / 360.0); }
using Handle = std::unique_ptr<codes_handle, decltype(&codes_handle_delete)>;
struct FileCloser { void operator()(FILE* f) const { if (f) fclose(f); } };

int main(int argc, char** argv) {
  try {
    if (argc != 11 && argc != 12) throw std::runtime_error(
        "usage: grib-crop INPUT OUTPUT WEST SOUTH EAST NORTH STRIDE CYCLE HOUR FIELDS [--east-inclusive]");
    const bool east_inclusive = argc == 12 && std::string(argv[11]) == "--east-inclusive";
    if (argc == 12 && !east_inclusive) throw std::runtime_error("unknown crop option");
    double west = std::stod(argv[3]), south = std::stod(argv[4]);
    double east = std::stod(argv[5]), north = std::stod(argv[6]);
    int stride = std::stoi(argv[7]), hour = std::stoi(argv[9]);
    std::string cycle = argv[8];
    if (!std::isfinite(west) || !std::isfinite(east) || !std::isfinite(south) ||
        !std::isfinite(north) || west < -180 || west > 180 || east < -180 ||
        east > 180 || south < -90 || north > 90 || south >= north ||
        west == east || stride < 1 || stride > 8 || cycle.size() != 10)
      throw std::runtime_error("invalid region, stride or cycle");
    std::set<std::string> wanted;
    std::string names = argv[10];
    while (!names.empty()) {
      auto p = names.find(','); wanted.insert(names.substr(0, p));
      names = p == std::string::npos ? "" : names.substr(p + 1);
    }
    std::unique_ptr<FILE, FileCloser> input(fopen(argv[1], "rb"));
    std::unique_ptr<FILE, FileCloser> output(fopen(argv[2], "wb"));
    if (!input || !output) throw std::runtime_error("unable to open GRIB files");
    std::set<std::string> found;
    int err = 0;
    while (auto raw = codes_handle_new_from_file(nullptr, input.get(), PRODUCT_GRIB, &err)) {
      Handle source(raw, codes_handle_delete);
      auto name = string(raw, "shortName");
      if (!wanted.count(name)) continue;
      if (!found.insert(name).second) throw std::runtime_error("duplicate field");
      if (integer(raw, "edition") != 2 || string(raw, "gridType") != "regular_ll" ||
          integer(raw, "jPointsAreConsecutive") || integer(raw, "alternativeRowScanning") ||
          integer(raw, "iScansNegatively"))
        throw std::runtime_error("unsupported GRIB grid or scanning convention");
      if (integer(raw, "dataDate") != std::stol(cycle.substr(0, 8)) ||
          integer(raw, "dataTime") != std::stol(cycle.substr(8)) * 100 ||
          integer(raw, "endStep") != hour || integer(raw, "stepUnits") != 1)
        throw std::runtime_error("source forecast identity mismatch");
      long ni = integer(raw, "Ni"), nj = integer(raw, "Nj");
      if (ni < 2 || nj < 2 || ni * nj > 3000000)
        throw std::runtime_error("unsupported grid dimensions");
      double lon0 = number(raw, "longitudeOfFirstGridPointInDegrees");
      double lat0 = number(raw, "latitudeOfFirstGridPointInDegrees");
      double dx = number(raw, "iDirectionIncrementInDegrees");
      double dy = number(raw, "jDirectionIncrementInDegrees");
      bool northward = integer(raw, "jScansPositively");
      dy *= northward ? 1 : -1;
      double span = east > west ? east - west : east + 360 - west;
      std::vector<std::pair<double, long>> cols;
      std::vector<long> rows;
      // NOMADS weather includes the eastern cell; waves exclude it. A global
      // grid includes each column once. Both latitude limits are inclusive.
      for (long i = 0; i < ni; ++i) {
        double delta = normal(lon0 + i * dx - west);
        if (delta < span - 1e-7 || (east_inclusive && delta <= span + 1e-7))
          cols.emplace_back(delta, i);
      }
      std::sort(cols.begin(), cols.end());
      for (long j = 0; j < nj; ++j) {
        double lat = lat0 + j * dy;
        if (lat >= south - 1e-7 && lat <= north + 1e-7) rows.push_back(j);
      }
      std::vector<long> selected_cols, selected_rows;
      for (size_t i = 0; i < cols.size(); i += stride) selected_cols.push_back(cols[i].second);
      for (size_t j = 0; j < rows.size(); j += stride) selected_rows.push_back(rows[j]);
      if (selected_cols.size() < 2 || selected_rows.size() < 2)
        throw std::runtime_error("region has fewer than two grid points per dimension");
      size_t count = ni * nj;
      std::vector<double> values(count), subset;
      check(codes_get_double_array(raw, "values", values.data(), &count), "values");
      if (count != static_cast<size_t>(ni * nj)) throw std::runtime_error("grid/value count mismatch");
      double missing = number(raw, "missingValue");
      bool has_missing = false;
      for (long j : selected_rows) for (long i : selected_cols) {
        auto value = values[j * ni + i];
        has_missing |= value == missing;
        subset.push_back(value);
      }
      Handle target(codes_handle_clone(raw), codes_handle_delete);
      if (!target) throw std::runtime_error("unable to clone GRIB");
      set(target.get(), "packingType", std::string("grid_simple"));
      // At least source bit precision, with two guard bits. Repacking is
      // independently checked against native values in integration tests.
      set(target.get(), "bitsPerValue", std::min(32L, std::max(16L, integer(raw, "bitsPerValue") + 2)));
      set(target.get(), "Ni", static_cast<long>(selected_cols.size()));
      set(target.get(), "Nj", static_cast<long>(selected_rows.size()));
      set(target.get(), "latitudeOfFirstGridPointInDegrees", lat0 + selected_rows.front() * dy);
      set(target.get(), "latitudeOfLastGridPointInDegrees", lat0 + selected_rows.back() * dy);
      set(target.get(), "longitudeOfFirstGridPointInDegrees", normal(lon0 + selected_cols.front() * dx));
      set(target.get(), "longitudeOfLastGridPointInDegrees", normal(lon0 + selected_cols.back() * dx));
      set(target.get(), "iDirectionIncrementInDegrees", dx * stride);
      set(target.get(), "jDirectionIncrementInDegrees", std::abs(dy) * stride);
      set(target.get(), "bitmapPresent", static_cast<long>(has_missing));
      check(codes_set_double_array(target.get(), "values", subset.data(), subset.size()), "subset values");
      const void* message = nullptr; size_t length = 0;
      check(codes_get_message(target.get(), &message, &length), "message");
      if (fwrite(message, 1, length, output.get()) != length) throw std::runtime_error("write failed");
    }
    check(err, "read GRIB");
    if (found != wanted) throw std::runtime_error("missing requested fields");
    if (fflush(output.get())) throw std::runtime_error("flush failed");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
  }
}
