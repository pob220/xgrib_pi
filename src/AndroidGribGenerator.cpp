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
#include <wx/datetime.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>

#include "ocpn_plugin.h"

namespace {

bool ReadExact(std::ifstream& input, char* bytes, std::size_t size) {
  return static_cast<bool>(input.read(bytes, size));
}

std::uint64_t BigEndian(const unsigned char* bytes, std::size_t count) {
  std::uint64_t result = 0;
  for (std::size_t i = 0; i < count; ++i) result = (result << 8) | bytes[i];
  return result;
}

bool AppendGrib(const wxString& path, std::ofstream& output,
                std::size_t* messages, wxString* error) {
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
    input.seekg(static_cast<std::streamoff>(offset + length - 4));
    char terminator[4]{};
    if (!ReadExact(input, terminator, 4) ||
        std::memcmp(terminator, "7777", 4) != 0) {
      *error = wxString::Format(_("Missing GRIB terminator at byte %llu"),
                                static_cast<unsigned long long>(offset)) +
               " in " + path;
      return false;
    }
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
    offset += length;
    ++*messages;
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
  auto* intro = new wxStaticText(
      this, wxID_ANY,
      _("Combine weather, wave and current GRIB files already on this tablet. "
        "Each selected file must contain complete, uncompressed GRIB messages."));
  intro->Wrap(650);
  content->Add(intro, 0, wxEXPAND | wxALL, 12);

  const wxString labels[] = {_("Weather GRIB"), _("Wave GRIB"),
                             _("Current GRIB")};
  for (size_t index = 0; index < 3; ++index) {
    auto* row = new wxBoxSizer(wxHORIZONTAL);
    auto* pick = new wxButton(this, wxID_ANY, labels[index]);
    pick->SetMinSize(wxSize(180, 56));
    pick->Bind(wxEVT_BUTTON, [this, index](wxCommandEvent&) {
      ChooseInput(index);
    });
    row->Add(pick, 0, wxRIGHT, 8);
    m_pathDisplays[index] = new wxTextCtrl(
        this, wxID_ANY, _("No file selected"), wxDefaultPosition,
        wxDefaultSize, wxTE_READONLY);
    row->Add(m_pathDisplays[index], 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    content->Add(row, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  }

  content->Add(new wxStaticText(this, wxID_ANY, _("Output filename")), 0,
               wxLEFT | wxRIGHT, 12);
  m_outputName = new wxTextCtrl(
      this, wxID_ANY,
      "xgrib_combined_" + wxDateTime::Now().ToUTC().Format("%Y%m%d_%H%M%S") +
          ".grb");
  content->Add(m_outputName, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  m_status = new wxTextCtrl(this, wxID_ANY,
      _("The new file will be saved in xGRIB's generated folder."),
      wxDefaultPosition, wxSize(-1, 100), wxTE_MULTILINE | wxTE_READONLY);
  content->Add(m_status, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

  auto* actions = new wxBoxSizer(wxHORIZONTAL);
  m_generate = new wxButton(this, wxID_ANY, _("Generate and open"));
  m_generate->SetMinSize(wxSize(220, 56));
  m_generate->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Generate(); });
  actions->Add(m_generate, 1, wxRIGHT, 8);
  auto* close = new wxButton(this, wxID_CANCEL, _("Close"));
  close->SetMinSize(wxSize(140, 56));
  close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { Hide(); });
  Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent&) { Hide(); });
  actions->Add(close, 0);
  content->Add(actions, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);
  SetSizer(content);
  SetSize(wxSize(700, 570));
  CentreOnParent();
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
  const wxString name = m_outputName->GetValue();
  if (name.empty() || name.Contains("/") || name.Contains("\\") ||
      !(name.Lower().EndsWith(".grb") ||
        name.Lower().EndsWith(".grib") ||
        name.Lower().EndsWith(".grb2"))) {
    m_status->SetValue(_("Use a GRIB filename without folders."));
    return;
  }
  wxFileName output(GetPluginDataDir("xgrib_pi"), name);
  output.AppendDir("generated");
  if (!output.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL)) {
    m_status->SetValue(_("Could not create the generated GRIB folder."));
    return;
  }
  if (output.FileExists()) {
    m_status->SetValue(_("A file with this name already exists. Change the filename."));
    return;
  }
  const wxString temporary = output.GetFullPath() + ".part";
  wxRemoveFile(temporary);
  m_generate->Enable(false);
  m_status->SetValue(_("Combining and checking GRIB messages..."));
  wxString error;
  std::size_t messages = 0;
  bool success = false;
  {
    std::ofstream stream(std::string(temporary.ToUTF8().data()),
                         std::ios::binary | std::ios::trunc);
    if (!stream) {
      error = _("Could not create output file.");
    } else {
      success = true;
      for (const auto& input : inputs) {
        if (!AppendGrib(input, stream, &messages, &error)) {
          success = false;
          break;
        }
      }
      stream.close();
      if (!stream && success) {
        error = _("Could not finish writing the output file.");
        success = false;
      }
    }
  }
  if (success) success = wxRenameFile(temporary, output.GetFullPath(), false);
  if (!success) {
    wxRemoveFile(temporary);
    m_status->SetValue(error.empty() ? _("Could not save the generated GRIB.")
                                     : error);
    m_generate->Enable(true);
    return;
  }
  m_status->SetValue(wxString::Format(
      _("Created %zu GRIB messages in "), messages) + output.GetFullName() +
      _(". Saved in xGRIB's generated folder."));
  m_generate->Enable(true);
  if (m_onGribReady) m_onGribReady(output.GetFullPath());
}
