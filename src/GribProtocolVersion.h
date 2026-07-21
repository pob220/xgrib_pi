#ifndef XGRIB_GRIB_PROTOCOL_VERSION_H
#define XGRIB_GRIB_PROTOCOL_VERSION_H

namespace xgrib {

// Version of the GRIB timeline-record interchange contract implemented by
// xGRIB. This is deliberately independent of xGRIB's package version.
inline constexpr int kGribProtocolVersionMajor = 5;
inline constexpr int kGribProtocolVersionMinor = 0;

}  // namespace xgrib

#endif  // XGRIB_GRIB_PROTOCOL_VERSION_H
