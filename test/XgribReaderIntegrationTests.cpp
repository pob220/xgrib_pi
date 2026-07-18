#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include <wx/string.h>

#include "GribReader.h"
#include "GribVectorPolicy.h"
#include "GribV2Record.h"

namespace {

class InterpolationFixtureRecord : public GribRecord {
public:
  InterpolationFixtureRecord(double spacing, double u, double origin = 0.0) {
    ok = true;
    knownData = true;
    hasBMS = false;
    BMSbits = nullptr;
    isAdjacentI = true;
    Lo1 = La1 = origin;
    Lo2 = La2 = origin + 1.0;
    lonMin = latMin = origin;
    lonMax = latMax = origin + 1.0;
    Di = Dj = spacing;
    Ni = Nj = static_cast<zuint>(std::lround(1.0 / spacing)) + 1;
    data = new double[Ni * Nj];
    std::fill(data, data + Ni * Nj, u);
  }
};

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  Expect(GribV2DataTypeForParameter(10, 0, 3) == GRB_HTSGW,
         "GRIB2 significant wave height should be recognized");
  Expect(GribV2DataTypeForParameter(10, 0, 10) == GRB_WVDIR,
         "GRIB2 primary wave direction should be recognized");
  Expect(GribV2DataTypeForParameter(10, 0, 11) == GRB_WVPER,
         "GRIB2 primary wave period should be recognized");

  // Weather Routing requests arbitrary timeline times from xGRIB. Verify the
  // interpolation used for a regional-to-global model handover can bridge
  // aligned grids with different resolutions (UKV/GFS is a 10:1 ratio).
  InterpolationFixtureRecord fineU(0.1, 2.0), fineV(0.1, 0.0);
  InterpolationFixtureRecord coarseU(1.0, 4.0), coarseV(1.0, 0.0);
  GribRecord* interpolatedV = nullptr;
  std::unique_ptr<GribRecord> interpolatedU(
      GribRecord::Interpolated2DRecord(interpolatedV, fineU, fineV, coarseU,
                                       coarseV, 0.5));
  std::unique_ptr<GribRecord> interpolatedVOwner(interpolatedV);
  Expect(interpolatedU && interpolatedVOwner,
         "mixed-resolution vector interpolation should succeed");
  Expect(interpolatedU->getNi() == 2 && interpolatedU->getNj() == 2,
         "mixed-resolution interpolation should use the common coarse grid");
  Expect(std::abs(interpolatedU->getValue(0, 0) - 3.0) < 1e-9,
         "mixed-resolution interpolation should blend vector magnitude");

  InterpolationFixtureRecord offsetCoarseU(1.0, 4.0, 0.03);
  InterpolationFixtureRecord offsetCoarseV(1.0, 0.0, 0.03);
  interpolatedV = nullptr;
  std::unique_ptr<GribRecord> offsetInterpolatedU(
      GribRecord::Interpolated2DRecord(interpolatedV, fineU, fineV,
                                       offsetCoarseU, offsetCoarseV, 0.5));
  std::unique_ptr<GribRecord> offsetInterpolatedVOwner(interpolatedV);
  Expect(offsetInterpolatedU && offsetInterpolatedVOwner,
         "misaligned regional/global grids should use spatial sampling");
  Expect(std::abs(offsetInterpolatedU->getValue(0, 0) - 3.0) < 1e-9,
         "misaligned-grid sampling should preserve the temporal blend");

  Expect(argc == 2 || argc == 3,
         "usage: xgrib_reader_integration_tests FILE.grb [--any]");

  GribReader reader(wxString::FromUTF8(argv[1]));
  Expect(reader.isOk(), "xGRIB reader rejected native generator output");
  if (argc == 3) {
    Expect(std::string(argv[2]) == "--any", "unknown reader test option");
    Expect(reader.getTotalNumberOfGribRecords() > 0,
           "generated GRIB should contain at least one recognized record");
    std::cout << "xGRIB reader accepted generated output\n";
    return 0;
  }
  Expect(reader.getTotalNumberOfGribRecords() == 6,
         "native generator fixture should contain six current records");
  Expect(reader.getNumberOfDates() == 3,
         "native generator fixture should contain three valid times");

  int uRecords = 0;
  int vRecords = 0;
  GribRecord* firstU = nullptr;
  GribRecord* firstV = nullptr;
  for (const auto& [key, records] : *reader.getGribMap()) {
    (void)key;
    if (!records) continue;
    for (const GribRecord* record : *records) {
      if (record->getDataType() == GRB_UOGRD) {
        ++uRecords;
        if (!firstU) firstU = const_cast<GribRecord*>(record);
      }
      if (record->getDataType() == GRB_VOGRD) {
        ++vRecords;
        if (!firstV) firstV = const_cast<GribRecord*>(record);
      }
    }
  }
  Expect(uRecords == 3, "expected three U-current records");
  Expect(vRecords == 3, "expected three V-current records");
  Expect(xgrib::IsRenderableCurrentRecordPair(firstU, firstV),
         "native generator fixture currents should be renderable");
  const double original = firstU->getValue(0, 0);
  firstU->setValue(0, 0, 12.001);
  firstV->setValue(0, 0, 0.0);
  Expect(!xgrib::IsRenderableCurrentRecordPair(firstU, firstV),
         "one implausible vector must reject the complete current record pair");
  firstU->setValue(0, 0, original);

  std::cout << "xGRIB reader accepted native generator output\n";
  return 0;
}
