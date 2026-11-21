#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>

#include "ocpn_plugin.h"

class grib_pi;

#ifndef OCPN_HAVE_ENVIRONMENT_PROVIDER_V1
typedef int (*PlugInPlanningCancelledV1)(void* cancellation_context);
typedef void (*PlugInPlanningProgressV1)(void* progress_context,
                                         double progress);
typedef int (*PlugInEnvironmentRunV1)(
    void* provider_context, const char* request_json,
    PlugInPlanningCancelledV1 is_cancelled, void* cancellation_context,
    PlugInPlanningProgressV1 report_progress, void* progress_context,
    const char** result_json, const char** error_code,
    const char** error_message);
typedef int (*PlugInEnvironmentActivateV1)(
    void* provider_context, const char* provider_handle,
    const char** result_json, const char** error_code,
    const char** error_message);
struct PlugInEnvironmentProviderV1 {
  std::size_t struct_size;
  const char* capability;
  const char* display_name;
  const char* descriptor_json;
  void* provider_context;
  PlugInEnvironmentRunV1 run;
  PlugInEnvironmentActivateV1 activate;
};
#endif

/** Optional external-control adapter; a stock host is a supported no-op. */
class ExternalEnvironmentProvider {
public:
  explicit ExternalEnvironmentProvider(grib_pi& plugin);
  ~ExternalEnvironmentProvider();

  bool RegisterIfSupported();
  bool Unregister();

private:
  static int Run(void* context, const char* request_json,
                 PlugInPlanningCancelledV1 is_cancelled,
                 void* cancellation_context,
                 PlugInPlanningProgressV1 report_progress,
                 void* progress_context, const char** result_json,
                 const char** error_code, const char** error_message);
  static int Activate(void* context, const char* provider_handle,
                      const char** result_json, const char** error_code,
                      const char** error_message);
  int RunRequest(const char* request_json,
                 PlugInPlanningCancelledV1 is_cancelled,
                 void* cancellation_context,
                 PlugInPlanningProgressV1 report_progress,
                 void* progress_context, const char** result_json,
                 const char** error_code, const char** error_message);
  int ActivateDataset(const char* provider_handle, const char** result_json,
                      const char** error_code, const char** error_message);
  int Fail(const std::string& code, const std::string& message,
           const char** error_code, const char** error_message);

  grib_pi& plugin_;
  std::mutex run_mutex_;
  bool registered_{false};
  std::string result_;
  std::string error_code_;
  std::string error_message_;
};
