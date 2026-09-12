#include "GeneratorJobJson.h"

#include <iostream>

#include <wx/jsonreader.h>
#include <wx/jsonwriter.h>

int main() {
  wxJSONValue envelope = CreateGeneratorJobEnvelope();
  envelope["request"]["waveProvider"] = wxString("gfs_wave");

  wxJSONWriter writer;
  wxString encoded;
  writer.Write(envelope, encoded);

  wxJSONValue decoded;
  wxJSONReader reader;
  if (reader.Parse(encoded, &decoded) != 0) {
    std::cerr << "Failed to parse generated job envelope\n";
    return 1;
  }

  const auto& operation = decoded["operation"];
  const auto& cycle = decoded["request"]["cycle"];
  const auto& waveProvider = decoded["request"]["waveProvider"];
  const auto& passwordEnvironment =
      decoded["credentials"]["copernicusPasswordEnvironment"];
  if (decoded["schemaVersion"].AsInt() != 1 || !operation.IsString() ||
      operation.AsString() != "generateEnvironment" || !cycle.IsString() ||
      cycle.AsString() != "auto" || !waveProvider.IsString() ||
      waveProvider.AsString() != "gfs_wave" ||
      !passwordEnvironment.IsString() ||
      passwordEnvironment.AsString() !=
          "ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD") {
    std::cerr << "Generated job envelope does not match schema v1:\n"
              << encoded << '\n';
    return 1;
  }
  // Same parser the GUI uses for a completed native job, including >32-bit
  // numeric counts and backward compatibility with older helpers.
  wxJSONValue result;
  double mib = -1;
  if (ReadGeneratorActualNumericMiB(result, &mib)) return 2;
  result["schemaVersion"] = 1;
  result["status"] = wxString("complete");
  auto& report = result["result"]["size_comparison"];
  report["schemaVersion"] = 1;
  report["actual"]["numericComplete"] = true;
  report["actual"]["decodedBytes"] = static_cast<wxULongLong_t>(8589934592ULL);
  writer.Write(result, encoded);
  if (reader.Parse(encoded, &decoded) != 0 ||
      !ReadGeneratorActualNumericMiB(decoded, &mib) || mib != 8192.0) return 3;
  report["actual"]["numericComplete"] = false;
  if (ReadGeneratorActualNumericMiB(result, &mib)) return 4;
  report["actual"]["numericComplete"] = true;
  report["actual"]["decodedBytes"] = wxString("8589934592");
  if (ReadGeneratorActualNumericMiB(result, &mib)) return 5;
  report["actual"]["decodedBytes"] = -1;
  if (ReadGeneratorActualNumericMiB(result, &mib)) return 6;
  report["actual"]["decodedBytes"] = 1024;
  result["status"] = wxString("failed");
  if (ReadGeneratorActualNumericMiB(result, &mib)) return 7;
  result["status"] = wxString("complete");
  report["schemaVersion"] = 2;
  if (ReadGeneratorActualNumericMiB(result, &mib)) return 8;
  return 0;
}
