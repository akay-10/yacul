#include <csignal>
#include <filesystem> // for filesystem
#include <sstream>

#include "absl/flags/parse.h"
#include "absl/log/globals.h"
#include "absl/log/initialize.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log_sink_registry.h"
#include "absl/strings/str_split.h"
#include "backward.hpp"
#include "logger.h"

using namespace std;
namespace fs = filesystem;

// Define flags for log directory and prefix
ABSL_FLAG(std::string, log_dir, "", "Directory to dump logs");
ABSL_FLAG(bool, enable_custom_log_sink, true,
          "Enable custom log sink for directory output");
ABSL_FLAG(bool, enable_snippet_for_backward_cpp, false,
          "Enable small function call snippet in the stack trace on catching "
          "a signal");

namespace utils {
namespace logging {

//------------------------------------------------------------------------------

// Initialize static members
bool Logger::initialized_ = false;
bool Logger::signal_handler_initialized_ = false;
vector<int> Logger::installed_signals_;
CustomLogSink::UPtr Logger::custom_sink_ = nullptr;

//------------------------------------------------------------------------------

void Logger::Init(int argc, char **argv) {
  if (initialized_) {
    LOG(WARNING) << "Logger already initialized";
    return;
  }

  if (argc == 0 || argv == nullptr) {
    std::cerr << "No command line arguments provided; using defaults\n";
    argc = 1;
    static char *default_argv[] = {const_cast<char *>("app")};
    argv = default_argv;
  }

  // Parse abseil flags
  absl::ParseCommandLine(argc, argv);

  // Initialize abseil logging
  absl::InitializeLog();

  // Hacky way for disabling absl's own stack dump on check failures
  absl::log_internal::SetMaxFramesInLogStackTrace(0);

  // Set default log level to INFO
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // Setup custom log directory
  string log_dir = absl::GetFlag(FLAGS_log_dir);

  if (!log_dir.empty()) {
    // Create log directory if it doesn't exist
    try {
      if (!fs::exists(log_dir)) {
        fs::create_directories(log_dir);
      }
    } catch (const fs::filesystem_error &e) {
      LOG(ERROR) << "Failed to create log directory: " << log_dir
                 << ", error: " << e.what();
    }
  }

  // Register custom log sink if enabled
  if (absl::GetFlag(FLAGS_enable_custom_log_sink)) {
    vector<string> app_name = absl::StrSplit(argv[0], '/');
    LOG(INFO) << "Log file prefix name is " << app_name.back();
    custom_sink_ = make_unique<CustomLogSink>(log_dir, app_name.back());
    absl::AddLogSink(custom_sink_.get());
  }

  initialized_ = true;
  LOG(INFO) << "Logger initialized successfully";
}

//------------------------------------------------------------------------------

void Logger::Shutdown() {
  if (!initialized_) {
    return;
  }

  LOG(INFO) << "Shutting down logger";
  // Remove custom sink
  if (custom_sink_) {
    absl::RemoveLogSink(custom_sink_.get());
    custom_sink_.reset();
  }
  DisableSignalHandlers();
  initialized_ = false;
}

//------------------------------------------------------------------------------

void Logger::EnableSignalHandlers() {
  if (signal_handler_initialized_) {
    LOG(WARNING) << "Signal handlers already enabled";
    return;
  }

  // Set up alternate signal stack, static so it's not on the stack itself
  static uint8_t alternate_stack[65536]; // 64KB alternate stack
  stack_t ss;
  ss.ss_sp = alternate_stack;
  ss.ss_size = sizeof(alternate_stack);
  ss.ss_flags = 0;
  if (sigaltstack(&ss, nullptr) != 0) {
    LOG(ERROR) << "Failed to set up alternate signal stack: "
               << strerror(errno);
    return;
  }

  // Install custom signal handlers manually
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = [](int sig, siginfo_t *info, void *ctx) {
    Logger::WriteCrashTrace(sig);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
  };

  // Track installed signals
  installed_signals_ = {SIGSEGV, SIGABRT, SIGFPE,  SIGILL, SIGBUS,
                        SIGTERM, SIGINT,  SIGQUIT, SIGSYS, SIGPIPE};

  for (int sig : installed_signals_) {
    sigaction(sig, &sa, nullptr);
  }

  signal_handler_initialized_ = true;

  LOG(INFO) << "Signal handlers enabled with backward-cpp";
}

//------------------------------------------------------------------------------

void Logger::WriteCrashTrace(int signal) {
  CHECK(custom_sink_);
  custom_sink_->BackwardPrinter(signal);
}

//------------------------------------------------------------------------------

void Logger::DisableSignalHandlers() {
  if (!signal_handler_initialized_) {
    LOG(WARNING) << "Signal handlers not enabled";
    return;
  }

  // Restore default handlers for tracked signals
  for (int sig : installed_signals_) {
    ::signal(sig, SIG_DFL);
  }

  installed_signals_.clear();
  signal_handler_initialized_ = false;
  LOG(INFO) << "Signal handlers disabled and restored to default";
}

//------------------------------------------------------------------------------

CustomLogSink::CustomLogSink(const string &log_dir, const string &app_name)
    : log_dir_(log_dir), app_name_(app_name),
      skip_log_to_file_(log_dir.empty()) {

  if (skip_log_to_file_) {
    LOG(WARNING) << "Log directory is empty. Logs will not be written to file.";
    return;
  }
  OpenLogFile();
}

//------------------------------------------------------------------------------

CustomLogSink::~CustomLogSink() {
  lock_guard<mutex> lock(file_mutex_);
  if (log_file_.is_open()) {
    log_file_.close();
  }
}

//------------------------------------------------------------------------------

void CustomLogSink::Send(const absl::LogEntry &entry) {
  if (skip_log_to_file_) {
    return;
  }
  lock_guard<mutex> lock(file_mutex_);
  if (!log_file_.is_open()) {
    OpenLogFile();
  }
  if (log_file_.is_open()) {
    log_file_ << entry.text_message_with_prefix_and_newline();
    log_file_.flush();
  }
}

//------------------------------------------------------------------------------

void CustomLogSink::BackwardPrinter(int signal) {
  backward::StackTrace st;
  st.load_here(16); // Capture up to 16 frames

  backward::Printer p;
  p.object = true;
  p.color_mode = backward::ColorMode::never;
  p.address = true;
  p.snippet = absl::GetFlag(FLAGS_enable_snippet_for_backward_cpp);

  std::ostringstream oss;
  oss << "\n=== CRASH DETECTED (Signal: " << signal << ") ===\n";
  p.print(st, oss);
  oss << "\n=== END CRASH TRACE ===\n";
  std::cerr << oss.str();
  if (!skip_log_to_file_ && log_file_.is_open()) {
    log_file_ << oss.str();
    log_file_.flush();
  }
}

//------------------------------------------------------------------------------

void CustomLogSink::OpenLogFile() {
  string filename = GetLogFileName();
  string filepath = log_dir_ + "/" + filename;
  LOG(INFO) << "Opening log file " << filepath;

  log_file_.open(filepath, ios::out | ios::app);

  if (!log_file_.is_open()) {
    LOG(ERROR) << "Failed to open log file: " << filepath << endl;
  }
}

//------------------------------------------------------------------------------

string CustomLogSink::GetLogFileName() const {
  // Generate filename with timestamp and process ID
  auto now = chrono::system_clock::now();
  auto time_t = chrono::system_clock::to_time_t(now);
  ostringstream oss;
  oss << app_name_ << "_" << put_time(localtime(&time_t), "%Y%m%d_%H%M%S")
      << "_" << getpid() << ".log";
  return oss.str();
}

//------------------------------------------------------------------------------

} // namespace logging
} // namespace utils
