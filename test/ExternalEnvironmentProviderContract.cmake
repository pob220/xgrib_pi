file(READ "${SOURCE_FILE}" source)

# wxJSON's Unicode build can bind a narrow string literal (or long long when
# 64-bit integer overloads are disabled) to bool.  The external protocol must
# therefore use explicit wxString and unambiguous numeric assignments.
set(required_literals
  "envelope[\"operation\"] = wxString(\"generateEnvironment\")"
  "envelope[\"credentials\"][\"copernicusPasswordEnvironment\"]"
  "wxString(\"ENVIRONMENTAL_GRIB_COPERNICUS_PASSWORD\")"
  "request[\"copernicusUsername\"] = copernicus_username"
  "Copernicus Marine Global Waves"
  "Copernicus Marine North-West Shelf"
  "Copernicus username is too long"
  "request[\"fallbackWaveProvider\"] = wxString(\"none\")"
  "request[\"fallbackCurrentSource\"] = wxString(\"none\")"
  "fields.Append(wxString(\"environmental-grib\"))"
  "response[\"validFromEpochSeconds\"] = static_cast<double>"
  "response[\"validToEpochSeconds\"] = static_cast<double>"
  "response[\"byteSize\"] = static_cast<double>"
  "wxString(\"environmental-grib job schema v1\")"
  "Append(wxString(\"provider:\") + weather)")

foreach(literal IN LISTS required_literals)
  string(FIND "${source}" "${literal}" offset)
  if(offset EQUAL -1)
    message(FATAL_ERROR
      "External environment provider wire contract is missing: ${literal}")
  endif()
endforeach()

set(forbidden_literals
  "envelope[\"operation\"] = \""
  "request[\"fallbackWaveProvider\"] = \""
  "request[\"fallbackCurrentSource\"] = \""
  "response[\"provenance\"].Append(\"")

foreach(literal IN LISTS forbidden_literals)
  string(FIND "${source}" "${literal}" offset)
  if(NOT offset EQUAL -1)
    message(FATAL_ERROR
      "External environment provider uses ambiguous wxJSON assignment: ${literal}")
  endif()
endforeach()
