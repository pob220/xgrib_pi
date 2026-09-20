#pragma once
#include <functional>
#include <memory>
#include <wx/dialog.h>
#include "ocpn_plugin.h"

int AndroidChooseForecast(wxWindow* parent, const wxArrayString& times, int selected);
struct GribOverlaySettings;
bool AndroidGribSettings(wxWindow* parent, GribOverlaySettings& settings);
void AndroidFitDialog(wxWindow* parent, wxDialog& dialog);

class AndroidGribGeneratorDialog : public wxDialog {
 public:
  using GribReadyCallback = std::function<void(const wxString&)>;
  AndroidGribGeneratorDialog(wxWindow* parent, GribReadyCallback ready);
  ~AndroidGribGeneratorDialog() override;
  void ShowMobile(const PlugIn_ViewPort& viewport, bool serverDownload = false);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
