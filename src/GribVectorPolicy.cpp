#include "GribVectorPolicy.h"

#include "GribRecord.h"

namespace xgrib {

bool IsRenderableCurrentRecordPair(const GribRecord* eastward,
                                   const GribRecord* northward) {
  if (!eastward || !northward || eastward->getNi() != northward->getNi() ||
      eastward->getNj() != northward->getNj())
    return false;

  bool hasVector = false;
  for (int i = 0; i < eastward->getNi(); ++i) {
    for (int j = 0; j < eastward->getNj(); ++j) {
      const double u = eastward->getValue(i, j);
      const double v = northward->getValue(i, j);
      if (u == GRIB_NOTDEF || v == GRIB_NOTDEF) continue;

      hasVector = true;
      const double magnitude = std::hypot(u, v);
      if (!IsRenderableDirectionVector(magnitude, 0.0, false)) return false;
    }
  }
  return hasVector;
}

}  // namespace xgrib
