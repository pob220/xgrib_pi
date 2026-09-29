#include "GribVectorPolicy.h"

#include "GribRecord.h"
#include <cstdint>
#include <climits>

namespace xgrib {

bool IsRenderableCurrentRecordPair(const GribRecord* eastward,
                                   const GribRecord* northward) {
  if (!eastward || !northward || !eastward->isOk() || !northward->isOk() ||
      !eastward->isDataKnown() || !northward->isDataKnown() ||
      eastward->getNi() <= 0 || eastward->getNj() <= 0 ||
      std::uint64_t(eastward->getNi()) * eastward->getNj() > INT_MAX ||
      eastward->getNi() != northward->getNi() ||
      eastward->getNj() != northward->getNj())
    return false;

  bool hasVector = false;
  for (int j = 0; j < eastward->getNj(); ++j) {
    for (int i = 0; i < eastward->getNi(); ++i) {
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
