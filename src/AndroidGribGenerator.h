#pragma once
#include <functional>
#include <memory>
#include <wx/dialog.h>
#include "ocpn_plugin.h"

class AndroidGribGeneratorDialog : public wxDialog {
 public:
  using GribReadyCallback = std::function<void(const wxString&)>;
  AndroidGribGeneratorDialog(wxWindow* parent, GribReadyCallback ready);
  ~AndroidGribGeneratorDialog() override;
  void ShowMobile(const PlugIn_ViewPort& viewport);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
