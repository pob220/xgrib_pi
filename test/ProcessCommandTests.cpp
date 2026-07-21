#include <cstdlib>
#include <iostream>

#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/init.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "ProcessCommand.h"

namespace {

void Expect(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && wxString::FromUTF8(argv[1]) == "--child") return 0;

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
  wxRemoveFile(copiedExecutable.GetFullPath());
  wxRmdir(testDirectory.GetPath());
  Expect(exitCode == 0,
         "wxExecute must launch a quoted executable path with spaces and Unicode");

  std::cout << "xGRIB process command tests passed\n";
  return 0;
}
