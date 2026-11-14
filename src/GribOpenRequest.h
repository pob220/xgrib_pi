// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <wx/jsonreader.h>
#include <wx/jsonval.h>
#include <wx/string.h>

namespace xgrib {
struct GribOpenRequest {
  bool valid{false};
  wxString file;
  bool show{false};
  bool RequestsDisplay() const { return valid && show && !file.empty(); }
};

// The optional boolean "show" extends the existing message. Omitted, false
// or non-boolean values retain the legacy non-activating settings behavior.
inline GribOpenRequest ParseGribOpenRequest(const wxString& json) {
  wxJSONReader reader;
  wxJSONValue root;
  GribOpenRequest request;
  if (reader.Parse(json, &root) != 0 || !root.IsObject()) return request;
  if (root.HasMember("grib_file") && !root["grib_file"].IsString())
    return request;
  request.valid = true;
  if (root.HasMember("grib_file")) request.file = root["grib_file"].AsString();
  request.show = root.HasMember("show") && root["show"].IsBool() &&
                 root["show"].AsBool();
  return request;
}
}  // namespace xgrib
