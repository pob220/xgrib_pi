#ifndef XGRIB_GRIB_DISPLAY_GEOMETRY_H
#define XGRIB_GRIB_DISPLAY_GEOMETRY_H

#include <algorithm>
#include <cmath>

namespace xgrib {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMetresPerSecondToKnots = 1.94384449;

struct DisplayAxes {
  double along_x;
  double along_y;
  double across_x;
  double across_y;
};

inline int LegacyDirectionArrowSizePixels(int legacy_size_index) {
  return legacy_size_index == 1 ? 16 : 26;
}

inline double NormalizeBearing(double degrees) {
  double normalized = std::fmod(degrees, 360.0);
  if (normalized < 0.0) normalized += 360.0;
  return normalized;
}

inline double WaveTravelBearing(double from_degrees) {
  return NormalizeBearing(from_degrees + 180.0);
}

inline double CurrentTowardBearing(double interpolated_from_degrees) {
  return NormalizeBearing(interpolated_from_degrees + 180.0);
}

inline DisplayAxes BearingDisplayAxes(double bearing_degrees,
                                      double viewport_rotation_radians) {
  const double radians = NormalizeBearing(bearing_degrees) * kPi / 180.0 +
                         viewport_rotation_radians;
  const double along_x = std::sin(radians);
  const double along_y = -std::cos(radians);
  return {along_x, along_y, -along_y, along_x};
}

inline double WaveHeightCircleRadius(double symbol_size_pixels,
                                     double height_metres) {
  const double size = std::clamp(symbol_size_pixels, 8.0, 40.0);
  return std::clamp(size * 0.22 + height_metres * 1.5, 4.0, size * 0.55);
}

inline double ProportionalCurrentLength(double base_size_pixels,
                                        double growth_pixels_per_knot,
                                        double speed_metres_per_second) {
  const double base = std::clamp(base_size_pixels, 6.0, 40.0);
  const double growth = std::clamp(growth_pixels_per_knot, 0.0, 12.0);
  const double knots =
      std::max(0.0, speed_metres_per_second) * kMetresPerSecondToKnots;
  return std::clamp(base + growth * knots, 6.0, 64.0);
}

}  // namespace xgrib

#endif  // XGRIB_GRIB_DISPLAY_GEOMETRY_H
