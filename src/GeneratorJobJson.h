#pragma once

#include <wx/jsonval.h>

// Create the versioned fields shared by every native generator job. Explicit
// wxString values avoid wxJSON's const-char-to-bool overload trap.
wxJSONValue CreateGeneratorJobEnvelope();
