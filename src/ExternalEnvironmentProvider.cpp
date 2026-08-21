#include "ExternalEnvironmentProvider.h"

#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <csignal>
#include <dlfcn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

#include <wx/app.h>
#include <wx/datetime.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/jsonreader.h>
#include <wx/jsonwriter.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>

#include "XgribPaths.h"
#include "FileDigest.h"

bool OpenExternalGribDataset(grib_pi& plugin, const wxString& path,
                             wxString* error);

namespace {
using RegisterFunction = bool (*)(const char*,
                                  const PlugInEnvironmentProviderV1*);
using UnregisterFunction = bool (*)(const char*);

#ifndef _WIN32
template <typename T>
T Resolve(const char* name) {
  return reinterpret_cast<T>(dlsym(RTLD_DEFAULT, name));
}
#else
template <typename T>
T Resolve(const char* name) {
  return reinterpret_cast<T>(GetProcAddress(GetModuleHandle(nullptr), name));
}
#endif

constexpr const char* kCapability = "environmental-data.xgrib.v1";

constexpr const char* kDescriptor = R"json({
  "schemaVersion": 1,
  "cancellable": true,
  "maximumConcurrentJobs": 1,
  "fields": [
    {"name":"bboxSouthWest","label":"Area south-west","type":"coordinate","required":true},
    {"name":"bboxNorthEast","label":"Area north-east","type":"coordinate","required":true},
    {"name":"startUtc","label":"Forecast start (UTC)","type":"string","required":true},
    {"name":"hours","label":"Duration","type":"integer","required":true,"unit":"h","defaultValue":"72","minimum":1,"maximum":8760},
    {"name":"stepHours","label":"Time step","type":"integer","required":true,"unit":"h","defaultValue":"3","minimum":1,"maximum":12},
    {"name":"weatherProvider","label":"Weather provider","type":"resource","required":true,"resourceKind":"weather-provider","defaultValue":"gfs"},
    {"name":"weatherPreset","label":"Weather fields","type":"resource","required":true,"resourceKind":"weather-preset","defaultValue":"routing"},
    {"name":"extendForecast","label":"Extend forecast","type":"boolean","required":false,"defaultValue":"false"},
    {"name":"fallbackWeatherProvider","label":"Weather fallback","type":"resource","required":false,"resourceKind":"weather-provider","defaultValue":"none"},
    {"name":"includeWaves","label":"Include waves","type":"boolean","required":false,"defaultValue":"false"},
    {"name":"waveProvider","label":"Wave provider","type":"resource","required":false,"resourceKind":"wave-provider","defaultValue":"gfs_wave"},
    {"name":"currentSource","label":"Current source","type":"resource","required":false,"resourceKind":"current-source","defaultValue":"none"},
    {"name":"weatherGridSpacingDegrees","label":"Weather grid spacing","type":"number","required":false,"unit":"degrees","defaultValue":"0.25","minimum":0.01,"maximum":5},
    {"name":"currentGridSpacingDegrees","label":"Current grid spacing","type":"number","required":false,"unit":"degrees","defaultValue":"0.05","minimum":0.005,"maximum":5}
  ],
  "resources": [
    {"kind":"weather-provider","identity":"none","label":"None"},
    {"kind":"weather-provider","identity":"gfs","label":"NOAA GFS"},
    {"kind":"weather-provider","identity":"noaa_hrrr","label":"NOAA HRRR"},
    {"kind":"weather-provider","identity":"ukmo_ukv","label":"Met Office UKV"},
    {"kind":"weather-provider","identity":"metno_nordic","label":"MET Norway Nordic"},
    {"kind":"weather-provider","identity":"dwd_icon_eu","label":"DWD ICON-EU"},
    {"kind":"weather-provider","identity":"ecmwf_ifs_open","label":"ECMWF IFS Open"},
    {"kind":"weather-provider","identity":"ecmwf_aifs_open","label":"ECMWF AIFS Open"},
    {"kind":"weather-preset","identity":"minimal","label":"Minimal wind"},
    {"kind":"weather-preset","identity":"routing","label":"Routing"},
    {"kind":"weather-preset","identity":"marine","label":"Marine comfort"},
    {"kind":"weather-preset","identity":"all","label":"All available display data"},
    {"kind":"wave-provider","identity":"gfs_wave","label":"NOAA GFS Wave"},
    {"kind":"current-source","identity":"none","label":"None"},
    {"kind":"current-source","identity":"noaa_rtofs_global","label":"NOAA RTOFS global"}
  ]
})json";

bool ReadJsonFile(const wxString& path, wxJSONValue* value) {
  wxFile file(path);
  wxString text;
  if (!file.IsOpened() || !file.ReadAll(&text)) return false;
  wxJSONReader reader;
  return reader.Parse(text, value) == 0;
}

bool WriteJsonFile(const wxString& path, const wxJSONValue& value) {
  wxJSONWriter writer(wxJSONWRITER_STYLED);
  wxString text;
  writer.Write(value, text);
  wxFile file(path, wxFile::write);
  return file.IsOpened() && file.Write(text);
}

bool JsonCoordinate(wxJSONValue& parameters, const char* name,
                    double* latitude, double* longitude) {
  if (!parameters.HasMember(name) || !parameters[name].IsObject()) return false;
  auto& value = parameters[name];
  if (!value.HasMember("latitudeDegrees") ||
      !value.HasMember("longitudeDegrees") ||
      !(value["latitudeDegrees"].IsDouble() ||
        value["latitudeDegrees"].IsInt()) ||
      !(value["longitudeDegrees"].IsDouble() ||
        value["longitudeDegrees"].IsInt()))
    return false;
  *latitude = value["latitudeDegrees"].AsDouble();
  *longitude = value["longitudeDegrees"].AsDouble();
  return std::isfinite(*latitude) && std::isfinite(*longitude) &&
         *latitude >= -90.0 && *latitude <= 90.0 && *longitude >= -180.0 &&
         *longitude <= 180.0;
}

std::int64_t ParseUtc(const std::string& text) {
  std::tm value{};
  std::istringstream input(text);
  input >> std::get_time(&value, "%Y-%m-%dT%H:%M:%SZ");
  if (input.fail()) return -1;
#ifdef _WIN32
  return static_cast<std::int64_t>(_mkgmtime(&value));
#else
  return static_cast<std::int64_t>(timegm(&value));
#endif
}

std::int64_t ParseInspectionTime(const std::string& text) {
  std::tm value{};
  std::istringstream input(text);
  input >> std::get_time(&value, "%Y%m%dT%H%M");
  if (input.fail()) return -1;
#ifdef _WIN32
  return static_cast<std::int64_t>(_mkgmtime(&value));
#else
  return static_cast<std::int64_t>(timegm(&value));
#endif
}

wxString FindHelper() {
  wxString overridden;
  if (wxGetEnv("ENVIRONMENTAL_GRIB_GENERATOR", &overridden) &&
      wxFileExists(overridden))
    return overridden;
  wxFileName packaged(GetXgribDataDirectory(), "");
  packaged.RemoveLastDir();
  packaged.AppendDir("bin");
#ifdef _WIN32
  packaged.SetFullName("environmental-grib.exe");
#else
  packaged.SetFullName("environmental-grib");
#endif
  if (wxFileExists(packaged.GetFullPath())) return packaged.GetFullPath();
  return {};
}

int RunProcess(const wxString& executable, const wxString& job,
               const wxString& result,
               PlugInPlanningCancelledV1 is_cancelled,
               void* cancellation_context) {
#ifndef _WIN32
  const auto executable_bytes = executable.ToUTF8();
  const auto job_bytes = job.ToUTF8();
  const auto result_bytes = result.ToUTF8();
  const pid_t child = fork();
  if (child == 0) {
    execl(executable_bytes.data(), executable_bytes.data(), "run-job", "--job",
          job_bytes.data(), "--result", result_bytes.data(),
          static_cast<char*>(nullptr));
    _exit(127);
  }
  if (child < 0) return -1;
  int status = 0;
  while (waitpid(child, &status, WNOHANG) == 0) {
    if (is_cancelled && is_cancelled(cancellation_context)) {
      kill(child, SIGTERM);
      waitpid(child, &status, 0);
      return -2;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
  wxString command = "\"" + executable + "\" run-job --job \"" + job +
                     "\" --result \"" + result + "\"";
  std::vector<wchar_t> writable(command.wc_str(),
                                command.wc_str() + command.length() + 1);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(nullptr, writable.data(), nullptr, nullptr, FALSE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process))
    return -1;
  while (WaitForSingleObject(process.hProcess, 100) == WAIT_TIMEOUT) {
    if (is_cancelled && is_cancelled(cancellation_context)) {
      TerminateProcess(process.hProcess, 2);
      WaitForSingleObject(process.hProcess, INFINITE);
      CloseHandle(process.hThread);
      CloseHandle(process.hProcess);
      return -2;
    }
  }
  DWORD exit_code = 1;
  GetExitCodeProcess(process.hProcess, &exit_code);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return static_cast<int>(exit_code);
#endif
}

std::string JsonText(const wxJSONValue& value) {
  wxJSONWriter writer;
  wxString text;
  writer.Write(value, text);
  return text.ToStdString();
}
}  // namespace

ExternalEnvironmentProvider::ExternalEnvironmentProvider(grib_pi& plugin)
    : plugin_(plugin) {}

ExternalEnvironmentProvider::~ExternalEnvironmentProvider() { Unregister(); }

bool ExternalEnvironmentProvider::RegisterIfSupported() {
  if (registered_) return true;
  const auto register_provider =
      Resolve<RegisterFunction>("PlugIn_RegisterEnvironmentProviderV1");
  if (!register_provider) {
    wxLogMessage("xGRIB: host has no environmental provider service; "
                 "continuing with normal plugin features");
    return false;
  }
  PlugInEnvironmentProviderV1 provider{};
  provider.struct_size = sizeof(provider);
  provider.capability = kCapability;
  provider.display_name = "xGRIB environmental acquisition";
  provider.descriptor_json = kDescriptor;
  provider.provider_context = this;
  provider.run = &ExternalEnvironmentProvider::Run;
  provider.activate = &ExternalEnvironmentProvider::Activate;
  registered_ = register_provider("xGRIB", &provider);
  if (!registered_)
    wxLogWarning("xGRIB: environmental provider registration failed");
  return registered_;
}

bool ExternalEnvironmentProvider::Unregister() {
  if (!registered_) return true;
  const auto unregister = Resolve<UnregisterFunction>(
      "PlugIn_UnregisterEnvironmentProvidersV1");
  if (!unregister) {
    registered_ = false;
    return true;
  }
  if (!unregister("xGRIB")) return false;
  registered_ = false;
  return true;
}

int ExternalEnvironmentProvider::Run(
    void* context, const char* request_json,
    PlugInPlanningCancelledV1 is_cancelled, void* cancellation_context,
    PlugInPlanningProgressV1 report_progress, void* progress_context,
    const char** result_json, const char** error_code,
    const char** error_message) {
  return static_cast<ExternalEnvironmentProvider*>(context)->RunRequest(
      request_json, is_cancelled, cancellation_context, report_progress,
      progress_context, result_json, error_code, error_message);
}

int ExternalEnvironmentProvider::Activate(
    void* context, const char* provider_handle, const char** result_json,
    const char** error_code, const char** error_message) {
  return static_cast<ExternalEnvironmentProvider*>(context)->ActivateDataset(
      provider_handle, result_json, error_code, error_message);
}

int ExternalEnvironmentProvider::Fail(const std::string& code,
                                      const std::string& message,
                                      const char** error_code,
                                      const char** error_message) {
  error_code_ = code;
  error_message_ = message;
  if (error_code) *error_code = error_code_.c_str();
  if (error_message) *error_message = error_message_.c_str();
  return 0;
}

int ExternalEnvironmentProvider::RunRequest(
    const char* request_json, PlugInPlanningCancelledV1 is_cancelled,
    void* cancellation_context, PlugInPlanningProgressV1 report_progress,
    void* progress_context, const char** result_json, const char** error_code,
    const char** error_message) {
  std::unique_lock<std::mutex> single_run(run_mutex_, std::try_to_lock);
  if (!single_run.owns_lock())
    return Fail("provider_busy", "xGRIB accepts one acquisition at a time",
                error_code, error_message);
  wxJSONValue root;
  wxJSONReader reader;
  if (!request_json || reader.Parse(wxString::FromUTF8(request_json), &root) ||
      !root.IsObject() || !root.HasMember("parameters") ||
      !root["parameters"].IsObject())
    return Fail("invalid_request", "Invalid environmental request JSON",
                error_code, error_message);
  auto& parameters = root["parameters"];
  double south = 0.0, west = 0.0, north = 0.0, east = 0.0;
  if (!JsonCoordinate(parameters, "bboxSouthWest", &south, &west) ||
      !JsonCoordinate(parameters, "bboxNorthEast", &north, &east) ||
      south >= north || west >= east)
    return Fail("invalid_area", "A non-wrapping WGS84 bounding box is required",
                error_code, error_message);
  if (!parameters.HasMember("startUtc") ||
      !parameters["startUtc"].IsString() || !parameters.HasMember("hours") ||
      !parameters["hours"].IsInt() ||
      !parameters.HasMember("stepHours") ||
      !parameters["stepHours"].IsInt())
    return Fail("invalid_request", "startUtc, hours and stepHours are required",
                error_code, error_message);
  const std::string start = parameters["startUtc"].AsString().ToStdString();
  const auto start_epoch = ParseUtc(start);
  const int hours = parameters["hours"].AsInt();
  const int step = parameters["stepHours"].AsInt();
  if (start_epoch < 0 || hours < 1 || hours > 8760 || step < 1 || step > 12)
    return Fail("invalid_request", "Forecast time constraints are invalid",
                error_code, error_message);

  const auto text = [&](const char* name, const char* fallback) {
    return parameters.HasMember(name) && parameters[name].IsString()
               ? parameters[name].AsString()
               : wxString(fallback);
  };
  const auto boolean = [&](const char* name, bool fallback) {
    return parameters.HasMember(name) && parameters[name].IsBool()
               ? parameters[name].AsBool()
               : fallback;
  };
  const auto number = [&](const char* name, double fallback) {
    return parameters.HasMember(name) &&
                   (parameters[name].IsDouble() || parameters[name].IsInt())
               ? parameters[name].AsDouble()
               : fallback;
  };
  const wxString weather = text("weatherProvider", "gfs");
  const wxString preset = text("weatherPreset", "routing");
  const wxString wave = text("waveProvider", "gfs_wave");
  const wxString current = text("currentSource", "none");
  const bool include_waves = boolean("includeWaves", false);

  wxFileName directory(*GetpPrivateApplicationDataLocation(), "");
  directory.AppendDir("xgrib_pi");
  directory.AppendDir("scheduler-datasets");
  if (!wxDirExists(directory.GetPath()) &&
      !wxFileName::Mkdir(directory.GetPath(), wxS_DIR_DEFAULT,
                         wxPATH_MKDIR_FULL))
    return Fail("storage_unavailable", "Cannot create xGRIB dataset directory",
                error_code, error_message);
  const wxString token = wxString::Format(
      "%ld-%lld", wxGetProcessId(),
      static_cast<long long>(wxDateTime::UNow().GetValue().GetValue()));
  wxFileName output(directory.GetPath(), "scheduled-" + token + ".grb2");
  wxFileName job(directory.GetPath(), ".job-" + token + ".json");
  wxFileName job_result(directory.GetPath(), ".result-" + token + ".json");
  wxJSONValue envelope;
  envelope["schemaVersion"] = 1;
  envelope["operation"] = "generateEnvironment";
  auto& request = envelope["request"];
  request["bbox"]["west"] = west;
  request["bbox"]["south"] = south;
  request["bbox"]["east"] = east;
  request["bbox"]["north"] = north;
  request["start"] = wxString::FromUTF8(start);
  request["hours"] = hours;
  request["stepHours"] = step;
  request["weatherProvider"] = weather;
  request["weatherPreset"] = preset;
  request["extendForecast"] = boolean("extendForecast", false);
  request["fallbackWeatherProvider"] =
      text("fallbackWeatherProvider", "none");
  request["fallbackWaveProvider"] = "none";
  request["fallbackCurrentSource"] = "none";
  request["weatherGridSpacingDeg"] =
      number("weatherGridSpacingDegrees", 0.25);
  request["includeWaves"] = include_waves;
  request["waveProvider"] = wave;
  request["waveStepHours"] = 3;
  request["currentSource"] = current;
  request["currentGridSpacingDeg"] =
      number("currentGridSpacingDegrees", 0.05);
  request["output"] = output.GetFullPath();
  request["overwrite"] = true;
  request["keepIntermediate"] = false;
  request["dryRun"] = false;
  if (!WriteJsonFile(job.GetFullPath(), envelope))
    return Fail("storage_unavailable", "Cannot write acquisition job",
                error_code, error_message);
  const wxString helper = FindHelper();
  if (helper.empty()) {
    wxRemoveFile(job.GetFullPath());
    return Fail("provider_unavailable", "environmental-grib helper not found",
                error_code, error_message);
  }
  if (report_progress) report_progress(progress_context, 0.05);
  const int exit_code = RunProcess(helper, job.GetFullPath(),
                                   job_result.GetFullPath(), is_cancelled,
                                   cancellation_context);
  wxJSONValue generated;
  const bool result_read = ReadJsonFile(job_result.GetFullPath(), &generated);
  wxRemoveFile(job.GetFullPath());
  wxRemoveFile(job_result.GetFullPath());
  if (exit_code == -2)
    return Fail("cancelled", "Environmental acquisition was cancelled",
                error_code, error_message);
  if (exit_code != 0 || !result_read ||
      generated["status"].AsString() != "complete" ||
      !generated.HasMember("result")) {
    const auto message = result_read && generated.HasMember("error")
                             ? generated["error"]["message"].AsString()
                             : wxString("environmental-grib failed");
    return Fail("generation_failed", message.ToStdString(), error_code,
                error_message);
  }
  if (report_progress) report_progress(progress_context, 0.9);
  auto& helper_result = generated["result"];
  auto& inspection = helper_result["inspection"];
  const auto checksum_value =
      xgrib::Sha256File(std::filesystem::path(output.GetFullPath().ToStdString()));
  if (!checksum_value)
    return Fail("validation_failed", "Cannot hash generated dataset",
                error_code, error_message);
  const std::string& checksum = *checksum_value;
  std::int64_t valid_from = start_epoch;
  std::int64_t valid_to = start_epoch + static_cast<std::int64_t>(hours) * 3600;
  if (inspection.HasMember("first_valid_time")) {
    const auto parsed = ParseInspectionTime(
        inspection["first_valid_time"].AsString().ToStdString());
    if (parsed >= 0) valid_from = parsed;
  }
  if (inspection.HasMember("last_valid_time")) {
    const auto parsed = ParseInspectionTime(
        inspection["last_valid_time"].AsString().ToStdString());
    if (parsed > valid_from) valid_to = parsed;
  }
  wxJSONValue fields(wxJSONTYPE_ARRAY);
  if (inspection.HasMember("short_name_counts") &&
      inspection["short_name_counts"].IsObject()) {
    for (const auto& name : inspection["short_name_counts"].GetMemberNames())
      fields.Append(name);
  }
  if (fields.Size() == 0) fields.Append("environmental-grib");
  wxJSONValue response;
  response["identity"] = "xgrib-" +
                         wxString::FromUTF8(checksum.substr(0, 20));
  response["providerHandle"] = output.GetFullPath();
  response["model"] = weather + (include_waves ? "+" + wave : "") +
                      (current != "none" ? "+" + current : "");
  response["cycle"] = helper_result.HasMember("selected_cycle")
                            ? helper_result["selected_cycle"].AsString()
                            : wxString::FromUTF8(start);
  auto& coverage = inspection["coverage"];
  response["coverage"]["south"] =
      coverage.HasMember("south") ? coverage["south"].AsDouble() : south;
  response["coverage"]["west"] =
      coverage.HasMember("west") ? coverage["west"].AsDouble() : west;
  response["coverage"]["north"] =
      coverage.HasMember("north") ? coverage["north"].AsDouble() : north;
  response["coverage"]["east"] =
      coverage.HasMember("east") ? coverage["east"].AsDouble() : east;
  response["validFromEpochSeconds"] = static_cast<wxLongLong_t>(valid_from);
  response["validToEpochSeconds"] = static_cast<wxLongLong_t>(valid_to);
  response["fields"] = fields;
  response["checksumSha256"] = wxString::FromUTF8(checksum);
  response["byteSize"] = static_cast<wxLongLong_t>(
      helper_result["byte_count"].AsUInt64());
  response["provenance"].Append("environmental-grib job schema v1");
  response["provenance"].Append("provider:" + weather);
  result_ = JsonText(response);
  if (result_json) *result_json = result_.c_str();
  if (report_progress) report_progress(progress_context, 1.0);
  return 1;
}

int ExternalEnvironmentProvider::ActivateDataset(
    const char* provider_handle, const char** result_json,
    const char** error_code, const char** error_message) {
  std::unique_lock<std::mutex> single_run(run_mutex_, std::try_to_lock);
  if (!single_run.owns_lock())
    return Fail("provider_busy", "xGRIB is acquiring another dataset",
                error_code, error_message);
  if (!provider_handle || !*provider_handle || !wxFileExists(provider_handle))
    return Fail("dataset_unavailable", "Generated dataset no longer exists",
                error_code, error_message);
  const wxString path = wxString::FromUTF8(provider_handle);
  bool success = false;
  wxString activation_error;
  if (wxIsMainThread()) {
    success = OpenExternalGribDataset(plugin_, path, &activation_error);
  } else if (wxTheApp) {
    struct State {
      std::mutex mutex;
      std::condition_variable changed;
      bool complete{false};
      bool success{false};
      wxString error;
    };
    auto state = std::make_shared<State>();
    wxTheApp->CallAfter([this, state, path] {
      state->success = OpenExternalGribDataset(plugin_, path, &state->error);
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->complete = true;
      }
      state->changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(state->mutex);
    if (!state->changed.wait_for(lock, std::chrono::seconds(10),
                                 [&] { return state->complete; }))
      return Fail("activation_timeout", "OpenCPN did not activate dataset",
                  error_code, error_message);
    success = state->success;
    activation_error = state->error;
  }
  if (!success)
    return Fail("activation_failed",
                activation_error.empty() ? "xGRIB could not display dataset"
                                         : activation_error.ToStdString(),
                error_code, error_message);
  result_ = "{\"active\":true}";
  if (result_json) *result_json = result_.c_str();
  return 1;
}
