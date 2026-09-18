#include "AndroidGribGenerator.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/datetime.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/notebook.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/utils.h>

#include "ocpn_plugin.h"

namespace {

struct CurrentRegion {
  const char* label;
  const char* id;
  double west, east, south, north;  // NOAA GRIB grid, 0–360° longitude
  double exampleWest, exampleEast, exampleSouth, exampleNorth;
};

// Grid bounds were checked against the Section 3 headers of NOAA's 2026-09-18
// f024 GRIBs. Examples are smaller than the source grids and can be edited.
constexpr CurrentRegion kCurrentRegions[] = {
    {"Western Atlantic", "west_atl", 260, 306, 10, 44.8, -80, -70, 25, 35},
    {"Arctic", "arctic", 160, 236, 60, 80, -170, -150, 65, 75},
    {"Alaska", "alaska", 140, 245, 40, 85, -165, -145, 50, 60},
    {"Bering Sea", "bering", 155, 211, 40, 67.2, -175, -160, 50, 60},
    {"Gulf of Alaska", "gulf_alaska", 195, 237, 40, 62.5, -160, -145, 45, 55},
    {"Hudson Bay / Baffin", "hudson_baffin", 251, 333, 40, 78,
     -75, -60, 55, 65},
    {"US West Coast", "west_conus", 210, 260, 10, 60, -130, -120, 30, 40},
    {"Hawaii", "honolulu", 180, 230, 0, 40, -165, -150, 15, 25},
    {"Guam", "guam", 130, 180, 0, 30, 140, 150, 10, 20},
    {"Samoa", "samoa", 170, 214.8, -30, 0, -175, -165, -25, -15},
    {"Tropical Pacific (low resolution)", "trop_paci_lowres", 130, 250,
     -40, 40, -170, -150, -10, 10},
};

double EastLongitude(double longitude) {
  return longitude < 0 ? longitude + 360 : longitude;
}

bool RegionCoversArea(const CurrentRegion& region, double west, double east,
                      double south, double north) {
  if (south < region.south || north > region.north) return false;
  const double areaWest = EastLongitude(west);
  double areaEast = EastLongitude(east);
  if (areaEast < areaWest) areaEast += 360;
  for (double shift : {-360.0, 0.0, 360.0}) {
    if (areaWest + shift >= region.west && areaEast + shift <= region.east)
      return true;
  }
  return false;
}

wxString FormatHour(int hour) { return wxString::Format("%03d", hour); }

wxString GribFilterUrl(const wxString& cycle, const wxString& date,
                       int forecastHour, double west, double east,
                       double south, double north, bool wave) {
  const wxString endpoint = wave ? "filter_gfswave.pl" : "filter_gfs_0p25.pl";
  wxString directory = "/gfs." + date + "/" + cycle +
      (wave ? "/wave/gridded" : "/atmos");
  directory.Replace("/", "%2F");
  const wxString file = wave
      ? "gfswave.t" + cycle + "z.global.0p25.f" + FormatHour(forecastHour) + ".grib2"
      : "gfs.t" + cycle + "z.pgrb2.0p25.f" + FormatHour(forecastHour);
  wxString url = "https://nomads.ncep.noaa.gov/cgi-bin/" + endpoint +
      "?file=" + file + "&dir=" + directory +
      wxString::Format("&subregion=&leftlon=%.4f&rightlon=%.4f&toplat=%.4f&bottomlat=%.4f",
                       west, east, north, south);
  if (wave) {
    url += "&var_HTSGW=on&var_PERPW=on&var_DIRPW=on&lev_surface=on";
  } else {
    url += "&var_UGRD=on&var_VGRD=on&var_PRES=on&var_TMP=on"
           "&lev_10_m_above_ground=on&lev_mean_sea_level=on&lev_2_m_above_ground=on";
  }
  return url;
}

bool ReadExact(std::ifstream& input, char* bytes, std::size_t size) {
  return static_cast<bool>(input.read(bytes, size));
}

std::uint64_t BigEndian(const unsigned char* bytes, std::size_t count) {
  std::uint64_t result = 0;
  for (std::size_t i = 0; i < count; ++i) result = (result << 8) | bytes[i];
  return result;
}

bool AppendGrib(const wxString& path, std::ofstream& output,
                std::size_t* messages, wxString* error,
                std::array<unsigned, 3>* fieldMasks = nullptr,
                int currentStartHour = -1, int currentEndHour = -1) {
  std::ifstream input(std::string(path.ToUTF8().data()),
                      std::ios::binary | std::ios::ate);
  if (!input) {
    *error = _("Cannot open input: ") + path;
    return false;
  }
  const auto end = input.tellg();
  if (end <= 0) {
    *error = _("Input GRIB is empty: ") + path;
    return false;
  }
  input.seekg(0);
  std::uint64_t offset = 0;
  const auto fileSize = static_cast<std::uint64_t>(end);
  std::array<char, 65536> chunk{};
  while (offset < fileSize) {
    unsigned char header[16]{};
    const auto remaining = fileSize - offset;
    if (remaining < 12 || !ReadExact(input, reinterpret_cast<char*>(header),
                                    std::min<std::uint64_t>(16, remaining)) ||
        std::memcmp(header, "GRIB", 4) != 0) {
      *error = wxString::Format(_("Invalid GRIB header at byte %llu"),
                                static_cast<unsigned long long>(offset)) +
               " in " + path;
      return false;
    }
    const auto edition = header[7];
    const std::uint64_t length = edition == 1
        ? BigEndian(header + 4, 3)
        : edition == 2 ? BigEndian(header + 8, 8) : 0;
    if (length < (edition == 2 ? 16u : 12u) || length > remaining ||
        length > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
      *error = wxString::Format(_("Invalid GRIB message length at byte %llu"),
                                static_cast<unsigned long long>(offset)) +
               " in " + path;
      return false;
    }
    bool include = currentStartHour < 0;
    if (currentStartHour >= 0 && edition != 2) {
      *error = _("RTOFS current response is not GRIB2.");
      return false;
    }
    if (edition == 2 && fieldMasks) {
      std::uint64_t sectionOffset = 16;
      while (sectionOffset + 22 <= length - 4) {
        input.seekg(static_cast<std::streamoff>(offset + sectionOffset));
        unsigned char section[22]{};
        if (!ReadExact(input, reinterpret_cast<char*>(section), 22)) break;
        const auto sectionLength = BigEndian(section, 4);
        if (sectionLength < 5 || sectionLength > length - sectionOffset - 4)
          break;
        if (section[4] == 4 && sectionLength >= 11) {
          const auto discipline = header[6];
          const auto category = section[9];
          const auto number = section[10];
          if (currentStartHour >= 0 && sectionLength >= 22 &&
              discipline == 10 && category == 1 &&
              (number == 2 || number == 3) && section[17] == 1) {
            const auto step = BigEndian(section + 18, 4);
            include = step >= static_cast<unsigned>(currentStartHour) &&
                      step <= static_cast<unsigned>(currentEndHour);
          }
          if (include && discipline == 0 && category == 2 &&
              (number == 2 || number == 3))
            (*fieldMasks)[0] |= number == 2 ? 1u : 2u;
          if (include && discipline == 10 && category == 0 && number == 3)
            (*fieldMasks)[1] = 1;
          if (include && discipline == 10 && category == 1 &&
              (number == 2 || number == 3))
            (*fieldMasks)[2] |= number == 2 ? 1u : 2u;
          break;
        }
        sectionOffset += sectionLength;
      }
    }
    input.seekg(static_cast<std::streamoff>(offset + length - 4));
    char terminator[4]{};
    if (!ReadExact(input, terminator, 4) ||
        std::memcmp(terminator, "7777", 4) != 0) {
      *error = wxString::Format(_("Missing GRIB terminator at byte %llu"),
                                static_cast<unsigned long long>(offset)) +
               " in " + path;
      return false;
    }
    if (include) {
      input.seekg(static_cast<std::streamoff>(offset));
      auto left = length;
      while (left) {
        const auto count = static_cast<std::size_t>(
            std::min<std::uint64_t>(left, chunk.size()));
        if (!ReadExact(input, chunk.data(), count) ||
            !output.write(chunk.data(), count)) {
          *error = _("Could not copy GRIB data from ") + path;
          return false;
        }
        left -= count;
      }
      ++*messages;
    }
    offset += length;
  }
  return true;
}

}  // namespace

AndroidGribGeneratorDialog::AndroidGribGeneratorDialog(
    wxWindow* parent, GribReadyCallback onGribReady)
    : wxDialog(parent, wxID_ANY, _("Generate GRIB"), wxDefaultPosition,
               wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_onGribReady(std::move(onGribReady)) {
  auto* content = new wxBoxSizer(wxVERTICAL);
  m_notebook = new wxNotebook(this, wxID_ANY);
  auto* forecast = new wxPanel(m_notebook);
  auto* forecastSizer = new wxBoxSizer(wxVERTICAL);
  auto* forecastIntro = new wxStaticText(
      forecast, wxID_ANY,
      _("NOAA GFS weather and waves for the area below.\n"
        "RTOFS currents cover selected regions only."));
  forecastIntro->SetMinSize(wxSize(-1, 68));
  forecastSizer->Add(forecastIntro, 0, wxEXPAND | wxALL, 10);

  auto* bounds = new wxFlexGridSizer(2, 4, 8, 8);
  bounds->AddGrowableCol(1);
  bounds->AddGrowableCol(3);
  auto addBound = [&](const wxString& label, const wxString& value,
                      wxTextCtrl** target) {
    bounds->Add(new wxStaticText(forecast, wxID_ANY, label), 0,
                wxALIGN_CENTER_VERTICAL);
    *target = new wxTextCtrl(forecast, wxID_ANY, value);
    bounds->Add(*target, 1, wxEXPAND);
  };
  addBound("W", "-8", &m_west);
  addBound("E", "-2", &m_east);
  addBound("S", "50", &m_south);
  addBound("N", "56", &m_north);
  forecastSizer->Add(bounds, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  auto* duration = new wxBoxSizer(wxHORIZONTAL);
  auto* hoursLabel = new wxStaticText(forecast, wxID_ANY, _("Hours"));
  hoursLabel->SetMinSize(wxSize(85, 45));
  duration->Add(hoursLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
  m_hours = new wxChoice(forecast, wxID_ANY);
  for (const char* hours : {"6", "12", "24", "48"}) m_hours->Append(hours);
  m_hours->SetSelection(2);
  duration->Add(m_hours, 1, wxEXPAND);
  forecastSizer->Add(duration, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

  m_waves = new wxCheckBox(forecast, wxID_ANY, _("GFS waves"));
  m_waves->SetValue(true);
  m_waves->SetMinSize(wxSize(-1, 45));
  forecastSizer->Add(m_waves, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
  m_currents = new wxCheckBox(forecast, wxID_ANY, _("RTOFS ocean currents"));
  m_currents->SetMinSize(wxSize(-1, 45));
  forecastSizer->Add(m_currents, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
  m_currentRegion = new wxChoice(forecast, wxID_ANY);
  for (const auto& region : kCurrentRegions) m_currentRegion->Append(region.label);
  m_currentRegion->SetSelection(0);
  m_currentRegion->Enable(false);
  auto* regionRow = new wxBoxSizer(wxHORIZONTAL);
  regionRow->Add(m_currentRegion, 1, wxEXPAND | wxRIGHT, 8);
  auto* useRegionArea = new wxButton(forecast, wxID_ANY, _("Use area"));
  useRegionArea->SetMinSize(wxSize(190, 48));
  useRegionArea->Enable(false);
  useRegionArea->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
    const auto& region = kCurrentRegions[m_currentRegion->GetSelection()];
    m_west->SetValue(wxString::Format("%g", region.exampleWest));
    m_east->SetValue(wxString::Format("%g", region.exampleEast));
    m_south->SetValue(wxString::Format("%g", region.exampleSouth));
    m_north->SetValue(wxString::Format("%g", region.exampleNorth));
  });
  regionRow->Add(useRegionArea, 0);
  m_currents->Bind(wxEVT_CHECKBOX, [this, useRegionArea](wxCommandEvent&) {
    m_currentRegion->Enable(m_currents->GetValue());
    useRegionArea->Enable(m_currents->GetValue());
  });
  forecastSizer->Add(regionRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  auto* regionNote = new wxStaticText(
      forecast, wxID_ANY,
      _("Currents apply only inside the selected region."));
  regionNote->SetMinSize(wxSize(-1, 45));
  forecastSizer->Add(regionNote, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  forecast->SetSizer(forecastSizer);
  m_notebook->AddPage(forecast, _("On-tablet forecast"), true);

  auto* local = new wxPanel(m_notebook);
  auto* localSizer = new wxBoxSizer(wxVERTICAL);
  auto* localIntro = new wxStaticText(
      local, wxID_ANY,
      _("Combine GRIB files already on this tablet. Each file must contain "
        "complete, uncompressed GRIB messages."));
  localIntro->Wrap(670);
  localSizer->Add(localIntro, 0, wxEXPAND | wxALL, 10);

  const wxString labels[] = {_("Weather GRIB"), _("Wave GRIB"),
                             _("Current GRIB")};
  for (size_t index = 0; index < 3; ++index) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* pick = new wxButton(local, wxID_ANY, labels[index]);
    pick->SetMinSize(wxSize(180, 56));
    pick->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent&) {
      ChooseInput(index);
    });
    row->Add(pick, 0, wxRIGHT, 8);
    m_pathDisplays[index] = new wxTextCtrl(
        local, wxID_ANY, _("No file selected"), wxDefaultPosition,
        wxDefaultSize, wxTE_READONLY);
    row->Add(m_pathDisplays[index], 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    localSizer->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);
  }
  local->SetSizer(localSizer);
  m_notebook->AddPage(local, _("Combine local files"), false);
  m_notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxBookCtrlEvent& event) {
    m_forecastMode = event.GetSelection() == 0;
    event.Skip();
  });
  content->Add(m_notebook, 1, wxEXPAND | wxALL, 8);

  content->Add(new wxStaticText(this, wxID_ANY, _("Output filename")), 0,
               wxLEFT | wxRIGHT, 12);
  m_outputName = new wxTextCtrl(
      this, wxID_ANY,
      "xgrib_noaa_" + wxDateTime::Now().ToUTC().Format("%Y%m%d_%H%M%S") +
          ".grb2");
  content->Add(m_outputName, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  m_status = new wxTextCtrl(this, wxID_ANY,
      _("The new file will be saved in xGRIB's generated folder."),
      wxDefaultPosition, wxSize(-1, 110), wxTE_MULTILINE | wxTE_READONLY);
  content->Add(m_status, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  auto* actions = new wxBoxSizer(wxHORIZONTAL);
  m_generate = new wxButton(this, wxID_ANY, _("Generate and open"));
  m_generate->SetMinSize(wxSize(220, 56));
  m_generate->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Generate(); });
  actions->Add(m_generate, 1, wxRIGHT, 8);
  m_close = new wxButton(this, wxID_CANCEL, _("Close"));
  m_close->SetMinSize(wxSize(140, 56));
  m_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { CloseOrCancel(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { CloseOrCancel(); });
  actions->Add(m_close, 0);
  content->Add(actions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  SetSizer(content);
  SetMinSize(wxSize(760, 800));
  SetSize(wxSize(760, 800));
  CentreOnParent();
}

AndroidGribGeneratorDialog::~AndroidGribGeneratorDialog() {
  CleanDownloads();
}

void AndroidGribGeneratorDialog::ChooseInput(size_t index) {
  wxString path;
  if (PlatformFileSelectorDialog(this, &path, _("Select a GRIB file"),
                                 "/storage/emulated/0/Download", "", "*.*") !=
          wxID_OK || path.empty())
    return;
  if (!wxFileExists(path)) {
    m_status->SetValue(_("Cannot read selected file: ") + path);
    return;
  }
  m_paths[index] = path;
  m_pathDisplays[index]->SetValue(path);
}

void AndroidGribGeneratorDialog::Generate() {
  if (m_forecastMode) {
    GenerateForecast();
    return;
  }
  std::vector<wxString> inputs;
  for (const auto& path : m_paths) {
    if (!path.empty() &&
        std::find(inputs.begin(), inputs.end(), path) == inputs.end())
      inputs.push_back(path);
  }
  if (inputs.empty()) {
    m_status->SetValue(_("Select at least one GRIB file first."));
    return;
  }
  if (!PrepareOutput()) return;
  m_generate->Enable(false);
  m_status->SetValue(_("Combining and checking GRIB messages..."));
  wxString error;
  const bool success = Assemble(inputs, false, &error);
  m_generate->Enable(true);
  if (!success) m_status->SetValue(error);
}

bool AndroidGribGeneratorDialog::PrepareOutput() {
  const wxString name = m_outputName->GetValue();
  if (name.empty() || name.Contains("/") || name.Contains("\\") ||
      !(name.Lower().EndsWith(".grb") ||
        name.Lower().EndsWith(".grib") ||
        name.Lower().EndsWith(".grb2"))) {
    m_status->SetValue(_("Use a GRIB filename without folders."));
    return false;
  }
  wxFileName output(GetPluginDataDir("xgrib_pi"), name);
  output.AppendDir("generated");
  if (!output.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    m_status->SetValue(_("Could not create the generated GRIB folder."));
    return false;
  }
  if (output.FileExists()) {
    m_status->SetValue(_("A file with this name already exists. Change the filename."));
    return false;
  }
  m_outputPath = output.GetFullPath();
  return true;
}

bool AndroidGribGeneratorDialog::Assemble(const std::vector<wxString>& inputs,
                                          bool verifyProviders,
                                          wxString* error) {
  const wxString temporary = m_outputPath + ".part";
  wxRemoveFile(temporary);
  std::size_t messages = 0;
  std::array<unsigned, 3> fieldMasks{};
  bool success = false;
  {
    std::ofstream stream(std::string(temporary.ToUTF8().data()),
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
      *error = _("Could not create output file.");
    } else {
      success = true;
      for (const auto& input : inputs) {
        const bool isCurrent = verifyProviders &&
            wxFileName(input).GetFullName().StartsWith("currents_");
        if (!AppendGrib(input, stream, &messages, error,
                        verifyProviders ? &fieldMasks : nullptr,
                        isCurrent ? m_currentStartHour : -1,
                        isCurrent ? m_currentEndHour : -1)) {
          success = false;
          break;
        }
      }
      stream.close();
      if (!stream && success) {
        *error = _("Could not finish writing the output file.");
        success = false;
      }
    }
  }
  if (success && verifyProviders &&
      (fieldMasks[0] != 3 || (m_waves->GetValue() && fieldMasks[1] == 0) ||
       (m_currents->GetValue() && fieldMasks[2] != 3))) {
    *error = _("A NOAA response is missing requested weather, wave or ocean "
               "current fields. Try another model cycle or region.");
    success = false;
  }
  if (success) success = wxRenameFile(temporary, m_outputPath, false);
  if (!success) {
    wxRemoveFile(temporary);
    if (error->empty()) *error = _("Could not save the generated GRIB.");
    return false;
  }
  m_status->SetValue(wxString::Format(
      _("Created %zu GRIB messages in "), messages) + wxFileName(m_outputPath).GetFullName() +
      _(". Saved in xGRIB's generated folder."));
  if (m_onGribReady) m_onGribReady(m_outputPath);
  return true;
}

void AndroidGribGeneratorDialog::GenerateForecast() {
  double west, east, south, north;
  if (!m_west->GetValue().ToDouble(&west) ||
      !m_east->GetValue().ToDouble(&east) ||
      !m_south->GetValue().ToDouble(&south) ||
      !m_north->GetValue().ToDouble(&north) ||
      west < -180 || east > 180 || west >= east || south < -90 || north > 90 ||
      south >= north || east - west > 60 || north - south > 40) {
    m_status->SetValue(_("Enter a valid area (west < east, south < north; "
                         "maximum 60° by 40°)."));
    return;
  }
  if (m_currents->GetValue() &&
      !RegionCoversArea(kCurrentRegions[m_currentRegion->GetSelection()],
                        west, east, south, north)) {
    m_status->SetValue(_("The selected current region does not cover the "
                         "forecast area. Adjust the bounds or tap Use area."));
    return;
  }
  if (!PrepareOutput()) return;
  const int hours = wxAtoi(m_hours->GetStringSelection());
  wxDateTime now = wxDateTime::Now().ToUTC();
  wxDateTime gfsTime = now - wxTimeSpan::Hours(6);
  gfsTime.SetHour((gfsTime.GetHour() / 6) * 6);
  gfsTime.SetMinute(0);
  gfsTime.SetSecond(0);
  gfsTime.SetMillisecond(0);
  wxDateTime startTime = now;
  startTime.SetMinute(0);
  startTime.SetSecond(0);
  startTime.SetMillisecond(0);
  if (now.GetMinute() || now.GetSecond())
    startTime += wxTimeSpan::Hours(1);
  const int untilThreeHourBoundary = (3 - startTime.GetHour() % 3) % 3;
  startTime += wxTimeSpan::Hours(untilThreeHourBoundary);
  const int firstWeatherLead =
      (startTime - gfsTime).GetHours();
  const wxString date = gfsTime.Format("%Y%m%d");
  const wxString cycle = wxString::Format("%02d", gfsTime.GetHour());
  wxDateTime rtofsTime = now.GetHour() >= 12
      ? now : now - wxTimeSpan::Days(1);
  rtofsTime.SetHour(0);
  rtofsTime.SetMinute(0);
  rtofsTime.SetSecond(0);
  rtofsTime.SetMillisecond(0);
  const wxString rtofsDate = rtofsTime.Format("%Y%m%d");
  m_currentStartHour = (startTime - rtofsTime).GetHours();
  m_currentEndHour = m_currentStartHour + hours;
  if (m_currents->GetValue() &&
      (m_currentStartHour < 1 || m_currentEndHour > 72)) {
    m_status->SetValue(_("RTOFS does not cover this full forecast window. "
                         "Choose a shorter range."));
    return;
  }

  m_workDir = m_outputPath + ".downloads";
  if (!wxFileName::Mkdir(m_workDir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    m_status->SetValue(_("Could not create a forecast download folder."));
    return;
  }
  m_downloads.clear();
  for (int hour = firstWeatherLead;
       hour <= firstWeatherLead + hours; hour += 3) {
    const wxString lead = FormatHour(hour);
    m_downloads.push_back({
        GribFilterUrl(cycle, date, hour, west, east, south, north, false),
        "GFS weather +" + lead + "h",
        m_workDir + "/weather_" + lead + ".grb2"});
    if (m_waves->GetValue())
      m_downloads.push_back({
          GribFilterUrl(cycle, date, hour, west, east, south, north, true),
          "GFS waves +" + lead + "h",
          m_workDir + "/waves_" + lead + ".grb2"});
  }
  if (m_currents->GetValue()) {
    const auto& region = kCurrentRegions[m_currentRegion->GetSelection()];
    const int firstSegment =
        ((std::max(m_currentStartHour, 1) - 1) / 24 + 1) * 24;
    const int lastSegment = ((m_currentEndHour + 23) / 24) * 24;
    for (int day = firstSegment; day <= lastSegment; day += 24) {
      const wxString lead = FormatHour(day);
      const wxString url = "https://nomads.ncep.noaa.gov/pub/data/nccf/com/"
          "rtofs/prod/rtofs." + rtofsDate + "/rtofs_glo.t00z.f" + lead +
          "_" + region.id + "_std.grb2";
      m_downloads.push_back({url,
          wxString("RTOFS currents: ") + region.label + " +" + lead + "h",
          m_workDir + "/currents_" + lead + ".grb2"});
    }
  }
  m_downloadIndex = 0;
  m_canceling = false;
  m_generate->Enable(false);
  m_close->SetLabel(_("Cancel"));
  StartNextDownload();
}

void AndroidGribGeneratorDialog::StartNextDownload() {
  m_downloading = true;
  while (m_downloadIndex < m_downloads.size()) {
    const auto& item = m_downloads[m_downloadIndex];
    m_status->SetValue(wxString::Format(_("Downloading %zu/%zu: "),
        m_downloadIndex + 1, m_downloads.size()) + item.label);
    wxYieldIfNeeded();
    if (m_canceling) {
      StopForecast(_("Forecast generation cancelled."));
      return;
    }
    const auto status = OCPN_downloadFile(
        item.url, item.path, _("NOAA forecast"), item.label, wxNullBitmap,
        this, 0, 45);
    if (m_canceling) {
      StopForecast(_("Forecast generation cancelled."));
      return;
    }
    if (status != OCPN_DL_NO_ERROR || !wxFileExists(item.path)) {
      StopForecast(_("Download failed: ") + item.label);
      return;
    }
    ++m_downloadIndex;
    // NOMADS asks clients to pause between GRIB Filter requests. Its direct
    // RTOFS files do not use that filter endpoint.
    if (item.url.Contains("/cgi-bin/filter_") &&
        m_downloadIndex < m_downloads.size() &&
        m_downloads[m_downloadIndex].url.Contains("/cgi-bin/filter_")) {
      for (int remaining = 10; remaining > 0; --remaining) {
        m_status->SetValue(wxString::Format(
            _("Waiting %d seconds before the next NOAA filter request..."),
            remaining));
        wxYieldIfNeeded();
        if (m_canceling) {
          StopForecast(_("Forecast generation cancelled."));
          return;
        }
        wxMilliSleep(1000);
      }
    }
  }
  FinishForecast();
}

void AndroidGribGeneratorDialog::FinishForecast() {
  m_downloading = false;
  std::vector<wxString> inputs;
  for (const auto& item : m_downloads) inputs.push_back(item.path);
  m_status->SetValue(_("Checking and assembling the downloaded GRIB messages..."));
  wxString error;
  const bool success = Assemble(inputs, true, &error);
  CleanDownloads();
  m_generate->Enable(true);
  m_close->SetLabel(_("Close"));
  if (!success) m_status->SetValue(error);
}

void AndroidGribGeneratorDialog::StopForecast(const wxString& error) {
  m_downloading = false;
  CleanDownloads();
  m_status->SetValue(error);
  m_generate->Enable(true);
  m_close->SetLabel(_("Close"));
}

void AndroidGribGeneratorDialog::CloseOrCancel() {
  if (m_downloading) {
    m_canceling = true;
    m_status->SetValue(_("Stopping after this download..."));
  } else {
    Hide();
  }
}

void AndroidGribGeneratorDialog::CleanDownloads() {
  if (!m_workDir.empty()) {
    wxFileName::Rmdir(m_workDir, wxPATH_RMDIR_RECURSIVE);
    m_workDir.clear();
  }
  m_downloads.clear();
}
