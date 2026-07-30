file(READ "${CTRL_SOURCE}" ctrl_source)
file(READ "${PLUGIN_SOURCE}" plugin_source)

string(FIND "${ctrl_source}" "GRIBUICtrlBar::~GRIBUICtrlBar()" destructor_start)
string(FIND "${ctrl_source}" "void GRIBUICtrlBar::SaveState()" save_state_start)
if(destructor_start LESS 0 OR save_state_start LESS 0 OR
   save_state_start LESS_EQUAL destructor_start)
  message(FATAL_ERROR
    "GRIBUICtrlBar destructor and SaveState must remain separately identifiable")
endif()

math(EXPR destructor_length "${save_state_start} - ${destructor_start}")
string(SUBSTRING "${ctrl_source}" ${destructor_start} ${destructor_length}
       destructor_body)
if(destructor_body MATCHES "GetOCPNConfigObject")
  message(FATAL_ERROR
    "GRIBUICtrlBar destructor must not access host-owned configuration services")
endif()

string(FIND "${ctrl_source}" "void GRIBUICtrlBar::OnClose(" close_start)
string(FIND "${ctrl_source}" "void GRIBUICtrlBar::OnSize(" close_end)
if(close_start LESS 0 OR close_end LESS_EQUAL close_start)
  message(FATAL_ERROR "GRIBUICtrlBar::OnClose must remain identifiable")
endif()
math(EXPR close_length "${close_end} - ${close_start}")
string(SUBSTRING "${ctrl_source}" ${close_start} ${close_length} close_body)
if(NOT close_body MATCHES "SaveState\\(\\)")
  message(FATAL_ERROR
    "Normal control-bar close must persist state before window teardown")
endif()

string(FIND "${plugin_source}" "bool grib_pi::DeInit(" deinit_start)
string(FIND "${plugin_source}" "int grib_pi::GetAPIVersionMajor(" deinit_end)
if(deinit_start LESS 0 OR deinit_end LESS_EQUAL deinit_start)
  message(FATAL_ERROR "grib_pi::DeInit must remain identifiable")
endif()
math(EXPR deinit_length "${deinit_end} - ${deinit_start}")
string(SUBSTRING "${plugin_source}" ${deinit_start} ${deinit_length}
       deinit_body)
if(NOT deinit_body MATCHES "m_pGribCtrlBar->SaveState\\(\\)")
  message(FATAL_ERROR
    "Plugin deinitialisation must persist control-bar state while host services live")
endif()
