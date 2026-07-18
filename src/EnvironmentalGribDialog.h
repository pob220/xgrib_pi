#pragma once

#include <functional>

#include <wx/filepicker.h>
#include <wx/process.h>
#include <wx/scrolwin.h>
#include <wx/spinctrl.h>
#include <wx/timer.h>
#include <wx/wx.h>

#include "ocpn_plugin.h"

class EnvironmentalGribDialog : public wxDialog {
public:
  using GribReadyCallback = std::function<void(const wxString&)>;

  explicit EnvironmentalGribDialog(wxWindow* parent,
                                   GribReadyCallback onGribReady = {});
  ~EnvironmentalGribDialog() override;
  void SetCurrentViewPort(const PlugIn_ViewPort& vp);
  void PrepareForParentShutdown();

private:
  void OnCheckDependencies(wxCommandEvent& event);
  void OnCheckTpxoModel(wxCommandEvent& event);
  void OnPrepareTpxoCache(wxCommandEvent& event);
  void OnGenerate(wxCommandEvent& event);
  void OnBrowseOutput(wxCommandEvent& event);
  void OnOutputFilenameChanged(wxCommandEvent& event);
  void OnOfflineTidalFileChanged(wxFileDirPickerEvent& event);
  void OnPresetChanged(wxCommandEvent& event);
  void OnProviderChanged(wxCommandEvent& event);
  void OnModeChanged(wxCommandEvent& event);
  void OnCancel(wxCommandEvent& event);
  void OnClose(wxCommandEvent& event);
  void OnDialogClose(wxCloseEvent& event);
  void OnProcessTimer(wxTimerEvent& event);
  void OnProcessTerminated(wxProcessEvent& event);
  void AppendLog(const wxString& message);
  void DrainProcessOutput();
  void FlushProcessOutput();
  void DrainStream(wxInputStream* stream, wxString* buffer, const wxString& prefix);
  void StartCommand(const wxString& command, const wxString& password, bool generation);
  void FinishCommand(long exit_code, bool launched);
  bool ChildProcessStillExists() const;
  bool OutputFileLooksValidGrib(wxString* details = nullptr) const;
  void SetBusy(bool busy);
  void ApplyPreset(int selection);
  bool ConfirmLargeCopernicusRequest();
  bool ValidateUkvRequest();
  bool ValidateEcmwfRequest();
  bool AutoWouldUseMarineIe() const;
  bool NeedsCopernicusCredentials() const;
  bool IsOfflineTidalSelected() const;
  bool IsOfflineTidalNeeded() const;
  bool ValidateOfflineTidalPackage();
  void UpdateProviderUi();
  void RefreshOutputFilenameDefault();
  void LoadSettings();
  void SaveSettings();
  void TryOpenGeneratedGrib();
  wxString BuildGenerateCommand();
  bool WriteGenerateJob(const wxString& job_path, wxString* error) const;
  wxString OutputPath() const;
  wxString SourceLabel() const;
  wxString ValidTimeSummary() const;
  int ExpectedMessageCount() const;
  wxString DefaultOutputFilenameForSelection() const;
  wxString FindDefaultGenerator() const;
  wxString Redact(const wxString& text) const;

  wxTextCtrl* m_generatorPath;
  wxScrolledWindow* m_scrolled;
  wxTextCtrl* m_west;
  wxTextCtrl* m_south;
  wxTextCtrl* m_east;
  wxTextCtrl* m_north;
  wxTextCtrl* m_startUtc;
  wxSpinCtrl* m_durationHours;
  wxSpinCtrl* m_stepHours;
  wxCheckBox* m_extendForecast;
  wxStaticText* m_fallbackWeatherLabel;
  wxChoice* m_fallbackWeatherProvider;
  wxStaticText* m_fallbackWaveLabel;
  wxChoice* m_fallbackWaveProvider;
  wxStaticText* m_fallbackCurrentLabel;
  wxChoice* m_fallbackCurrentSource;
  wxCheckBox* m_generateWeather;
  wxChoice* m_weatherProvider;
  wxStaticText* m_weatherPresetLabel;
  wxChoice* m_weatherPreset;
  wxStaticText* m_wavesLabel;
  wxCheckBox* m_includeWaves;
  wxStaticText* m_waveProviderLabel;
  wxChoice* m_waveProvider;
  wxStaticText* m_existingWeatherFileLabel;
  wxFilePickerCtrl* m_existingWeatherFile;
  wxCheckBox* m_generateCurrents;
  wxChoice* m_currentSource;
  wxStaticText* m_existingCurrentFileLabel;
  wxFilePickerCtrl* m_existingCurrentFile;
  wxStaticText* m_offlineTidalFileLabel;
  wxFilePickerCtrl* m_offlineTidalFile;
  wxStaticText* m_offlineTidalStatusLabel;
  wxTextCtrl* m_offlineTidalStatus;
  wxStaticText* m_offlineCurrentModeLabel;
  wxChoice* m_offlineCurrentMode;
  wxChoice* m_mode;
  wxChoice* m_presetChoice;
  wxChoice* m_provider;
  wxStaticText* m_usernameLabel;
  wxTextCtrl* m_username;
  wxStaticText* m_passwordLabel;
  wxTextCtrl* m_password;
  wxCheckBox* m_rememberUsername;
  wxStaticText* m_providerNote;
  wxStaticText* m_tpxoModelDirLabel;
  wxDirPickerCtrl* m_tpxoModelDir;
  wxStaticText* m_tpxoModelNameLabel;
  wxTextCtrl* m_tpxoModelName;
  wxStaticText* m_tpxoGridSpacingLabel;
  wxTextCtrl* m_tpxoGridSpacing;
  wxStaticText* m_checkTpxoLabel;
  wxButton* m_checkTpxoButton;
  wxCheckBox* m_useTpxoCache;
  wxStaticText* m_tpxoCacheFileLabel;
  wxFilePickerCtrl* m_tpxoCacheFile;
  wxStaticText* m_prepareTpxoCacheLabel;
  wxButton* m_prepareTpxoCacheButton;
  wxStaticText* m_localNetcdfLabel;
  wxFilePickerCtrl* m_localNetcdf;
  wxDirPickerCtrl* m_outputDir;
  wxTextCtrl* m_outputFile;
  wxButton* m_checkButton;
  wxButton* m_generateButton;
  wxButton* m_cancelButton;
  wxButton* m_closeButton;
  wxCheckBox* m_openAfter;
  wxCheckBox* m_showMergeInstructions;
  wxTextCtrl* m_log;
  wxTimer m_processTimer;
  wxProcess* m_process{nullptr};
  bool m_processRunning{false};
  bool m_processGeneration{false};
  bool m_processCancelled{false};
  long m_processPid{0};
  bool m_hasCurrentViewPort{false};
  PlugIn_ViewPort m_currentViewPort{};
  wxString m_currentCommand;
  wxString m_jobPath;
  wxString m_resultPath;
  wxString m_stdoutBuffer;
  wxString m_stderrBuffer;
  wxString m_lastAutoOutputFilename;
  GribReadyCallback m_onGribReady;
  bool m_outputFileUserCustomized{false};
  bool m_updatingOutputFilename{false};
  bool m_offlineTidalPackageValid{false};
  bool m_offlineClimatologyAvailable{false};
};
