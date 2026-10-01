#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

#include <wx/app.h>
#include <wx/apptrait.h>
#include <wx/evtloop.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/stream.h>
#include <wx/utils.h>

#include "ProcessCommand.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

class MonitoredProcess : public wxProcess {
public:
  void OnTerminate(int, int) override { terminated = true; }
  bool terminated{false};
};

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && wxString::FromUTF8(argv[1]) == "--child") return 0;
  if (argc == 2 && wxString::FromUTF8(argv[1]) == "--waiting-child") {
    std::cout << "ready\n" << std::flush;
    std::this_thread::sleep_for(std::chrono::seconds(30));
    return 0;
  }

  wxInitializer initializer;
  Expect(initializer.IsOk(), "wxWidgets must initialize for process tests");

#ifdef _WIN32
  Expect(xgrib::QuoteProcessArgument("C:\\Program Files\\xgrib\\helper.exe") ==
             "\"C:\\Program Files\\xgrib\\helper.exe\"",
         "Windows paths must use CreateProcess double-quote rules");
  Expect(xgrib::QuoteProcessArgument("C:\\trailing\\") ==
             "\"C:\\trailing\\\\\"",
         "Windows trailing backslashes must be doubled");
  Expect(xgrib::QuoteProcessArgument("alpha\"beta") ==
             "\"alpha\\\"beta\"",
         "Windows embedded quotes must be escaped");
#else
  Expect(xgrib::QuoteProcessArgument("alpha beta") == "'alpha beta'",
         "POSIX paths must retain shell single-quote rules");
  Expect(xgrib::QuoteProcessArgument("alpha'beta") == "'alpha'\\''beta'",
         "POSIX embedded single quotes must be escaped");
#endif

  const wxString executable = wxStandardPaths::Get().GetExecutablePath();
  wxFileName testDirectory(wxFileName::GetTempDir(), "");
  testDirectory.AppendDir(wxString::FromUTF8("xgrib process caf\xc3\xa9"));
  Expect(testDirectory.DirExists() ||
             testDirectory.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL),
         "temporary Unicode test directory must be created");
  wxFileName copiedExecutable(testDirectory.GetPath(),
                              "xgrib process command test");
#ifdef _WIN32
  copiedExecutable.SetExt("exe");
#endif
  Expect(wxCopyFile(executable, copiedExecutable.GetFullPath(), true),
         "test executable must be copied to a path with spaces and Unicode");

  const wxString command =
      xgrib::QuoteProcessArgument(copiedExecutable.GetFullPath()) + " --child";
  const long exitCode = wxExecute(command, wxEXEC_SYNC);
  Expect(exitCode == 0,
         "wxExecute must launch a quoted executable path with spaces and Unicode");

  std::unique_ptr<wxEventLoopBase> eventLoop(
      wxTheApp->GetTraits()->CreateEventLoop());
  wxEventLoopActivator activate(eventLoop.get());
  MonitoredProcess process;
  process.Redirect();
  const long pid = wxExecute(
      xgrib::QuoteProcessArgument(copiedExecutable.GetFullPath()) +
          " --waiting-child",
      wxEXEC_ASYNC | wxEXEC_HIDE_CONSOLE | wxEXEC_MAKE_GROUP_LEADER, &process);
  Expect(pid > 0 && wxProcess::Exists(static_cast<int>(pid)),
         "a running console helper must be detected");
  // Wait until exec and process-group setup finish before requesting cancel.
  for (int attempt = 0; attempt < 500 && !process.IsInputAvailable(); ++attempt) {
    wxYield();
    wxMilliSleep(10);
  }
  Expect(process.IsInputAvailable(), "console helper must reach its wait");
  wxKillError error = wxKILL_OK;
  xgrib::TerminateGeneratorProcess(pid, &error);
  Expect(error == wxKILL_OK, "console helper cancellation must succeed");
  for (int attempt = 0; attempt < 500 && !process.terminated; ++attempt) {
    eventLoop->DispatchTimeout(10);
    wxTheApp->ProcessPendingEvents();
  }
  Expect(process.terminated, "cancelled helper must deliver completion");
  Expect(!wxProcess::Exists(static_cast<int>(pid)),
         "a cancelled helper must not be reported as still running");

  wxRemoveFile(copiedExecutable.GetFullPath());
  wxRmdir(testDirectory.GetPath());

  std::cout << "xGRIB process command tests passed\n";
  return 0;
}
