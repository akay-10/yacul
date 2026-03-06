#ifndef UTILS_LOGGING_LOGGER_H
#define UTILS_LOGGING_LOGGER_H

#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "absl/log/vlog_is_on.h"
#include "backward.hpp"
#include "basic/basic.h"

namespace utils {
namespace logging {

// Forward declarations
class CustomLogSink;

class Logger {
public:
  // Delete constructors for static class
  Logger() = delete;

  static void Init(int argc = 0, char **argv = nullptr);

  static void Shutdown();

  static void EnableSignalHandlers();

  static void DisableSignalHandlers();

  static bool IsInitialized() { return initialized_; }

private:
  static void WriteCrashTrace(int signal);

private:
  DISALLOW_COPY_AND_ASSIGN(Logger);

  static bool initialized_;
  static bool signal_handler_initialized_;
  static std::vector<int> installed_signals_;
  static std::unique_ptr<CustomLogSink> custom_sink_;
};

class CustomLogSink : public absl::LogSink {
public:
  typedef std::unique_ptr<CustomLogSink> UPtr;
  typedef std::unique_ptr<const CustomLogSink> UPtrConst;

  explicit CustomLogSink(const std::string &log_dir,
                         const std::string &app_name);
  ~CustomLogSink() override;

  void Send(const absl::LogEntry &entry) override;

  friend class Logger;

private:
  void OpenLogFile();

  std::string GetLogFileName() const;

  void BackwardPrinter(int signal);

private:
  std::atomic<bool> skip_log_to_file_;
  std::string log_dir_;
  std::ofstream log_file_;
  std::string app_name_;
  std::mutex file_mutex_;
};

} // namespace logging
} // namespace utils

#endif // UTILS_LOGGING_LOGGER_H
