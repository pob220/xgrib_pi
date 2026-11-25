// SPDX-License-Identifier: GPL-2.0-or-later
// World model catalogue and request fields follow OpenCPN's GRIB downloader.
#include "WorldGribModels.h"

#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/jsonreader.h>
#include <wx/wfstream.h>

namespace xgrib {
namespace {
bool ValidModelId(const std::string& id) {
  return !id.empty() && id.find_first_not_of(
                            "abcdefghijklmnopqrstuvwxyz"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
                            std::string::npos;
}
}  // namespace

std::vector<WorldGribModel> ParseWorldGribModels(wxInputStream& stream) {
  wxJSONReader reader;
  wxJSONValue catalogue;
  if (reader.Parse(stream, &catalogue) != 0 || !catalogue.IsArray())
    return {};

  std::vector<WorldGribModel> models;
  std::set<std::string> ids;
  for (int index = 0; index < catalogue.Size(); ++index) {
    auto entry = catalogue.ItemAt(index);
    if (!entry.IsObject() || !entry["name"].IsString() ||
        !entry["title"].IsString() || !entry["maxlength"].IsInt())
      return {};
    const auto modelName = entry["name"].AsString();
    const auto name = modelName.utf8_str();
    if (!name.data()) return {};
    WorldGribModel model{std::string(name.data(), name.length()),
                         entry["title"].AsString().Trim().Trim(false),
                         entry["maxlength"].AsInt()};
    if (!ValidModelId(model.id) || model.title.empty() || model.maxHours < 1 ||
        !ids.insert(model.id).second)
      return {};
    models.push_back(std::move(model));
  }
  return models;
}

std::vector<WorldGribModel> ReadWorldGribModels(const wxString& path) {
  if (!wxFileExists(path)) return {};
  wxFileInputStream stream(path);
  return stream.IsOk() ? ParseWorldGribModels(stream)
                       : std::vector<WorldGribModel>{};
}

std::vector<WorldGribModel> LoadWorldGribModels(const wxString& cache,
                                                const wxString& bundled) {
  auto models = ReadWorldGribModels(cache);
  if (models.empty()) models = ReadWorldGribModels(bundled);
  return models;
}

std::string WorldGribUrl(const WorldGribModel& model, int hours, double south,
                          double north, double west, double east) {
  if (!ValidModelId(model.id) || hours < 1 || hours > model.maxHours)
    throw std::invalid_argument("Invalid model or forecast duration");
  std::ostringstream url;
  url.imbue(std::locale::classic());
  url << "https://grib.bosun.io/grib?model=" << model.id << "&latmin=" << south
      << "&latmax=" << north << "&lonmin=" << west << "&lonmax=" << east
      << "&length=" << hours << "&encoding=ccsds";
  return url.str();
}

bool HasWorldGribFileMarkers(const wxString& path) {
  wxFile file(path);
  char marker[4];
  return file.IsOpened() && file.Length() >= 20 &&
         file.Read(marker, sizeof(marker)) == 4 &&
         std::string_view(marker, sizeof(marker)) == "GRIB" &&
         file.Seek(-4, wxFromEnd) != wxInvalidOffset &&
         file.Read(marker, sizeof(marker)) == 4 &&
         std::string_view(marker, sizeof(marker)) == "7777";
}

wxString WorldGribServerError(const wxString& path) {
  if (!wxFileExists(path) || wxFileName::GetSize(path) >= 1024) return {};
  wxFile file(path);
  wxString reason;
  if (!file.IsOpened() || !file.ReadAll(&reason, wxConvUTF8)) return {};
  reason.Trim().Trim(false);
  if (reason.StartsWith("<") || reason.StartsWith("GRIB")) return {};
  for (const auto character : reason) {
    if (character < 32 && character != '\n' && character != '\r' &&
        character != '\t')
      return {};
  }
  return reason;
}
}  // namespace xgrib
