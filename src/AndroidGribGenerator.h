#pragma once

#include <functional>
#include <vector>

#include <wx/dialog.h>
#include <wx/string.h>

class wxButton;
class wxCheckBox;
class wxChoice;
class wxNotebook;
class wxTextCtrl;

// Android forecast downloads and local-file assembly run in OpenCPN's process;
// Android does not allow executing a helper from the plugin data directory.
class AndroidGribGeneratorDialog : public wxDialog {
public:
  using GribReadyCallback = std::function<void(const wxString&)>;
  AndroidGribGeneratorDialog(wxWindow* parent, GribReadyCallback onGribReady);
  ~AndroidGribGeneratorDialog() override;

private:
  struct DownloadItem {
    wxString url;
    wxString label;
    wxString path;
  };
  void ChooseInput(size_t index);
  void Generate();
  void GenerateForecast();
  void StartNextDownload();
  void FinishForecast();
  void StopForecast(const wxString& error);
  void CloseOrCancel();
  bool PrepareOutput();
  bool Assemble(const std::vector<wxString>& inputs, bool verifyProviders,
                wxString* error);
  void CleanDownloads();
  wxString m_paths[3];
  wxTextCtrl* m_pathDisplays[3];
  wxNotebook* m_notebook{};
  bool m_forecastMode{true};
  wxTextCtrl* m_west{};
  wxTextCtrl* m_east{};
  wxTextCtrl* m_south{};
  wxTextCtrl* m_north{};
  wxChoice* m_hours{};
  wxCheckBox* m_waves{};
  wxCheckBox* m_currents{};
  wxChoice* m_currentRegion{};
  wxTextCtrl* m_outputName;
  wxTextCtrl* m_status;
  wxButton* m_generate;
  wxButton* m_close{};
  wxString m_outputPath;
  wxString m_workDir;
  std::vector<DownloadItem> m_downloads;
  std::size_t m_downloadIndex{};
  bool m_downloading{};
  bool m_canceling{};
  int m_currentStartHour{};
  int m_currentEndHour{};
  GribReadyCallback m_onGribReady;
};
