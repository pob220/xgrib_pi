// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <string>
#include <vector>
#include <wx/string.h>

class wxInputStream;

namespace xgrib {

struct WorldGribModel {
  std::string id;
  wxString title;
  int maxHours;
};

inline constexpr char kWorldGribModelsUrl[] = "https://grib.bosun.io/models";
inline constexpr char kWorldGribModelsFile[] = "world-models.json";

std::vector<WorldGribModel> ParseWorldGribModels(wxInputStream& stream);
std::vector<WorldGribModel> ReadWorldGribModels(const wxString& path);
std::vector<WorldGribModel> LoadWorldGribModels(const wxString& cache,
                                                const wxString& bundled);
std::string WorldGribUrl(const WorldGribModel& model, int hours, double south,
                          double north, double west, double east);
bool HasWorldGribFileMarkers(const wxString& path);
wxString WorldGribServerError(const wxString& path);

}  // namespace xgrib
