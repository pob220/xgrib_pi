#include "WorldGribModels.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/mstream.h>

namespace {
void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

auto Parse(std::string_view json) {
  wxMemoryInputStream input(json.data(), json.size());
  return xgrib::ParseWorldGribModels(input);
}

class TemporaryFile {
public:
  TemporaryFile() : path(wxFileName::CreateTempFileName("xgrib-world-test-")) {}
  ~TemporaryFile() { wxRemoveFile(path); }
  void Write(const wxString& contents) {
    wxFile file(path, wxFile::write);
    Check(file.IsOpened() && file.Write(contents), "write temporary file");
  }
  const wxString path;
};
}  // namespace

int main(int argc, char** argv) {
  try {
    Check(argc == 2, "bundled catalogue path is required");
    const auto bundled = wxString::FromUTF8(argv[1]);
    const auto catalogue = xgrib::ReadWorldGribModels(bundled);
    for (const auto* id : {"ecmwf0p25", "ecmwf0p1", "ecmwfaifs0p25", "gfs0p25",
                           "dwdicon0p125", "gdps0p15", "arpege0p25", "rtofs0p1",
                           "rtofs0p25", "smoc0p083"})
      Check(std::any_of(catalogue.begin(), catalogue.end(),
                        [=](const auto& model) { return model.id == id; }),
            id);
    Check(Parse("[]").empty(), "reject empty list");
    Check(Parse(R"([{"name":"x","title":"model","maxlength":0}])").empty(),
          "reject zero forecast length");
    Check(Parse(R"([{"name":"x","title":"model","maxlength":"24"}])").empty(),
          "reject string forecast length");
    Check(Parse(R"([{"name":"x&bad=1","title":"model","maxlength":24}])").empty(),
          "reject name unsafe for a URL or filename");
    Check(Parse(R"([{"name":"x","title":"one","maxlength":24},
                     {"name":"x","title":"two","maxlength":48}])").empty(),
          "reject duplicate model IDs");

    TemporaryFile cache;
    cache.Write("<html>error</html>");
    Check(xgrib::LoadWorldGribModels(cache.path, bundled).size() ==
              catalogue.size(),
          "a bad cached catalogue falls back to the packaged one");
    Check(xgrib::WorldGribServerError(cache.path).empty(), "ignore HTML error");
    cache.Write("  Area exceeds the limit.\n");
    Check(xgrib::WorldGribServerError(cache.path) == "Area exceeds the limit.",
          "report the server's plain text reason");
    cache.Write("GRIB0123456789abcdef");
    Check(!xgrib::HasWorldGribFileMarkers(cache.path),
          "reject an incomplete GRIB response");
    cache.Write("GRIB0123456789abcdef7777");
    Check(xgrib::HasWorldGribFileMarkers(cache.path),
          "recognize both markers on the hosted GRIB stream");
    cache.Write(R"([{"name":"smoc0p083","title":"CMEMS SMOC","maxlength":239}])");
    const auto refreshed = xgrib::LoadWorldGribModels(cache.path, bundled);
    Check(refreshed.size() == 1, "use a valid refreshed catalogue");
    const auto& model = refreshed.front();
    Check(xgrib::WorldGribUrl(model, 137, 35.5, 36.5, -6.5, -5) ==
              "https://grib.bosun.io/grib?model=smoc0p083&latmin=35.5"
              "&latmax=36.5&lonmin=-6.5&lonmax=-5&length=137&encoding=ccsds",
          "download the selected model and exact number of hours using CCSDS");
    try {
      xgrib::WorldGribUrl(model, 240, 35.5, 36.5, -6.5, -5);
      Check(false, "reject hours beyond the model limit");
    } catch (const std::invalid_argument&) {
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
