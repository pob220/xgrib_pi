#include <cmath>
#include <cstdlib>
#include <iostream>

#include "GribDisplayGeometry.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

void ExpectNear(double actual, double expected, double tolerance,
                const char* message) {
  Expect(std::abs(actual - expected) <= tolerance, message);
}

}  // namespace

int main() {
  Expect(xgrib::LegacyDirectionArrowSizePixels(0) == 26,
         "legacy default arrow size must migrate to 26 pixels");
  Expect(xgrib::LegacyDirectionArrowSizePixels(1) == 16,
         "legacy small arrow size must migrate to 16 pixels");

  ExpectNear(xgrib::WaveTravelBearing(0.0), 180.0, 1e-9,
             "waves from north must travel south");
  ExpectNear(xgrib::WaveTravelBearing(90.0), 270.0, 1e-9,
             "waves from east must travel west");
  ExpectNear(xgrib::CurrentTowardBearing(-180.0), 0.0, 1e-9,
             "interpolated current directions must normalize cleanly");
  const auto south = xgrib::BearingDisplayAxes(180.0, 0.0);
  ExpectNear(south.along_x, 0.0, 1e-9,
             "southbound symbols must have no horizontal component");
  ExpectNear(south.along_y, 1.0, 1e-9,
             "southbound symbols must point down the display");
  const auto rotated = xgrib::BearingDisplayAxes(180.0, xgrib::kPi / 2.0);
  ExpectNear(rotated.along_x, -1.0, 1e-9,
             "viewport rotation must rotate symbol geometry");
  ExpectNear(rotated.along_y, 0.0, 1e-9,
             "rotated symbol geometry must preserve direction");

  ExpectNear(xgrib::WaveHeightCircleRadius(12.0, 0.0), 4.0, 1e-9,
             "calm-wave circles must retain a visible minimum radius");
  Expect(xgrib::WaveHeightCircleRadius(20.0, 3.0) >
             xgrib::WaveHeightCircleRadius(20.0, 1.0),
         "wave circle radius must grow with significant height");
  ExpectNear(xgrib::WaveHeightCircleRadius(20.0, 100.0), 11.0, 1e-9,
             "wave circle radius must remain bounded by symbol size");

  const double slowCurrent = xgrib::ProportionalCurrentLength(8.0, 6.0, 0.5);
  const double fastCurrent = xgrib::ProportionalCurrentLength(8.0, 6.0, 1.0);
  Expect(fastCurrent > slowCurrent,
         "proportional current arrows must grow with speed");
  ExpectNear(xgrib::ProportionalCurrentLength(8.0, 6.0, 100.0), 64.0, 1e-9,
             "proportional current arrows must remain within the display cap");
  ExpectNear(xgrib::ProportionalCurrentLength(4.0, -1.0, 1.0), 6.0, 1e-9,
             "current display settings must be clamped before geometry use");

  std::cout << "xGRIB display geometry tests passed\n";
  return 0;
}
