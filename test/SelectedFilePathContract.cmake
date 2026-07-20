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

file(READ "${ENVIRONMENTAL_DIALOG_SOURCE}" environmental_dialog_source)
foreach(path_control IN ITEMS m_existingWeatherPath m_existingCurrentPath)
  if(NOT environmental_dialog_source MATCHES
      "${path_control}->ChangeValue\\(")
    message(FATAL_ERROR
      "Environmental GRIB selections must immediately display the full path in ${path_control}")
  endif()
endforeach()

foreach(picker_control IN ITEMS m_existingWeatherFile m_existingCurrentFile)
  if(NOT environmental_dialog_source MATCHES
      "${picker_control}->GetPath\\(\\)")
    message(FATAL_ERROR
      "Environmental GRIB path displays must use the accepted ${picker_control} path")
  endif()
endforeach()

if(NOT environmental_dialog_source MATCHES
    "wxEVT_FILEPICKER_CHANGED[^;]*OnExistingGribFileChanged")
  message(FATAL_ERROR
    "Environmental GRIB path displays must be updated by file-picker events")
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
