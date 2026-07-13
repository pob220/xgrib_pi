#ifndef XGRIB_GRIB_VECTOR_POLICY_H
#define XGRIB_GRIB_VECTOR_POLICY_H

#include <cmath>

class GribRecord;

namespace xgrib {

// Currents above this value are treated as malformed display data. This is a
// rendering guard only; valid GRIB values are not modified or clamped.
constexpr double kMaxRenderableCurrentMetresPerSecond = 12.0;

inline bool IsRenderableDirectionVector(double magnitude,
                                        double directionDegrees,
                                        bool isWaveVector) {
  if (!std::isfinite(magnitude) || !std::isfinite(directionDegrees) ||
      magnitude < 0.0)
    return false;
  return isWaveVector || magnitude <= kMaxRenderableCurrentMetresPerSecond;
}

bool IsRenderableCurrentRecordPair(const GribRecord* eastward,
                                   const GribRecord* northward);

}  // namespace xgrib

#endif  // XGRIB_GRIB_VECTOR_POLICY_H
