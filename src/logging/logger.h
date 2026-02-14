#ifndef UTILS_LOGGING_LOGGER_H
#define UTILS_LOGGING_LOGGER_H

#include "basic/basic.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/log/check.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/log.h"
#include "absl/log/log_sink.h"
#include "absl/log/log_sink_registry.h"
#include "absl/log/vlog_is_on.h"
#include "absl/strings/str_format.h"
#include "backward.hpp"

namespace utils { namespace logging {

// Forward declarations
class CustomLogSink;

class Logger {
 public:
  // Delete constructors for static class
  Logger() = delete;

  static void Init(int argc, char** argv,
                   const std::string& app_name = "app",
                   const std::string& custom_log_dir = "");

  static void Shutdown();

  static void EnableSignalHandlers();

  static void DisableSignalHandlers();

  static bool IsInitialized() { return initialized_; }

 private:
  DISALLOW_COPY_AND_ASSIGN(Logger);

  static bool initialized_;
  static std::unique_ptr<CustomLogSink> custom_sink_;
  static std::unique_ptr<backward::SignalHandling> signal_handler_;

  // Signal handler implementation
  static void InstallSignalHandlers();
  static void CustomSignalHandler(int signal);
};

class CustomLogSink : public absl::LogSink {
 public:
  typedef std::unique_ptr<CustomLogSink> UPtr;
  typedef std::unique_ptr<const CustomLogSink> UPtrConst;

  explicit CustomLogSink(const std::string& log_dir,
                         const std::string& app_name);
  ~CustomLogSink() override;

  void Send(const absl::LogEntry& entry) override;

 private:
  std::string log_dir_;
  std::ofstream log_file_;
  std::string app_name_;
  std::mutex file_mutex_;

  void OpenLogFile();
  std::string GetLogFileName() const;
};

}} // namespace utils::logging

#endif // UTILS_LOGGING_LOGGER_H
