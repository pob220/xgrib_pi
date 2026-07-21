file(READ "${SOURCE_FILE}" source)

set(required_patterns
  "m_processTimer\\(this\\)"
  "m_processTimer\\.Start\\(100\\)"
  "void EnvironmentalGribDialog::OnProcessTimer\\(wxTimerEvent& event\\)[^}]*DrainProcessOutput\\(\\)"
  "DrainStream\\(m_process->GetInputStream\\(\\), &m_stdoutBuffer"
  "DrainStream\\(m_process->GetErrorStream\\(\\), &m_stderrBuffer"
  "request\\[\"offlineCurrentMode\"\\][^;]*wxString\\("
  "wxGetEnvMap\\(&env\\.env\\)"
  "env\\.env\\[\"ECCODES_DEFINITION_PATH\"\\]"
  "env\\.env\\[\"ECCODES_SAMPLES_PATH\"\\]"
  "env\\.env\\[\"PROJ_DATA\"\\]"
  "xGRIB: opened generated GRIB: %s"
  "packaged\\.SetFullName\\(\"environmental-grib\\.exe\"\\)")

foreach(pattern IN LISTS required_patterns)
  if(NOT source MATCHES "${pattern}")
    message(FATAL_ERROR
      "Environmental GRIB process-output contract is missing: ${pattern}")
  endif()
endforeach()
