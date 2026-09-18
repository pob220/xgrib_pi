#pragma once

#include <functional>

#include <wx/dialog.h>
#include <wx/string.h>

class wxButton;
class wxTextCtrl;

// A small, self-contained generator for Android. It combines complete GRIB
// messages from local files without invoking a child process or ecCodes.
class AndroidGribGeneratorDialog : public wxDialog {
public:
  using GribReadyCallback = std::function<void(const wxString&)>;
  AndroidGribGeneratorDialog(wxWindow* parent, GribReadyCallback onGribReady);

private:
  void ChooseInput(size_t index);
  void Generate();
  wxString m_paths[3];
  wxTextCtrl* m_pathDisplays[3];
  wxTextCtrl* m_outputName;
  wxTextCtrl* m_status;
  wxButton* m_generate;
  GribReadyCallback m_onGribReady;
};
