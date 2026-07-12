#pragma once

class wxConfigBase;

// xGRIB and bundled grib_pi implement the same overlays and plugin-message
// protocol. Running both is ambiguous, so xGRIB remains inactive while the
// bundled plugin is enabled.
bool IsBundledGribPluginEnabled(wxConfigBase* config);
