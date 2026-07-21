#pragma once

#include <wx/string.h>

namespace xgrib {

// Quote one argument for the native command-line parser used by wxExecute.
// Windows CreateProcess uses double-quote/backslash rules; POSIX uses the
// shell's single-quote rules.
wxString QuoteProcessArgument(const wxString& value);

}  // namespace xgrib
