file(READ "${SOURCE_FILE}" source)

set(required_patterns
  "m_processTimer\\(this\\)"
  "m_processTimer\\.Start\\(100\\)"
  "void EnvironmentalGribDialog::OnProcessTimer\\(wxTimerEvent& event\\)[^}]*DrainProcessOutput\\(\\)"
  "DrainStream\\(m_process->GetInputStream\\(\\), &m_stdoutBuffer"
  "DrainStream\\(m_process->GetErrorStream\\(\\), &m_stderrBuffer")

foreach(pattern IN LISTS required_patterns)
  if(NOT source MATCHES "${pattern}")
    message(FATAL_ERROR
      "Environmental GRIB process-output contract is missing: ${pattern}")
  endif()
endforeach()
