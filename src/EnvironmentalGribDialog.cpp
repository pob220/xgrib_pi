#include "EnvironmentalGribDialog.h"

#include "XgribPaths.h"

#include <wx/config.h>
#include <wx/datetime.h>
#include <wx/dir.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/process.h>
#include <wx/scrolwin.h>
#include <wx/stdpaths.h>
#include <wx/stream.h>
#include <wx/utils.h>
#include <wx/file.h>

#include "jsonval.h"
#include "jsonreader.h"
#include "jsonwriter.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <vector>

#ifdef __UNIX__
#include <signal.h>
#endif

namespace {

wxString ShellQuote(const wxString& value) {
  wxString escaped(value);
  escaped.Replace("'", "'\\''");
  return "'" + escaped + "'";
}

wxString DefaultOutputDirectory() {
  wxFileName path(wxStandardPaths::Get().GetUserDataDir(), "");
  path.AppendDir("grib");
  path.AppendDir("generated");
  return path.GetPath();
}

wxString TimestampedFilename(const wxString& prefix) {
  return prefix + "_" + wxDateTime::Now().ToUTC().Format("%Y%m%d_%H%M") + ".grb";
}

wxString DefaultStartUtc() {
  wxDateTime now = wxDateTime::Now().ToUTC();
  now.SetMinute(0);
  now.SetSecond(0);
  now.SetMillisecond(0);
  return now.FormatISOCombined('T') + "Z";
}

bool IsMarineIeProvider(const wxString& provider) {
  return provider.Contains("Marine Institute Ireland");
}

wxString MarineIeOutputFilename() {
  return TimestampedFilename("marine_ie_irish_sea_current");
}

wxString DefaultTpxoOutputFilename() {
  return TimestampedFilename("tpxo10_astronomical_tide_current");
}

wxString IrishSeaTpxoOutputFilename() {
  return TimestampedFilename("tpxo10_irish_sea_astronomical_tide_current");
}

wxString DefaultTpxoModelDirectory() {
  wxFileName path(wxGetHomeDir(), "");
  path.AppendDir("OpenCPN");
  path.AppendDir("tide-models");
  return path.GetPath();
}

wxString DefaultTpxoCacheFile() {
  wxFileName path(DefaultOutputDirectory(), "");
  path.AppendDir("tpxo-cache");
  path.SetFullName(TimestampedFilename("tpxo10_astronomical_tide_current_cache"));
  path.SetExt("tpxocache");
  return path.GetFullPath();
}

wxString ResolveTpxoAtlasDirectory(const wxString& selected) {
  const auto isAtlas = [](const wxString& directory) {
    wxFileName grid(directory, "grid_tpxo10atlas_v2.nc");
    wxDir model(directory);
    wxString constituent;
    return grid.FileExists() && model.IsOpened() &&
           model.GetFirst(&constituent, "u_*_tpxo10_atlas_30_v2.nc",
                          wxDIR_FILES);
  };
  if (isAtlas(selected)) return selected;
  wxFileName nested(selected, "");
  nested.AppendDir("TPXO10_atlas_v2");
  return isAtlas(nested.GetPath()) ? nested.GetPath() : wxString{};
}

wxString JsonEscape(const wxString& value) {
  wxString escaped;
  for (wxUniChar ch : value) {
    if (ch == '\\') {
      escaped += "\\\\";
    } else if (ch == '"') {
      escaped += "\\\"";
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (ch == '\r') {
      escaped += "\\r";
    } else if (ch == '\t') {
      escaped += "\\t";
    } else {
      escaped += ch;
    }
  }
  return escaped;
}

bool IsExecutableFile(const wxString& path) {
  return wxFileName::FileExists(path);
}

bool ParseDouble(const wxTextCtrl* control, double* value) {
  return control && control->GetValue().ToDouble(value);
}

bool GribStreamIsStrictlyValid(const wxString& path, wxString* details) {
  wxFile file(path);
  if (!file.IsOpened()) {
    if (details) *details = "could not open output file";
    return false;
  }
  wxFileOffset size = file.Length();
  if (size <= 0) {
    if (details) *details = "output file is empty";
    return false;
  }
  std::vector<unsigned char> buffer(static_cast<size_t>(size));
  wxFileOffset read = file.Read(buffer.data(), buffer.size());
  if (read != size) {
    if (details) *details = "could not read complete output file";
    return false;
  }
  const unsigned char* bytes = buffer.data();
  size_t offset = 0;
  int messages = 0;
  const size_t total = static_cast<size_t>(size);
  while (offset < total) {
    if (offset + 12 > total || bytes[offset] != 'G' || bytes[offset + 1] != 'R' ||
        bytes[offset + 2] != 'I' || bytes[offset + 3] != 'B') {
      if (details) *details = wxString::Format("GRIB marker not found at byte offset %zu", offset);
      return false;
    }
    int edition = bytes[offset + 7];
    size_t messageLength = 0;
    if (edition == 1) {
      messageLength = (static_cast<size_t>(bytes[offset + 4]) << 16) |
                      (static_cast<size_t>(bytes[offset + 5]) << 8) |
                      static_cast<size_t>(bytes[offset + 6]);
    } else if (edition == 2) {
      if (offset + 16 > total) {
        if (details) *details = "truncated GRIB2 header";
        return false;
      }
      for (int i = 8; i < 16; ++i) {
        messageLength = (messageLength << 8) | static_cast<size_t>(bytes[offset + i]);
      }
    } else {
      if (details) *details = wxString::Format("unsupported GRIB edition %d", edition);
      return false;
    }
    if (messageLength < 12 || offset + messageLength > total) {
      if (details) *details = wxString::Format("truncated GRIB message at byte offset %zu", offset);
      return false;
    }
    size_t end = offset + messageLength - 4;
    if (bytes[end] != '7' || bytes[end + 1] != '7' || bytes[end + 2] != '7' || bytes[end + 3] != '7') {
      if (details) *details = wxString::Format("GRIB terminator not found at byte offset %zu", end);
      return false;
    }
    ++messages;
    offset += messageLength;
  }
  if (messages == 0) {
    if (details) *details = "no GRIB messages found";
    return false;
  }
  if (details) *details = wxString::Format("validated GRIB stream: %d messages", messages);
  return true;
}

void RedactQueryParameter(wxString* text, const wxString& name) {
  wxString lower = text->Lower();
  wxString needle1 = "?" + name.Lower() + "=";
  wxString needle2 = "&" + name.Lower() + "=";
  for (const auto& needle : {needle1, needle2}) {
    size_t position = lower.find(needle);
    while (position != wxString::npos) {
      size_t value_start = position + needle.Length();
      size_t value_end = value_start;
      while (value_end < text->Length() &&
             (*text)[value_end] != '&' && (*text)[value_end] != '#' &&
             !wxIsspace((*text)[value_end])) {
        ++value_end;
      }
      text->replace(value_start, value_end - value_start, "<redacted>");
      lower = text->Lower();
      position = lower.find(needle, value_start + 10);
    }
  }
}

}  // namespace

EnvironmentalGribDialog::EnvironmentalGribDialog(
    wxWindow* parent, GribReadyCallback onGribReady)
    : wxDialog(parent, wxID_ANY, "Environmental GRIB Generator", wxDefaultPosition,
               wxSize(880, 760), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      m_onGribReady(std::move(onGribReady)) {
  auto* top = new wxBoxSizer(wxVERTICAL);
  m_scrolled = new wxScrolledWindow(
      this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
      wxVSCROLL | wxALWAYS_SHOW_SB | wxTAB_TRAVERSAL);
  m_scrolled->SetScrollRate(0, 12);
  m_scrolled->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_ALWAYS);
  m_scrolled->SetMinSize(wxSize(760, 330));
  auto* scrolled = m_scrolled;
  auto* form = new wxBoxSizer(wxVERTICAL);
  auto* grid = new wxFlexGridSizer(2, 8, 8);
  grid->AddGrowableCol(1, 1);

  m_generatorPath = new wxTextCtrl(scrolled, wxID_ANY, FindDefaultGenerator());
  m_west = new wxTextCtrl(scrolled, wxID_ANY, "-8.5");
  m_south = new wxTextCtrl(scrolled, wxID_ANY, "50.5");
  m_east = new wxTextCtrl(scrolled, wxID_ANY, "-2.5");
  m_north = new wxTextCtrl(scrolled, wxID_ANY, "56.5");
  wxString presets[] = {"Custom bbox", "Current chart area", "Irish Sea / North Channel",
                        "Western English Channel", "North Sea", "Bay of Biscay",
                        "Gulf Stream / Florida Straits", "US East Coast / Gulf Stream",
                        "Caribbean"};
  m_presetChoice = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, WXSIZEOF(presets), presets);
  m_presetChoice->SetSelection(0);
  m_startUtc = new wxTextCtrl(scrolled, wxID_ANY, DefaultStartUtc());
  m_durationHours = new wxSpinCtrl(scrolled, wxID_ANY);
  m_durationHours->SetRange(1, 240);
  m_durationHours->SetValue(72);
  m_stepHours = new wxSpinCtrl(scrolled, wxID_ANY);
  m_stepHours->SetRange(1, 24);
  m_stepHours->SetValue(1);

  m_generateWeather = new wxCheckBox(scrolled, wxID_ANY, "Generate/include weather");
  m_generateWeather->SetValue(true);
  wxString weatherProviders[] = {"NOAA GFS forecast",
                                 "NOAA HRRR 3 km forecast",
                                 "Met Office UKV 2 km forecast",
                                 "DWD ICON-EU 13 km forecast",
                                 "ECMWF IFS Open Data forecast",
                                 "ECMWF AIFS Open Data forecast (experimental)",
                                 "Existing weather GRIB file",
                                 "None"};
  m_weatherProvider = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                   WXSIZEOF(weatherProviders), weatherProviders);
  m_weatherProvider->SetSelection(0);
  wxString weatherPresets[] = {"Minimal wind", "Routing", "Marine comfort"};
  m_weatherPreset = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 WXSIZEOF(weatherPresets), weatherPresets);
  m_weatherPreset->SetSelection(1);
  m_weatherPreset->SetToolTip("Minimal: wind only. Routing: wind, pressure, and air temperature. Marine: routing fields plus gusts, precipitation, cloud cover, and optional waves.");
  m_includeWaves = new wxCheckBox(scrolled, wxID_ANY, "Include wave fields");
  m_includeWaves->SetToolTip("Adds significant wave height, primary wave period, and primary wave direction. NOAA GFS Wave requires no account; Copernicus Global Waves requires a Copernicus account.");
  wxString waveProviders[] = {"NOAA GFS Wave", "Copernicus Marine Global Waves"};
  m_waveProvider = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                WXSIZEOF(waveProviders), waveProviders);
  m_waveProvider->SetSelection(0);
  m_waveProvider->SetToolTip("NOAA GFS Wave requires no account. Copernicus Global Waves requires Copernicus Marine credentials and uses 3-hourly global wave fields.");
  m_existingWeatherFile = new wxFilePickerCtrl(scrolled, wxID_ANY, "", "Select weather GRIB", "*.grb;*.grb2");

  m_generateCurrents = new wxCheckBox(scrolled, wxID_ANY, "Generate/include currents");
  m_generateCurrents->SetValue(true);
  wxString currentSources[] = {"None",
                               "Existing current GRIB file",
                               "TPXO cache",
                               "TPXO direct astronomical tide model",
                               "Marine.ie Irish Sea latest run",
                               "Copernicus NWS forecast/model currents",
                               "Copernicus Global forecast/model currents",
                               "NOAA RTOFS Global ocean currents",
                               "NOAA OFS / S-111 coastal currents (experimental)",
                               "Auto forecast/model current provider"};
  m_currentSource = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                 WXSIZEOF(currentSources), currentSources);
  m_currentSource->SetSelection(2);
  m_existingCurrentFile = new wxFilePickerCtrl(scrolled, wxID_ANY, "", "Select current GRIB", "*.grb;*.grb2");

  wxString modes[] = {"Forecast/model current GRIB",
                      "Tidal stream prediction from local TPXO model",
                      "Local NetCDF file",
                      "Synthetic source"};
  m_mode = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, WXSIZEOF(modes), modes);
  m_mode->SetSelection(0);

  wxString providers[] = {"Auto", "Copernicus Marine North-West Shelf high-resolution currents",
                          "Copernicus Marine Global currents",
                          "Marine Institute Ireland Irish Sea currents, 3 day"};
  m_provider = new wxChoice(scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, WXSIZEOF(providers), providers);
  m_provider->SetSelection(1);
  m_mode->Hide();
  m_provider->Hide();
  m_mode->SetSize(0, 0);
  m_provider->SetSize(0, 0);
  m_username = new wxTextCtrl(scrolled, wxID_ANY);
  m_password = new wxTextCtrl(scrolled, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
  m_rememberUsername = new wxCheckBox(scrolled, wxID_ANY, "Remember username");
  m_providerNote = new wxStaticText(scrolled, wxID_ANY, "");
  m_tpxoModelDir = new wxDirPickerCtrl(scrolled, wxID_ANY, DefaultTpxoModelDirectory());
  m_tpxoModelName = new wxTextCtrl(scrolled, wxID_ANY, "TPXO10-atlas-v2-nc");
  m_tpxoGridSpacing = new wxTextCtrl(scrolled, wxID_ANY, "0.05");
  m_checkTpxoButton = new wxButton(scrolled, wxID_ANY, "Check TPXO model");
  m_useTpxoCache = new wxCheckBox(scrolled, wxID_ANY, "Use TPXO cache for this area");
  m_tpxoCacheFile = new wxFilePickerCtrl(scrolled, wxID_ANY, DefaultTpxoCacheFile(),
                                         "Select TPXO cache file", "*.tpxocache;*.npz");
  m_prepareTpxoCacheButton = new wxButton(scrolled, wxID_ANY, "Prepare/update cache");
  m_localNetcdf = new wxFilePickerCtrl(scrolled, wxID_ANY, "", "Select NetCDF file", "*.nc;*.nc4");
  m_outputDir = new wxDirPickerCtrl(scrolled, wxID_ANY, DefaultOutputDirectory());
  m_outputFile = new wxTextCtrl(scrolled, wxID_ANY, "");
  auto* outputBrowse = new wxButton(scrolled, wxID_ANY, "Browse...");
  m_openAfter = new wxCheckBox(scrolled, wxID_ANY, "Open generated GRIB after creation");
  m_showMergeInstructions = new wxCheckBox(scrolled, wxID_ANY, "Show final GRIB summary");
  m_showMergeInstructions->SetValue(false);
  m_showMergeInstructions->Hide();
  m_showMergeInstructions->SetSize(0, 0);

  auto addRow = [&](const wxString& label, wxWindow* control) -> wxStaticText* {
    auto* labelControl = new wxStaticText(scrolled, wxID_ANY, label);
    grid->Add(labelControl, 0, wxALIGN_CENTER_VERTICAL);
    grid->Add(control, 1, wxEXPAND);
    return labelControl;
  };
  addRow("Generator executable", m_generatorPath);
  addRow("West longitude", m_west);
  addRow("South latitude", m_south);
  addRow("East longitude", m_east);
  addRow("North latitude", m_north);
  addRow("Area preset", m_presetChoice);
  addRow("Start UTC", m_startUtc);
  addRow("Duration hours", m_durationHours);
  addRow("Step hours", m_stepHours);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Weather"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_generateWeather, 0);
  addRow("Weather provider", m_weatherProvider);
  m_weatherPresetLabel = addRow("Weather preset", m_weatherPreset);
  m_wavesLabel = new wxStaticText(scrolled, wxID_ANY, "Waves");
  grid->Add(m_wavesLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_includeWaves, 0);
  m_waveProviderLabel = addRow("Wave provider", m_waveProvider);
  m_existingWeatherFileLabel = addRow("Existing weather GRIB", m_existingWeatherFile);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Currents"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_generateCurrents, 0);
  addRow("Current source", m_currentSource);
  m_existingCurrentFileLabel = addRow("Existing current GRIB", m_existingCurrentFile);
  m_usernameLabel = addRow("Copernicus username", m_username);
  m_passwordLabel = addRow("Copernicus password", m_password);
  addRow("Provider note", m_providerNote);
  m_tpxoModelDirLabel = addRow("TPXO model directory", m_tpxoModelDir);
  m_tpxoModelNameLabel = addRow("TPXO model name", m_tpxoModelName);
  m_tpxoGridSpacingLabel = addRow("TPXO grid spacing degrees", m_tpxoGridSpacing);
  m_checkTpxoLabel = new wxStaticText(scrolled, wxID_ANY, "TPXO model check");
  grid->Add(m_checkTpxoLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_checkTpxoButton, 0);
  auto* tpxoCacheOptionLabel = new wxStaticText(scrolled, wxID_ANY, "TPXO cache");
  tpxoCacheOptionLabel->Hide();
  m_useTpxoCache->Hide();
  grid->Add(tpxoCacheOptionLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_useTpxoCache, 0);
  m_tpxoCacheFileLabel = addRow("TPXO cache file", m_tpxoCacheFile);
  m_prepareTpxoCacheLabel = new wxStaticText(scrolled, wxID_ANY, "TPXO cache preparation");
  grid->Add(m_prepareTpxoCacheLabel, 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(m_prepareTpxoCacheButton, 0);
  m_localNetcdfLabel = addRow("Local NetCDF", m_localNetcdf);
  addRow("Output directory", m_outputDir);
  auto* outputFileSizer = new wxBoxSizer(wxHORIZONTAL);
  outputFileSizer->Add(m_outputFile, 1, wxEXPAND | wxRIGHT, 8);
  outputFileSizer->Add(outputBrowse, 0);
  grid->Add(new wxStaticText(scrolled, wxID_ANY, "Output filename"), 0, wxALIGN_CENTER_VERTICAL);
  grid->Add(outputFileSizer, 1, wxEXPAND);

  form->Add(grid, 0, wxEXPAND | wxALL, 12);
  form->Add(m_rememberUsername, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  form->Add(m_openAfter, 0, wxLEFT | wxRIGHT | wxBOTTOM, 12);
  scrolled->SetSizer(form);
  scrolled->SetVirtualSize(form->GetMinSize());
  top->Add(scrolled, 1, wxEXPAND);

  m_log = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, 220),
                         wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
  m_log->SetMinSize(wxSize(760, 180));
  top->Add(m_log, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, 12);

  auto* buttons = new wxBoxSizer(wxHORIZONTAL);
  m_checkButton = new wxButton(this, wxID_ANY, "Check Dependencies");
  m_generateButton = new wxButton(this, wxID_OK, "Generate Complete GRIB");
  m_cancelButton = new wxButton(this, wxID_ANY, "Cancel");
  m_closeButton = new wxButton(this, wxID_CANCEL, "Close");
  buttons->Add(m_checkButton, 0, wxRIGHT, 8);
  buttons->AddStretchSpacer(1);
  buttons->Add(m_generateButton, 0, wxRIGHT, 8);
  buttons->Add(m_cancelButton, 0, wxRIGHT, 8);
  buttons->Add(m_closeButton, 0);
  top->Add(buttons, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  SetSizerAndFit(top);
  SetMinSize(wxSize(880, 720));
  SetSize(wxSize(900, 780));
  CentreOnParent();

  m_checkButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnCheckDependencies, this);
  m_generateButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnGenerate, this);
  m_checkTpxoButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnCheckTpxoModel, this);
  m_prepareTpxoCacheButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnPrepareTpxoCache, this);
  outputBrowse->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnBrowseOutput, this);
  m_outputFile->Bind(wxEVT_TEXT, &EnvironmentalGribDialog::OnOutputFilenameChanged, this);
  m_presetChoice->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnPresetChanged, this);
  m_provider->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_mode->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnModeChanged, this);
  m_weatherProvider->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_weatherPreset->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_waveProvider->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_currentSource->Bind(wxEVT_CHOICE, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_generateWeather->Bind(wxEVT_CHECKBOX, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_generateCurrents->Bind(wxEVT_CHECKBOX, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_includeWaves->Bind(wxEVT_CHECKBOX, &EnvironmentalGribDialog::OnProviderChanged, this);
  m_cancelButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnCancel, this);
  m_closeButton->Bind(wxEVT_BUTTON, &EnvironmentalGribDialog::OnClose, this);
  Bind(wxEVT_CLOSE_WINDOW, &EnvironmentalGribDialog::OnDialogClose, this);
  Bind(wxEVT_TIMER, &EnvironmentalGribDialog::OnProcessTimer, this);
  Bind(wxEVT_END_PROCESS, &EnvironmentalGribDialog::OnProcessTerminated, this);

  AppendLog("Generated environmental GRIBs are model data for planning and experimentation, not official navigation products.");
  wxLogMessage("xGRIB environmental generator executable: %s",
               m_generatorPath->GetValue());
  LoadSettings();
  RefreshOutputFilenameDefault();
  UpdateProviderUi();
  SetBusy(false);
}

EnvironmentalGribDialog::~EnvironmentalGribDialog() {
  PrepareForParentShutdown();
}

void EnvironmentalGribDialog::PrepareForParentShutdown() {
  m_processTimer.Stop();
  if (m_processRunning && m_processPid != 0 && ChildProcessStillExists()) {
    wxKillError error = wxKILL_OK;
    wxKill(m_processPid, wxSIGTERM, &error, wxKILL_CHILDREN);
    wxLogMessage("xGRIB: stopped environmental generator pid=%ld during shutdown "
                 "(wxKillError=%d)",
                 m_processPid, static_cast<int>(error));
  }
  if (m_process) {
    m_process->Detach();
    m_process = nullptr;
  }
  m_processRunning = false;
  m_processPid = 0;
}

void EnvironmentalGribDialog::SetCurrentViewPort(const PlugIn_ViewPort& vp) {
  m_currentViewPort = vp;
  m_hasCurrentViewPort = vp.bValid;
}

void EnvironmentalGribDialog::OnCheckDependencies(wxCommandEvent&) {
  wxString command = ShellQuote(m_generatorPath->GetValue()) + " capabilities";
  AppendLog("Checking native generator capabilities...");
  StartCommand(command, "", false);
}

void EnvironmentalGribDialog::OnCheckTpxoModel(wxCommandEvent&) {
  AppendLog("Checking TPXO model...");
  AppendLog("Source: TPXO10 astronomical tide model");
  const wxString atlas = ResolveTpxoAtlasDirectory(m_tpxoModelDir->GetPath());
  if (!atlas.empty()) {
    AppendLog("TPXO10 model is available: " + atlas);
    wxMessageBox("The TPXO10 model grid and constituent files are available.",
                 "TPXO model available", wxOK | wxICON_INFORMATION, this);
  } else {
    const wxString message =
        "TPXO10 model files were not found. Select either the model parent "
        "directory or the TPXO10_atlas_v2 directory: " +
        m_tpxoModelDir->GetPath();
    AppendLog(message);
    wxMessageBox(message, "TPXO model unavailable", wxOK | wxICON_WARNING,
                 this);
  }
}

void EnvironmentalGribDialog::OnPrepareTpxoCache(wxCommandEvent&) {
  wxFileName cachePath(m_tpxoCacheFile->GetPath());
  if (cachePath.GetFullPath().empty()) {
    wxString message = "Choose a TPXO cache file path before preparing the cache.";
    AppendLog(message);
    wxMessageBox(message, "Missing TPXO cache file", wxOK | wxICON_WARNING, this);
    return;
  }
  if (!cachePath.DirExists()) {
    cachePath.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  wxString command = ShellQuote(m_generatorPath->GetValue()) + " prepare-tpxo-cache --bbox " +
                     ShellQuote(m_west->GetValue()) + " " + ShellQuote(m_south->GetValue()) + " " +
                     ShellQuote(m_east->GetValue()) + " " + ShellQuote(m_north->GetValue()) +
                     " --grid-spacing-deg " + ShellQuote(m_tpxoGridSpacing->GetValue()) +
                     " --model-dir " + ShellQuote(m_tpxoModelDir->GetPath()) +
                     " --output " + ShellQuote(cachePath.GetFullPath()) +
                     " --overwrite --verbose";
  AppendLog("Preparing TPXO cache...");
  AppendLog("Source: TPXO10 astronomical tide model");
  AppendLog("TPXO cache files are derived from local licensed TPXO model files. Do not redistribute unless your TPXO licence permits it.");
  SaveSettings();
  StartCommand(command, "", false);
}

void EnvironmentalGribDialog::OnGenerate(wxCommandEvent&) {
  int mode = m_mode->GetSelection();
  wxString provider = m_provider->GetStringSelection();
  wxString weatherProvider = m_weatherProvider->GetStringSelection();
  wxString currentSource = m_currentSource->GetStringSelection();
  if (m_generateWeather->GetValue() && weatherProvider.Contains("Existing") &&
      m_existingWeatherFile->GetPath().empty()) {
    wxString message = "Select an existing weather GRIB file or choose a generated weather provider.";
    AppendLog(message);
    wxMessageBox(message, "Missing weather GRIB", wxOK | wxICON_WARNING, this);
    return;
  }
  if (m_generateCurrents->GetValue() && currentSource.Contains("Existing") &&
      m_existingCurrentFile->GetPath().empty()) {
    wxString message = "Select an existing current GRIB file or choose TPXO cache/None.";
    AppendLog(message);
    wxMessageBox(message, "Missing current GRIB", wxOK | wxICON_WARNING, this);
    return;
  }
  if (m_generateCurrents->GetValue() && currentSource.Contains("TPXO cache")) {
    wxString cachePath = m_tpxoCacheFile->GetPath();
    if (cachePath.empty()) {
      cachePath = DefaultTpxoCacheFile();
      m_tpxoCacheFile->SetPath(cachePath);
    }
    if (ResolveTpxoAtlasDirectory(m_tpxoModelDir->GetPath()).empty()) {
      wxString message =
          "Select a valid TPXO model parent or TPXO10_atlas_v2 directory "
          "before TPXO cache generation.";
      AppendLog(message);
      wxMessageBox(message, "Missing TPXO model", wxOK | wxICON_WARNING, this);
      return;
    }
    if (!wxFileName::FileExists(cachePath)) {
      wxString message =
          "No suitable TPXO cache exists for this area/grid/model. Prepare/update it now? This may take about a minute.";
      int response = wxMessageBox(message, "Prepare TPXO cache", wxYES_NO | wxICON_QUESTION, this);
      if (response != wxYES) {
        AppendLog("Generation cancelled: TPXO cache preparation was declined.");
        return;
      }
      AppendLog("TPXO cache missing; generation will prepare/update it before merging.");
    }
  }
  if (m_generateCurrents->GetValue() && currentSource.Contains("TPXO direct") &&
      ResolveTpxoAtlasDirectory(m_tpxoModelDir->GetPath()).empty()) {
    wxString message =
        "Select a valid TPXO model parent or TPXO10_atlas_v2 directory "
        "before direct TPXO generation.";
    AppendLog(message);
    wxMessageBox(message, "Missing TPXO model", wxOK | wxICON_WARNING, this);
    return;
  }
  bool copernicusForecast = NeedsCopernicusCredentials();
  if (copernicusForecast && (m_username->GetValue().empty() || m_password->GetValue().empty())) {
    wxString message = "Enter your Copernicus Marine username and password for this operation. The password is held in memory only and is not passed on the command line.";
    AppendLog(message);
    wxMessageBox(message, "Missing Copernicus credentials", wxOK | wxICON_WARNING, this);
    return;
  }
  if (copernicusForecast && !ConfirmLargeCopernicusRequest()) {
    AppendLog("Generation cancelled before launch.");
    return;
  }
  if (m_generateWeather->GetValue() && weatherProvider.Contains("Met Office UKV") && !ValidateUkvRequest()) {
    AppendLog("Generation cancelled before launch.");
    return;
  }
  if (m_generateWeather->GetValue() && weatherProvider.Contains("ECMWF") && !ValidateEcmwfRequest()) {
    AppendLog("Generation cancelled before launch.");
    return;
  }
  if (m_generateWeather->GetValue() &&
      (weatherProvider.Contains("HRRR") || weatherProvider.Contains("ICON-EU"))) {
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    bool parsed = m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
                  m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north);
    if (parsed && weatherProvider.Contains("HRRR") &&
        (west < -130.0 || east > -60.0 || south < 20.0 || north > 55.0)) {
      wxString message =
          "The requested bbox is outside the normal NOAA HRRR contiguous United States domain. "
          "Choose a US area or select a global/regional provider that covers this area.";
      AppendLog(message);
      wxMessageBox(message, "HRRR area unavailable", wxOK | wxICON_WARNING, this);
      return;
    }
    if (parsed && weatherProvider.Contains("ICON-EU") &&
        (west < -32.5 || east > 42.5 || south < 20.0 || north > 72.5)) {
      wxString message =
          "The requested bbox is outside the normal DWD ICON-EU Europe domain. "
          "Choose a European area or select a global provider that covers this area.";
      AppendLog(message);
      wxMessageBox(message, "ICON-EU area unavailable", wxOK | wxICON_WARNING, this);
      return;
    }
  }
  if (m_generateWeather->GetValue() && weatherProvider.Contains("AIFS")) {
    AppendLog("ECMWF AIFS Open Data files may be large if the helper cannot spatially crop the request.");
  }
  wxFileName output(OutputPath());
  if (!output.DirExists()) {
    output.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
  }
  if (copernicusForecast) {
    wxFileName downloadDir;
    downloadDir.AssignDir(m_outputDir->GetPath());
    downloadDir.AppendDir("currentgrib_downloads");
    if (!downloadDir.DirExists()) {
      downloadDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    }
  }
  wxString command = BuildGenerateCommand();
  if (command.empty()) {
    AppendLog("Generation cancelled: native generator job could not be created.");
    return;
  }
  SaveSettings();
  AppendLog("Starting environmental GRIB generation...");
  AppendLog("Source: " + SourceLabel());
  wxString childPassword = copernicusForecast ? m_password->GetValue() : "";
  StartCommand(command, childPassword, true);
}

void EnvironmentalGribDialog::OnBrowseOutput(wxCommandEvent&) {
  wxFileDialog dialog(this, "Choose output GRIB path", m_outputDir->GetPath(), m_outputFile->GetValue(),
                      "GRIB files (*.grb;*.grib)|*.grb;*.grib|All files (*.*)|*.*",
                      wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
  if (dialog.ShowModal() != wxID_OK) return;
  wxFileName selected(dialog.GetPath());
  m_outputDir->SetPath(selected.GetPath());
  m_outputFileUserCustomized = true;
  m_outputFile->SetValue(selected.GetFullName());
}

void EnvironmentalGribDialog::OnOutputFilenameChanged(wxCommandEvent&) {
  if (m_updatingOutputFilename) return;
  wxString value = m_outputFile->GetValue();
  if (value.empty()) {
    m_outputFileUserCustomized = false;
    RefreshOutputFilenameDefault();
    return;
  }
  if (!m_lastAutoOutputFilename.empty() && value == m_lastAutoOutputFilename) {
    m_outputFileUserCustomized = false;
    return;
  }
  m_outputFileUserCustomized = true;
}

void EnvironmentalGribDialog::OnPresetChanged(wxCommandEvent& event) {
  ApplyPreset(event.GetSelection());
}

void EnvironmentalGribDialog::OnProviderChanged(wxCommandEvent&) {
  RefreshOutputFilenameDefault();
  UpdateProviderUi();
}

void EnvironmentalGribDialog::OnModeChanged(wxCommandEvent&) {
  RefreshOutputFilenameDefault();
  UpdateProviderUi();
}

void EnvironmentalGribDialog::ApplyPreset(int selection) {
  if (selection == 0) {
    RefreshOutputFilenameDefault();
    AppendLog("Using custom bbox.");
    return;
  }
  if (selection == 1) {
    if (!m_hasCurrentViewPort) {
      AppendLog("Current chart area is not available yet; enter bbox manually.");
      wxMessageBox("OpenCPN has not provided a valid chart viewport yet. Pan or zoom the chart, then try again.",
                   "Current chart area unavailable", wxOK | wxICON_INFORMATION, this);
      m_presetChoice->SetSelection(0);
      return;
    }
    if (m_currentViewPort.lon_min >= m_currentViewPort.lon_max ||
        m_currentViewPort.lat_min >= m_currentViewPort.lat_max) {
      AppendLog("Current chart area crosses an unsupported longitude boundary; enter bbox manually.");
      wxMessageBox("The current chart area cannot be converted to a simple west/south/east/north bbox. Enter bbox manually.",
                   "Current chart area unavailable", wxOK | wxICON_INFORMATION, this);
      m_presetChoice->SetSelection(0);
      return;
    }
    m_west->SetValue(wxString::Format("%.6f", m_currentViewPort.lon_min));
    m_south->SetValue(wxString::Format("%.6f", m_currentViewPort.lat_min));
    m_east->SetValue(wxString::Format("%.6f", m_currentViewPort.lon_max));
    m_north->SetValue(wxString::Format("%.6f", m_currentViewPort.lat_max));
    RefreshOutputFilenameDefault();
    AppendLog("Applied current chart area preset.");
    m_presetChoice->SetSelection(0);
    return;
  }
  struct AreaPreset {
    double west;
    double south;
    double east;
    double north;
    int currentSourceSelection;
  };
  const AreaPreset presets[] = {
      {-8.5, 50.5, -2.5, 56.5, 5},
      {-7.0, 48.0, -1.0, 51.5, 5},
      {-5.0, 51.0, 9.0, 60.0, 5},
      {-10.5, 43.0, -1.0, 48.5, 6},
      {-81.0, 24.0, -77.0, 28.0, 7},
      {-81.0, 24.0, -70.0, 36.0, 7},
      {-85.0, 10.0, -60.0, 25.0, 7},
  };
  int areaIndex = selection - 2;
  if (areaIndex >= 0 && areaIndex < static_cast<int>(WXSIZEOF(presets))) {
    const AreaPreset& area = presets[areaIndex];
    m_west->SetValue(wxString::Format("%.1f", area.west));
    m_south->SetValue(wxString::Format("%.1f", area.south));
    m_east->SetValue(wxString::Format("%.1f", area.east));
    m_north->SetValue(wxString::Format("%.1f", area.north));
    m_generateCurrents->SetValue(true);
    if (area.currentSourceSelection >= 0 &&
        area.currentSourceSelection < static_cast<int>(m_currentSource->GetCount())) {
      m_currentSource->SetSelection(area.currentSourceSelection);
    }
    RefreshOutputFilenameDefault();
    UpdateProviderUi();
    AppendLog("Applied area preset: " + m_presetChoice->GetString(selection));
  }
}

bool EnvironmentalGribDialog::AutoWouldUseMarineIe() const {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  bool parsed = m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
                m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north);
  return parsed && west >= -6.994 && east <= -4.006 && south >= 51.506 && north <= 55.494 &&
         m_durationHours->GetValue() <= 72;
}

bool EnvironmentalGribDialog::NeedsCopernicusCredentials() const {
  bool waveCopernicus = m_generateWeather->GetValue() &&
      m_includeWaves->GetValue() &&
      m_waveProvider->GetStringSelection().Contains("Copernicus");
  if (waveCopernicus) {
    return true;
  }
  if (!m_generateCurrents->GetValue()) {
    return false;
  }
  wxString currentSource = m_currentSource->GetStringSelection();
  return currentSource.Contains("Copernicus") || currentSource.Contains("Auto");
}

void EnvironmentalGribDialog::UpdateProviderUi() {
  int mode = m_mode->GetSelection();
  wxString provider = m_provider->GetStringSelection();
  bool forecastMode = mode == 0;
  bool tpxoMode = mode == 1;
  bool netcdfMode = mode == 2;
  bool syntheticMode = mode == 3;
  bool weatherEnabled = m_generateWeather->GetValue() && m_weatherProvider->GetStringSelection() != "None";
  bool currentsEnabled = m_generateCurrents->GetValue() && m_currentSource->GetStringSelection() != "None";
  bool weatherExisting = weatherEnabled && m_weatherProvider->GetStringSelection().Contains("Existing");
  bool currentExisting = currentsEnabled && m_currentSource->GetStringSelection().Contains("Existing");
  bool currentTpxoCache = currentsEnabled && m_currentSource->GetStringSelection().Contains("TPXO cache");
  bool currentTpxoDirect = currentsEnabled && m_currentSource->GetStringSelection().Contains("TPXO direct");
  bool currentMarine = currentsEnabled && m_currentSource->GetStringSelection().Contains("Marine.ie");
  bool currentRtofs = currentsEnabled && m_currentSource->GetStringSelection().Contains("RTOFS");
  bool currentOfs = currentsEnabled && m_currentSource->GetStringSelection().Contains("OFS");
  bool weatherGfs = weatherEnabled && m_weatherProvider->GetStringSelection().Contains("GFS");
  bool weatherHrrr = weatherEnabled && m_weatherProvider->GetStringSelection().Contains("HRRR");
  bool weatherUkv = weatherEnabled && m_weatherProvider->GetStringSelection().Contains("Met Office UKV");
  bool weatherIconEu = weatherEnabled && m_weatherProvider->GetStringSelection().Contains("ICON-EU");
  bool weatherAifs = weatherEnabled && m_weatherProvider->GetStringSelection().Contains("AIFS");
  bool weatherGenerated = weatherEnabled && !weatherExisting;
  bool waveEnabled = weatherGenerated && m_includeWaves->GetValue();
  bool waveCopernicus = waveEnabled && m_waveProvider->GetStringSelection().Contains("Copernicus");
  bool needsCopernicusCredentials = NeedsCopernicusCredentials();
  bool showTpxoModel = currentTpxoDirect || currentTpxoCache;

  auto showPair = [](wxWindow* label, wxWindow* control, bool show) {
    if (label) label->Show(show);
    if (control) control->Show(show);
  };

  m_provider->Enable(false);
  m_weatherProvider->Enable(m_generateWeather->GetValue());
  showPair(m_weatherPresetLabel, m_weatherPreset, weatherGenerated);
  showPair(m_wavesLabel, m_includeWaves, weatherGenerated);
  showPair(m_waveProviderLabel, m_waveProvider, waveEnabled);
  showPair(m_existingWeatherFileLabel, m_existingWeatherFile, weatherExisting);
  m_weatherPreset->Enable(weatherGenerated);
  m_includeWaves->Enable(weatherGenerated);
  if (!weatherGenerated) {
    m_includeWaves->SetValue(false);
  }
  m_waveProvider->Enable(waveEnabled);
  m_existingWeatherFile->Enable(weatherExisting);
  m_currentSource->Enable(m_generateCurrents->GetValue());
  showPair(m_existingCurrentFileLabel, m_existingCurrentFile, currentExisting);
  showPair(m_usernameLabel, m_username, needsCopernicusCredentials);
  showPair(m_passwordLabel, m_password, needsCopernicusCredentials);
  showPair(m_tpxoModelDirLabel, m_tpxoModelDir, showTpxoModel);
  showPair(m_tpxoModelNameLabel, m_tpxoModelName, showTpxoModel);
  showPair(m_tpxoGridSpacingLabel, m_tpxoGridSpacing, showTpxoModel);
  showPair(m_checkTpxoLabel, m_checkTpxoButton, showTpxoModel);
  showPair(m_tpxoCacheFileLabel, m_tpxoCacheFile, currentTpxoCache);
  showPair(m_prepareTpxoCacheLabel, m_prepareTpxoCacheButton, currentTpxoCache);
  showPair(m_localNetcdfLabel, m_localNetcdf, false);
  m_rememberUsername->Show(needsCopernicusCredentials);
  m_existingCurrentFile->Enable(currentExisting);
  m_username->Enable(needsCopernicusCredentials);
  m_password->Enable(needsCopernicusCredentials);
  m_rememberUsername->Enable(needsCopernicusCredentials);
  m_tpxoModelDir->Enable(showTpxoModel);
  m_tpxoModelName->Enable(showTpxoModel);
  m_tpxoGridSpacing->Enable(showTpxoModel);
  m_checkTpxoButton->Enable(showTpxoModel && !m_processRunning);
  m_useTpxoCache->Enable(false);
  m_tpxoCacheFile->Enable(currentTpxoCache);
  m_prepareTpxoCacheButton->Enable(currentTpxoCache && !m_processRunning);
  m_localNetcdf->Enable(false);

  wxString currentNote;
  if (currentRtofs) {
    currentNote =
        "Current source: NOAA RTOFS Global ocean-current forecast. Global/no account. "
        "Useful for offshore ocean-current routing, including Gulf Stream-type circulation where model guidance is available. "
        "Current guidance is typically 6-hourly.";
  } else if (currentOfs) {
    currentNote =
        "Current source: NOAA OFS/S-111 coastal current forecast. Experimental stub. "
        "U.S. coastal waters and Great Lakes. Not yet a complete GRIB generator.";
  }

  if (weatherUkv) {
    wxString note =
        "Source: Met Office UKV 2 km forecast. Met Office UKV 2 km is a high-resolution UK/Ireland short-range forecast. "
        "The plugin converts the source NetCDF data to OpenCPN GRIB in the background.\n"
        "UKV weather is hourly to about 54h and 3-hourly thereafter. Currents may remain hourly. "
        "Requests outside the UK/Ireland domain or beyond available hours will fail clearly.";
    if (m_weatherPreset->GetStringSelection().Contains("Marine")) {
      note += "\nUKV marine extras are not implemented yet; routing fields will be generated.";
    }
    if (waveCopernicus) {
      note += "\nWave source: Copernicus Marine Global Waves forecast. Account required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherHrrr) {
    wxString note =
        "Source: NOAA HRRR 3 km forecast. Short-range hourly NOAA HRRR for the contiguous United States. "
        "No account. Currently uses full-grid GRIB messages; files may be large because bbox cropping is not yet implemented.";
    if (waveCopernicus) {
      note += "\nWave source: Copernicus Marine Global Waves forecast. Account required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherIconEu) {
    wxString note =
        "Source: DWD ICON-EU regional forecast. DWD ICON-EU regional forecast for Europe. "
        "No account. Currently uses full-domain DWD field files; files may be large because bbox cropping is not yet implemented.";
    if (m_weatherPreset->GetStringSelection().Contains("Marine")) {
      note += "\nICON-EU marine extras currently generate routing fields only.";
    }
    if (waveCopernicus) {
      note += "\nWave source: Copernicus Marine Global Waves forecast. Account required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherAifs) {
    wxString note =
        "Source: ECMWF AIFS Open Data forecast. Global AI forecast, no account. "
        "Experimental in this build; live retrieval still needs validation. Files may be large if not cropped. "
        "Forecast steps are 6-hourly or coarser.";
    if (waveCopernicus) {
      note += "\nWave source: Copernicus Marine Global Waves forecast. Account required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherEnabled && m_weatherProvider->GetStringSelection().Contains("ECMWF")) {
    wxString note =
        "Source: ECMWF IFS Open Data forecast. ECMWF IFS Open Data is global/medium-range and currently not "
        "spatially cropped; files may be large. Forecast steps are 3-hourly or coarser.";
    if (waveCopernicus) {
      note += "\nWave source: Copernicus Marine Global Waves forecast. Account required; global 3-hourly wave fields.";
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (weatherGfs) {
    wxString note = "Source: NOAA GFS forecast via NOMADS. Bbox-subset weather is compact; optional wave fields add significant wave height, primary wave period, and primary wave direction.";
    if (waveCopernicus) {
      note += "\nWave source: Copernicus Marine Global Waves forecast. Account required; global 3-hourly wave fields.";
    }
    if (m_includeWaves->GetValue() && m_stepHours->GetValue() != 3) {
      note += wxString::Format("\nWave fields are included every 3 hours; wind/weather and currents remain every %d hour%s.",
                               m_stepHours->GetValue(), m_stepHours->GetValue() == 1 ? "" : "s");
    }
    if (!currentNote.empty()) note += "\n" + currentNote;
    m_providerNote->SetLabel(note);
  } else if (currentTpxoCache) {
    m_providerNote->SetLabel("Source: TPXO10 astronomical tide model cache. TPXO provides astronomical tidal currents from local licensed model/cache data. It does not model Gulf Stream, surge, wind residuals, or river flow.");
  } else if (currentTpxoDirect) {
    m_providerNote->SetLabel("Source: TPXO10 astronomical tide model. TPXO provides astronomical tidal currents from local licensed model/cache data. It does not model Gulf Stream, surge, wind residuals, or river flow.");
  } else if (currentMarine) {
    m_providerNote->SetLabel("Source: Marine.ie Irish Sea model current GRIB. No Copernicus credentials required. Valid time range depends on provider run time.");
  } else if (!currentNote.empty()) {
    m_providerNote->SetLabel(currentNote);
  } else if (needsCopernicusCredentials) {
    m_providerNote->SetLabel("Source: Copernicus Marine model ocean currents. Account required. Username/password are used for this operation only; password is passed via environment, not command line.");
  } else if (!currentsEnabled) {
    m_providerNote->SetLabel("Currents disabled. Output will be weather-only if weather is enabled.");
  } else {
    m_providerNote->SetLabel("");
  }
  m_scrolled->Layout();
  m_scrolled->FitInside();
  m_scrolled->SetVirtualSize(m_scrolled->GetSizer()->GetMinSize());
  Layout();
}

bool EnvironmentalGribDialog::ConfirmLargeCopernicusRequest() {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  bool parsed = m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
                m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north);
  double area = parsed ? (east - west) * (north - south) : 0.0;
  if (m_durationHours->GetValue() <= 72 && area <= 12.0) {
    return true;
  }
  wxString message =
      "This Copernicus request is larger than the normal v1 default.\n\n"
      "Duration: " + wxString::Format("%d hours", m_durationHours->GetValue()) +
      "\nApproximate bbox area: " + wxString::Format("%.2f square degrees", area) +
      "\n\nContinue?";
  return wxMessageBox(message, "Confirm Copernicus download", wxYES_NO | wxICON_WARNING, this) == wxYES;
}

bool EnvironmentalGribDialog::ValidateUkvRequest() {
  if (m_stepHours->GetValue() == 1 && m_durationHours->GetValue() > 54) {
    wxString message =
        "Met Office UKV is hourly to about 54h, then 3-hourly to 120h.\n"
        "Continue with mixed-cadence UKV weather?\n"
        "Currents will remain at the selected interval where supported.";
    AppendLog(message);
    if (wxMessageBox(message, "Confirm mixed-cadence UKV weather", wxYES_NO | wxICON_WARNING, this) != wxYES) {
      return false;
    }
  }
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  if (m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
      m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north)) {
    if (west < -12.0 || east > 4.0 || south < 48.0 || north > 62.0) {
      wxString message =
          "The requested bbox is outside the Met Office UKV UK/Ireland regional domain. Choose a UK/Ireland area or use GFS/ECMWF.";
      AppendLog(message);
      wxMessageBox(message, "UKV area unavailable", wxOK | wxICON_WARNING, this);
      return false;
    }
  }
  if (m_weatherPreset->GetStringSelection().Contains("Marine")) {
    AppendLog("UKV marine extras are not implemented yet; routing fields will be generated.");
  }
  return true;
}

bool EnvironmentalGribDialog::ValidateEcmwfRequest() {
  wxString weatherProvider = m_weatherProvider->GetStringSelection();
  int stepHours = m_stepHours->GetValue();
  bool aifs = weatherProvider.Contains("AIFS");
  bool valid = aifs ? (stepHours == 6 || stepHours == 12)
                    : (stepHours == 3 || stepHours == 6 || stepHours == 12);
  if (valid) {
    return true;
  }

  int replacementStepHours = aifs ? 6 : 3;
  wxString providerName = aifs ? "ECMWF AIFS Open Data" : "ECMWF IFS Open Data";
  wxString message =
      providerName + " is available at " +
      (aifs ? "6-hourly or coarser intervals." : "3-hourly or coarser intervals.") +
      "\nChange Step hours to " + wxString::Format("%d", replacementStepHours) + " and continue?";
  AppendLog(message);

  wxMessageDialog dialog(this, message, "Confirm ECMWF forecast step",
                         wxYES_NO | wxICON_WARNING);
  dialog.SetYesNoLabels(wxString::Format("Continue with %dh", replacementStepHours), "Cancel");
  if (dialog.ShowModal() != wxID_YES) {
    return false;
  }

  m_stepHours->SetValue(replacementStepHours);
  AppendLog(wxString::Format("Step hours changed to %d for %s.", replacementStepHours, providerName));
  return true;
}

void EnvironmentalGribDialog::OnCancel(wxCommandEvent&) {
  if (!m_processRunning || m_processPid == 0) {
    AppendLog("No running process to cancel.");
    return;
  }
  if (!ChildProcessStillExists()) {
    AppendLog(wxString::Format("Process pid=%ld has already exited; cleaning up dialog state.", m_processPid));
    DrainProcessOutput();
    FlushProcessOutput();
    FinishCommand(-1, true);
    return;
  }
  m_processCancelled = true;
  AppendLog(wxString::Format("Cancelling process, pid=%ld", m_processPid));
  wxKillError error = wxKILL_OK;
  wxKill(m_processPid, wxSIGTERM, &error, wxKILL_CHILDREN);
  if (error != wxKILL_OK) {
    AppendLog(wxString::Format("Process cancel request returned wxKillError=%d", static_cast<int>(error)));
    if (!ChildProcessStillExists()) {
      AppendLog("Process was already gone after cancel request; treating it as exited.");
      DrainProcessOutput();
      FlushProcessOutput();
      FinishCommand(-1, true);
      return;
    }
  }
}

void EnvironmentalGribDialog::OnClose(wxCommandEvent& event) {
  if (m_processRunning) {
    int response = wxMessageBox(
        "A generation or dependency check is still running. Cancel it?",
        "Operation running",
        wxYES_NO | wxICON_QUESTION,
        this);
    if (response == wxYES) {
      OnCancel(event);
    }
    return;
  }
  (void)event;
  SaveSettings();
  Hide();
}

void EnvironmentalGribDialog::OnDialogClose(wxCloseEvent& event) {
  if (m_processRunning) {
    int response = wxMessageBox(
        "A generation or dependency check is still running. Cancel it?",
        "Operation running",
        wxYES_NO | wxICON_QUESTION,
        this);
    if (response == wxYES) {
      wxCommandEvent dummy;
      OnCancel(dummy);
    }
    event.Veto();
    return;
  }
  SaveSettings();
  Hide();
}

void EnvironmentalGribDialog::OnProcessTimer(wxTimerEvent& event) {
  (void)event;
  DrainProcessOutput();
  if (m_processRunning && m_processPid != 0 && !ChildProcessStillExists()) {
    AppendLog(wxString::Format("Process pid=%ld is no longer running; finalizing without wxEVT_END_PROCESS.", m_processPid));
    FlushProcessOutput();
    FinishCommand(-1, true);
  }
}

void EnvironmentalGribDialog::OnProcessTerminated(wxProcessEvent& event) {
  if (!m_processRunning || event.GetPid() != m_processPid) {
    AppendLog(wxString::Format("Ignoring stale process completion event, pid=%d", static_cast<int>(event.GetPid())));
    return;
  }
  AppendLog(wxString::Format("Process completed, pid=%d", static_cast<int>(event.GetPid())));
  DrainProcessOutput();
  FlushProcessOutput();
  FinishCommand(event.GetExitCode(), true);
}

void EnvironmentalGribDialog::AppendLog(const wxString& message) { m_log->AppendText(message + "\n"); }

void EnvironmentalGribDialog::DrainStream(wxInputStream* stream, wxString* buffer, const wxString& prefix) {
  if (!stream || !buffer) return;
  while (stream->CanRead()) {
    char ch = static_cast<char>(stream->GetC());
    if (stream->LastRead() == 0) break;
    if (ch == '\r') continue;
    if (ch == '\n') {
      AppendLog(Redact(prefix + *buffer));
      buffer->clear();
    } else {
      *buffer += wxString::FromUTF8(&ch, 1);
    }
  }
}

void EnvironmentalGribDialog::DrainProcessOutput() {
  if (!m_process) return;
  DrainStream(m_process->GetInputStream(), &m_stdoutBuffer, "");
  DrainStream(m_process->GetErrorStream(), &m_stderrBuffer, "stderr: ");
}

void EnvironmentalGribDialog::FlushProcessOutput() {
  if (!m_stdoutBuffer.empty()) {
    AppendLog(Redact(m_stdoutBuffer));
    m_stdoutBuffer.clear();
  }
  if (!m_stderrBuffer.empty()) {
    AppendLog(Redact("stderr: " + m_stderrBuffer));
    m_stderrBuffer.clear();
  }
}

void EnvironmentalGribDialog::StartCommand(const wxString& command, const wxString& password, bool generation) {
  AppendLog("StartCommand begins");
  if (m_processRunning) {
    AppendLog("Another operation is already running.");
    return;
  }
  m_currentCommand = command;
  m_processGeneration = generation;
  m_processCancelled = false;
  m_processPid = 0;
  m_stdoutBuffer.clear();
  m_stderrBuffer.clear();
  SetBusy(true);
  AppendLog("Command: " + Redact(command));
  if (!password.empty()) {
    AppendLog("Copernicus password will be passed to the native helper through an environment variable, not on the command line or in the job file.");
  }

  auto* process = new wxProcess(this);
  process->Redirect();

  wxExecuteEnv env;
  if (!password.empty()) {
    env.env["ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD"] = password;
  }

  long pid = wxExecute(command, wxEXEC_ASYNC | wxEXEC_NODISABLE, process, password.empty() ? nullptr : &env);
  if (pid == 0) {
    AppendLog("Process failed to launch");
    delete process;
    FinishCommand(-1, false);
    return;
  }

  m_process = process;
  m_processRunning = true;
  m_processPid = pid;
  AppendLog(wxString::Format("Process launched, pid=%ld", pid));
  m_processTimer.Start(100);
}

void EnvironmentalGribDialog::FinishCommand(long exit_code, bool launched) {
  m_processTimer.Stop();
  if (m_process) {
    delete m_process;
    m_process = nullptr;
  }
  AppendLog(wxString::Format("Exit status: %ld", exit_code));
  bool generation = m_processGeneration;
  bool cancelled = m_processCancelled;
  wxString command = m_currentCommand;
  m_processGeneration = false;
  m_processCancelled = false;
  m_currentCommand.clear();
  m_processRunning = false;
  m_processPid = 0;
  SetBusy(false);

  wxString nativeError;
  if (!m_resultPath.empty() && wxFileName::FileExists(m_resultPath)) {
    wxFile resultFile(m_resultPath);
    wxString resultText;
    if (resultFile.IsOpened() && resultFile.ReadAll(&resultText)) {
      wxJSONValue resultValue;
      wxJSONReader reader;
      if (reader.Parse(resultText, &resultValue) == 0 &&
          resultValue.HasMember("error") &&
          resultValue["error"].HasMember("message")) {
        nativeError = resultValue["error"]["message"].AsString();
        AppendLog("Native generator error: " + Redact(nativeError));
      }
    }
    wxRemoveFile(m_resultPath);
    m_resultPath.clear();
  }
  if (!m_jobPath.empty()) {
    wxRemoveFile(m_jobPath);
    m_jobPath.clear();
  }

  if (!launched) {
    wxMessageBox("The generator process failed to launch. Check the generator executable path.",
                 "Launch failed", wxOK | wxICON_ERROR, this);
    return;
  }
  wxString validationDetails;
  bool outputValid = generation && OutputFileLooksValidGrib(&validationDetails);
  if (outputValid && exit_code != 0) {
    AppendLog(validationDetails);
  }
  bool generationSucceeded = generation && (exit_code == 0 || (exit_code < 0 && outputValid));
  if (cancelled && !generationSucceeded) {
    AppendLog("Process cancelled.");
    return;
  }
  if (generationSucceeded) {
    wxString message = "Generated environmental GRIB\nSource: " + SourceLabel() +
                       "\nValid time: " + ValidTimeSummary() +
                       "\nMessages: see validation summary in log" +
                       "\nOutput: " + OutputPath();
    if (m_openAfter->GetValue()) {
      TryOpenGeneratedGrib();
      message += "\n\nThe generated file was opened in xGRIB.";
    } else {
      message += "\n\nUse Open GRIB in xGRIB to display this file. It is already a merged environmental GRIB when both weather and currents were selected.";
    }
    AppendLog(message);
    wxMessageBox(message, "Environmental GRIB generated", wxOK | wxICON_INFORMATION, this);
  } else if (exit_code != 0 && generation) {
    if (command.Contains("--use-source-grid")) {
      AppendLog("If this failed while using the NetCDF source grid, retry from the CLI without --use-source-grid to interpolate to a regular grid.");
    }
    wxMessageBox("Environmental GRIB generation failed. See the log/details area for command output.",
                 "Generation failed", wxOK | wxICON_ERROR, this);
  }
}

bool EnvironmentalGribDialog::ChildProcessStillExists() const {
  if (!m_processRunning || m_processPid == 0) return false;
#ifdef __UNIX__
  errno = 0;
  if (kill(static_cast<pid_t>(m_processPid), 0) == 0) return true;
  if (errno == ESRCH) return false;
  return true;
#else
  return true;
#endif
}

bool EnvironmentalGribDialog::OutputFileLooksValidGrib(wxString* details) const {
  wxString path = OutputPath();
  if (!wxFileName::FileExists(path)) {
    if (details) *details = "output file does not exist";
    return false;
  }
  return GribStreamIsStrictlyValid(path, details);
}

void EnvironmentalGribDialog::SetBusy(bool busy) {
  m_checkButton->Enable(!busy);
  m_generateButton->Enable(!busy);
  m_cancelButton->Enable(busy);
  m_closeButton->Enable(true);
  UpdateProviderUi();
}

void EnvironmentalGribDialog::TryOpenGeneratedGrib() {
  wxString path = OutputPath();
  if (!wxFileName::FileExists(path)) {
    AppendLog("Generated GRIB does not exist; it cannot be opened.");
    return;
  }
  if (m_onGribReady) {
    m_onGribReady(path);
    AppendLog("Opened generated GRIB in xGRIB: " + path);
    return;
  }

  // Retain the standard GRIB message as a compatibility fallback for builds
  // embedding this dialog outside xGRIB.
  const wxString body = "{\"grib_file\":\"" + JsonEscape(path) + "\"}";
  SendPluginMessage("GRIB_APPLY_JSON_CONFIG", body);
  AppendLog("Requested GRIB open through plugin messaging: " + path);
}

bool EnvironmentalGribDialog::WriteGenerateJob(const wxString& job_path,
                                               wxString* error) const {
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  double currentSpacing = 0.0;
  if (!m_west->GetValue().ToDouble(&west) ||
      !m_south->GetValue().ToDouble(&south) ||
      !m_east->GetValue().ToDouble(&east) ||
      !m_north->GetValue().ToDouble(&north) ||
      !m_tpxoGridSpacing->GetValue().ToDouble(&currentSpacing)) {
    if (error) *error = "area coordinates and grid spacing must be numeric";
    return false;
  }

  wxString weatherProvider = "none";
  if (m_generateWeather->GetValue()) {
    const wxString selected = m_weatherProvider->GetStringSelection();
    if (selected.Contains("NOAA GFS")) weatherProvider = "gfs";
    else if (selected.Contains("HRRR")) weatherProvider = "noaa_hrrr";
    else if (selected.Contains("Met Office UKV")) weatherProvider = "ukmo_ukv";
    else if (selected.Contains("ICON-EU")) weatherProvider = "dwd_icon_eu";
    else if (selected.Contains("AIFS")) weatherProvider = "ecmwf_aifs_open";
    else if (selected.Contains("ECMWF")) weatherProvider = "ecmwf_ifs_open";
    else if (selected.Contains("Existing")) weatherProvider = "existing-file";
  }
  wxString weatherPreset = "routing";
  if (m_weatherPreset->GetStringSelection().Contains("Minimal")) {
    weatherPreset = "minimal";
  } else if (m_weatherPreset->GetStringSelection().Contains("Marine")) {
    weatherPreset = "marine";
  }

  wxString currentSource = "none";
  if (m_generateCurrents->GetValue()) {
    const wxString selected = m_currentSource->GetStringSelection();
    if (selected.Contains("TPXO cache")) currentSource = "tpxo-cache";
    else if (selected.Contains("TPXO direct")) currentSource = "tpxo";
    else if (selected.Contains("Existing")) currentSource = "existing-file";
    else if (selected.Contains("Marine.ie")) currentSource = "marine_ie_irish_sea";
    else if (selected.Contains("RTOFS")) currentSource = "noaa_rtofs_global";
    else if (selected.Contains("OFS")) currentSource = "noaa_ofs_s111";
    else if (selected.Contains("NWS")) currentSource = "copernicus_nws";
    else if (selected.Contains("Global")) currentSource = "copernicus_global";
    else if (selected.Contains("Auto")) currentSource = "auto";
  }
  if (m_mode->GetSelection() == 2) currentSource = "netcdf";
  if (m_mode->GetSelection() == 3) currentSource = "synthetic";

  wxJSONValue root;
  root["schemaVersion"] = 1;
  root["operation"] = "generateEnvironment";
  wxJSONValue& request = root["request"];
  request["bbox"]["west"] = west;
  request["bbox"]["south"] = south;
  request["bbox"]["east"] = east;
  request["bbox"]["north"] = north;
  request["start"] = m_startUtc->GetValue();
  request["hours"] = m_durationHours->GetValue();
  request["stepHours"] = m_stepHours->GetValue();
  request["cycle"] = "auto";
  request["weatherProvider"] = weatherProvider;
  request["weatherPreset"] = weatherPreset;
  request["weatherGridSpacingDeg"] = 0.025;
  request["weatherFile"] = m_existingWeatherFile->GetPath();
  request["includeWaves"] =
      m_includeWaves->GetValue() && weatherProvider != "none" &&
      weatherProvider != "existing-file";
  request["waveProvider"] =
      m_waveProvider->GetStringSelection().Contains("Copernicus")
          ? "copernicus_global_waves"
          : "gfs_wave";
  request["waveStepHours"] = 3;
  request["currentSource"] = currentSource;
  request["currentFile"] = m_existingCurrentFile->GetPath();
  request["inputNetcdf"] = m_localNetcdf->GetPath();
  request["inputCache"] = m_tpxoCacheFile->GetPath();
  request["tpxoModelDirectory"] = m_tpxoModelDir->GetPath();
  request["autoPrepareTpxoCache"] = currentSource == "tpxo-cache";
  request["currentGridSpacingDeg"] = currentSpacing;
  request["inferMinorTides"] = true;
  request["output"] = OutputPath();
  request["overwrite"] = true;
  request["keepIntermediate"] = false;
  request["dryRun"] = false;
  if (NeedsCopernicusCredentials()) {
    wxFileName downloadDir;
    downloadDir.AssignDir(m_outputDir->GetPath());
    downloadDir.AppendDir("currentgrib_downloads");
    request["downloadDirectory"] = downloadDir.GetPath();
    request["copernicusUsername"] = m_username->GetValue();
  }
  root["credentials"]["copernicusPasswordEnvironment"] =
      "ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD";

  wxJSONWriter writer;
  wxString text;
  writer.Write(root, text);
  wxFile file(job_path, wxFile::write);
  if (!file.IsOpened() || !file.Write(text)) {
    if (error) *error = "cannot write " + job_path;
    return false;
  }
  return true;
}

wxString EnvironmentalGribDialog::BuildGenerateCommand() {
  const wxString token = wxString::Format("%ld-%s", wxGetProcessId(),
                                          wxDateTime::Now().Format("%Y%m%d%H%M%S"));
  wxFileName job(m_outputDir->GetPath(), ".environmental-grib-job-" + token + ".json");
  wxFileName result(m_outputDir->GetPath(), ".environmental-grib-result-" + token + ".json");
  m_jobPath = job.GetFullPath();
  m_resultPath = result.GetFullPath();
  wxString error;
  if (!WriteGenerateJob(m_jobPath, &error)) {
    AppendLog("Cannot create native generator job: " + error);
    m_jobPath.clear();
    m_resultPath.clear();
    return {};
  }
  return ShellQuote(m_generatorPath->GetValue()) + " run-job --job " +
         ShellQuote(m_jobPath) + " --result " + ShellQuote(m_resultPath);
}

wxString EnvironmentalGribDialog::OutputPath() const {
  wxFileName output(m_outputDir->GetPath(), m_outputFile->GetValue());
  return output.GetFullPath();
}

wxString EnvironmentalGribDialog::SourceLabel() const {
  wxString weather = m_generateWeather->GetValue() ? m_weatherProvider->GetStringSelection() : "None";
  wxString current = m_generateCurrents->GetValue() ? m_currentSource->GetStringSelection() : "None";
  wxString waves = "None";
  if (m_generateWeather->GetValue() && m_includeWaves->GetValue()) {
    waves = m_waveProvider->GetStringSelection();
  }
  return "Environmental GRIB: weather=" + weather + ", currents=" + current + ", waves=" + waves;
}

wxString EnvironmentalGribDialog::ValidTimeSummary() const {
  wxString startText = m_startUtc->GetValue();
  wxString parseText = startText;
  if (parseText.EndsWith("Z")) parseText.RemoveLast();
  wxDateTime start;
  if (start.ParseISOCombined(parseText, 'T')) {
    wxDateTime end = start + wxTimeSpan::Hours(m_durationHours->GetValue());
    return start.FormatISOCombined('T') + "Z to " + end.FormatISOCombined('T') + "Z";
  }
  return startText + " plus " + wxString::Format("%d hours", m_durationHours->GetValue());
}

int EnvironmentalGribDialog::ExpectedMessageCount() const {
  int step = std::max(1, m_stepHours->GetValue());
  int timesteps = (m_durationHours->GetValue() / step) + 1;
  return timesteps * 2;
}

wxString EnvironmentalGribDialog::DefaultOutputFilenameForSelection() const {
  wxString prefix;
  bool weatherOn = m_generateWeather->GetValue() && m_weatherProvider->GetStringSelection() != "None";
  bool currentOn = m_generateCurrents->GetValue() && m_currentSource->GetStringSelection() != "None";
  wxString weatherProvider = m_weatherProvider->GetStringSelection();
  wxString currentSource = m_currentSource->GetStringSelection();
  double west = 0.0;
  double south = 0.0;
  double east = 0.0;
  double north = 0.0;
  bool looksIrishSea =
      m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
      m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north) &&
      std::abs(west - -8.5) < 0.01 && std::abs(south - 50.5) < 0.01 &&
      std::abs(east - -2.5) < 0.01 && std::abs(north - 56.5) < 0.01;
  bool looksGulfStream =
      m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
      m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north) &&
      west >= -85.0 && east <= -60.0 && south >= 20.0 && north <= 42.0;
  bool ukvMixedCadence = weatherProvider.Contains("UKV") && m_stepHours->GetValue() == 1 && m_durationHours->GetValue() > 54;
  if (weatherOn && currentOn) {
    prefix = "environment";
    if (weatherProvider.Contains("HRRR")) prefix += "_hrrr";
    else if (weatherProvider.Contains("GFS")) prefix += "_gfs";
    else if (weatherProvider.Contains("UKV")) prefix += "_ukmo_ukv";
    else if (weatherProvider.Contains("ICON-EU")) prefix += "_icon_eu";
    else if (weatherProvider.Contains("AIFS")) prefix += "_ecmwf_aifs";
    else if (weatherProvider.Contains("ECMWF")) prefix += "_ecmwf_ifs";
    else if (weatherProvider.Contains("Existing")) prefix += "_existing_weather";
    if (ukvMixedCadence) prefix += "_mixed";
    if (m_includeWaves->GetValue()) {
      prefix += m_waveProvider->GetStringSelection().Contains("Copernicus") ? "_copernicus_waves" : "_wave";
    }
    if (currentSource.Contains("TPXO cache")) prefix += "_tpxo_cache";
    else if (currentSource.Contains("TPXO direct")) prefix += "_tpxo";
    else if (currentSource.Contains("Marine.ie")) prefix += "_marine_ie";
    else if (currentSource.Contains("RTOFS")) prefix += "_noaa_rtofs_global";
    else if (currentSource.Contains("OFS")) prefix += "_noaa_ofs_s111";
    else if (currentSource.Contains("NWS")) prefix += "_copernicus_nws";
    else if (currentSource.Contains("Global")) prefix += "_copernicus_global";
    else if (currentSource.Contains("Auto")) prefix += "_auto_current";
    else if (currentSource.Contains("Existing")) prefix += "_existing_current";
    if (looksIrishSea) prefix += "_irish_sea";
    if (currentSource.Contains("RTOFS") && looksGulfStream) prefix += "_gulf_stream";
  } else if (weatherOn) {
    prefix = "weather";
    if (weatherProvider.Contains("HRRR")) prefix += "_hrrr";
    else if (weatherProvider.Contains("GFS")) prefix += "_gfs";
    else if (weatherProvider.Contains("UKV")) prefix += "_ukmo_ukv";
    else if (weatherProvider.Contains("ICON-EU")) prefix += "_icon_eu";
    else if (weatherProvider.Contains("AIFS")) prefix += "_ecmwf_aifs";
    else if (weatherProvider.Contains("ECMWF")) prefix += "_ecmwf_ifs";
    else prefix += "_existing";
    if (ukvMixedCadence) prefix += "_mixed";
    if (m_weatherPreset->GetStringSelection().Contains("Marine")) prefix += "_marine";
    if (m_includeWaves->GetValue()) {
      prefix += m_waveProvider->GetStringSelection().Contains("Copernicus") ? "_copernicus_waves" : "_wave";
    }
    if (looksIrishSea) prefix += "_irish_sea";
  } else if (currentOn) {
    prefix = "current";
    if (currentSource.Contains("TPXO cache")) prefix += "_tpxo_cache";
    else if (currentSource.Contains("TPXO direct")) prefix += "_tpxo";
    else if (currentSource.Contains("Marine.ie")) prefix += "_marine_ie";
    else if (currentSource.Contains("RTOFS")) prefix += "_noaa_rtofs_global";
    else if (currentSource.Contains("OFS")) prefix += "_noaa_ofs_s111";
    else if (currentSource.Contains("NWS")) prefix += "_copernicus_nws";
    else if (currentSource.Contains("Global")) prefix += "_copernicus_global";
    else if (currentSource.Contains("Auto")) prefix += "_auto";
    else prefix += "_existing";
    if (currentSource.Contains("RTOFS") && looksGulfStream) prefix += "_gulf_stream";
  }
  if (!prefix.empty()) return TimestampedFilename(prefix);

  int mode = m_mode->GetSelection();
  int preset = m_presetChoice->GetSelection();
  if (mode == 1) {
    return preset == 2 ? IrishSeaTpxoOutputFilename() : DefaultTpxoOutputFilename();
  }
  if (mode == 2) return TimestampedFilename("local_netcdf_current");
  if (mode == 3) return TimestampedFilename("synthetic_current");

  wxString provider = m_provider->GetStringSelection();
  if (IsMarineIeProvider(provider) || (provider == "Auto" && AutoWouldUseMarineIe())) {
    return MarineIeOutputFilename();
  }
  if (provider.Contains("Copernicus Marine Global")) {
    return TimestampedFilename("copernicus_global_current");
  }
  if (provider.Contains("Copernicus Marine North-West Shelf")) {
    return TimestampedFilename("copernicus_nws_current");
  }
  if (provider == "Auto") {
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
    bool parsed = m_west->GetValue().ToDouble(&west) && m_south->GetValue().ToDouble(&south) &&
                  m_east->GetValue().ToDouble(&east) && m_north->GetValue().ToDouble(&north);
    if (parsed && west >= -20.0 && east <= 13.0 && south >= 40.0 && north <= 65.0) {
      return TimestampedFilename("copernicus_nws_current");
    }
    return TimestampedFilename("copernicus_global_current");
  }
  return TimestampedFilename("current_grib");
}

void EnvironmentalGribDialog::RefreshOutputFilenameDefault() {
  wxString previousAuto = m_lastAutoOutputFilename;
  wxString current = m_outputFile->GetValue();
  wxString nextAuto = DefaultOutputFilenameForSelection();
  bool shouldUpdate = current.empty() || !m_outputFileUserCustomized ||
                      (!previousAuto.empty() && current == previousAuto);
  m_lastAutoOutputFilename = nextAuto;
  if (!shouldUpdate) return;
  m_updatingOutputFilename = true;
  m_outputFile->SetValue(nextAuto);
  m_updatingOutputFilename = false;
  m_outputFileUserCustomized = false;
}

void EnvironmentalGribDialog::LoadSettings() {
  wxConfigBase* config = wxConfigBase::Get(false);
  if (!config) return;
  wxString oldPath = config->GetPath();
  config->SetPath("/PlugIns/xGRIB/EnvironmentalGenerator");
  long mode = config->ReadLong("generation_mode", m_mode->GetSelection());
  if (mode >= 0 && mode < static_cast<long>(m_mode->GetCount())) {
    m_mode->SetSelection(static_cast<int>(mode));
  }
  wxString value;
  if (config->Read("tpxo_model_directory", &value) && !value.empty()) {
    m_tpxoModelDir->SetPath(value);
  }
  if (config->Read("tpxo_model_name", &value) && !value.empty()) {
    m_tpxoModelName->SetValue(value);
  }
  if (config->Read("tpxo_grid_spacing", &value) && !value.empty()) {
    m_tpxoGridSpacing->SetValue(value);
  }
  if (config->Read("tpxo_cache_file", &value) && !value.empty()) {
    m_tpxoCacheFile->SetPath(value);
  }
  m_useTpxoCache->SetValue(config->ReadBool("use_tpxo_cache", false));
  m_durationHours->SetValue(static_cast<int>(config->ReadLong("duration_hours", m_durationHours->GetValue())));
  m_stepHours->SetValue(static_cast<int>(config->ReadLong("step_hours", m_stepHours->GetValue())));
  bool rememberUsername = config->ReadBool("remember_copernicus_username", false);
  m_rememberUsername->SetValue(rememberUsername);
  if (rememberUsername && config->Read("copernicus_username", &value)) {
    m_username->SetValue(value);
  }
  config->SetPath(oldPath);
}

void EnvironmentalGribDialog::SaveSettings() {
  wxConfigBase* config = wxConfigBase::Get(false);
  if (!config) return;
  wxString oldPath = config->GetPath();
  config->SetPath("/PlugIns/xGRIB/EnvironmentalGenerator");
  config->Write("generation_mode", static_cast<long>(m_mode->GetSelection()));
  config->Write("tpxo_model_directory", m_tpxoModelDir->GetPath());
  config->Write("tpxo_model_name", m_tpxoModelName->GetValue());
  config->Write("tpxo_grid_spacing", m_tpxoGridSpacing->GetValue());
  config->Write("tpxo_cache_file", m_tpxoCacheFile->GetPath());
  config->Write("use_tpxo_cache", m_useTpxoCache->GetValue());
  config->Write("duration_hours", static_cast<long>(m_durationHours->GetValue()));
  config->Write("step_hours", static_cast<long>(m_stepHours->GetValue()));
  config->Write("remember_copernicus_username", m_rememberUsername->GetValue());
  if (m_rememberUsername->GetValue()) {
    config->Write("copernicus_username", m_username->GetValue());
  } else {
    config->DeleteEntry("copernicus_username", false);
  }
  config->Flush();
  config->SetPath(oldPath);
}

wxString EnvironmentalGribDialog::FindDefaultGenerator() const {
  wxString path;
  if (wxGetEnv("ENVIRONMENTAL_GRIB_GENERATOR", &path) &&
      IsExecutableFile(path)) {
    return path;
  }

  wxFileName executable(wxStandardPaths::Get().GetExecutablePath());
  wxFileName sibling(executable.GetPath(), "environmental-grib");
  if (IsExecutableFile(sibling.GetFullPath())) return sibling.GetFullPath();

  wxFileName packaged(GetXgribDataDirectory(), "");
  packaged.RemoveLastDir();
  packaged.AppendDir("bin");
  packaged.SetFullName("environmental-grib");
  if (IsExecutableFile(packaged.GetFullPath())) return packaged.GetFullPath();

  if (wxFindFileInPath(&path, wxGetenv("PATH"), "environmental-grib")) {
    return path;
  }
  return "environmental-grib";
}

wxString EnvironmentalGribDialog::Redact(const wxString& text) const {
  wxString redacted(text);
  if (!m_password->GetValue().empty()) redacted.Replace(m_password->GetValue(), "<redacted>");
  if (!m_username->GetValue().empty()) redacted.Replace(m_username->GetValue(), "<redacted-user>");
  RedactQueryParameter(&redacted, "x-cop-user");
  RedactQueryParameter(&redacted, "username");
  RedactQueryParameter(&redacted, "user");
  RedactQueryParameter(&redacted, "email");
  RedactQueryParameter(&redacted, "token");
  RedactQueryParameter(&redacted, "access_token");
  RedactQueryParameter(&redacted, "password");
  return redacted;
}
