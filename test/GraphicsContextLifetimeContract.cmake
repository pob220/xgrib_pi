file(READ "${DC_HEADER}" dc_header)
file(READ "${DC_SOURCE}" dc_source)
file(READ "${OVERLAY_SOURCE}" overlay_source)

if(NOT dc_header MATCHES "void SetDC\\(wxDC \\*dc_in\\);")
  message(FATAL_ERROR
    "pi_ocpnDC::SetDC must be implemented out of line so it can rebind graphics contexts")
endif()

if(NOT dc_source MATCHES
    "void pi_ocpnDC::SetDC\\(wxDC \\*dc_in\\)[^{]*\\{[^}]*delete pgc;[^}]*pgc = nullptr;")
  message(FATAL_ERROR
    "pi_ocpnDC::SetDC must discard the graphics context bound to the previous DC")
endif()

if(NOT dc_source MATCHES
    "dynamic_cast<wxMemoryDC \\*>\\(dc\\)[^;]*\\)[ \t\r\n]*pgc = wxGraphicsContext::Create\\(\\*pmdc\\)")
  message(FATAL_ERROR
    "pi_ocpnDC::SetDC must bind a graphics context to the current memory DC")
endif()

if(NOT overlay_source MATCHES
    "DoRenderGribOverlay\\(vp\\);[^}]*m_oDC->SetDC\\(nullptr\\);[^}]*m_pdc = nullptr;")
  message(FATAL_ERROR
    "The GRIB overlay must release its frame-local DC before returning to OpenCPN")
endif()
