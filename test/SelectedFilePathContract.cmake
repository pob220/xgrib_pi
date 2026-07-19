file(READ "${SOURCE_FILE}" source)

set(selection_contract
  "dialog->GetPaths(m_file_names);\n    if (!m_file_names.IsEmpty()) {\n      SetTitle(_(\"File: \") + m_file_names[0]);\n      Update();\n    }\n    OpenFile();")
string(FIND "${source}" "${selection_contract}" selection_contract_index)
if(selection_contract_index EQUAL -1)
  message(FATAL_ERROR
    "Selected GRIB path must be displayed before the file is loaded")
endif()

if(NOT source MATCHES "title.Append\\(fn.GetFullPath\\(\\)\\);")
  message(FATAL_ERROR
    "Loaded GRIB title must retain the selected file's full path")
endif()

file(STRINGS "${POTFILES_FILE}" potfiles_lines)
list(FIND potfiles_lines "src/XyGribPanel.cpp" xygrib_panel_index)
if(xygrib_panel_index EQUAL -1)
  message(FATAL_ERROR
    "POTFILES.in must contain src/XyGribPanel.cpp")
endif()

foreach(line IN LISTS potfiles_lines)
  if(line MATCHES "src\\\\XyGribPanel\\.cpp")
    message(FATAL_ERROR
      "POTFILES.in must not use a backslash for src/XyGribPanel.cpp")
  endif()
endforeach()
