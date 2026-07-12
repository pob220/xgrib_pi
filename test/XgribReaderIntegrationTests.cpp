#include <cstdlib>
#include <iostream>
#include <string>

#include <wx/string.h>

#include "GribReader.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
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
  for (const auto& [key, records] : *reader.getGribMap()) {
    (void)key;
    if (!records) continue;
    for (const GribRecord* record : *records) {
      if (record->getDataType() == GRB_UOGRD) ++uRecords;
      if (record->getDataType() == GRB_VOGRD) ++vRecords;
    }
  }
  Expect(uRecords == 3, "expected three U-current records");
  Expect(vRecords == 3, "expected three V-current records");

  std::cout << "xGRIB reader accepted native generator output\n";
  return 0;
}
