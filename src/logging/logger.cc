#include <csignal>
#include <filesystem> // for filesystem
#include <sstream>

#include "logger.h"
#include "backward.hpp"

using namespace std;
namespace fs = filesystem;

// Define flags for log directory and prefix
ABSL_FLAG(std::string, log_dir, "/home/akayhomie/code/utils/logs",
          "Directory to dump logs");
ABSL_FLAG(bool, enable_custom_log_sink, true,
          "Enable custom log sink for directory output");
ABSL_FLAG(bool, enable_snippet_for_backward_cpp, false,
          "Enable small function call snippet in the stack trace on catching "
          "a signal");

namespace utils { namespace logging {

//------------------------------------------------------------------------------

// Initialize static members
bool Logger::initialized_ = false;
bool Logger::signal_handler_initialized_ = false;
vector<int> Logger::installed_signals_;
CustomLogSink::UPtr Logger::custom_sink_ = nullptr;

//------------------------------------------------------------------------------

void Logger::Init(int argc, char** argv, const string& app_name,
                  const string& custom_log_dir) {
  if (initialized_) {
    LOG(WARNING) << "Logger already initialized";
    return;
  }

  // Parse abseil flags
  absl::ParseCommandLine(argc, argv);

  // Initialize abseil logging
  absl::InitializeLog();

  // Set default log level to INFO 
  absl::SetMinLogLevel(absl::LogSeverityAtLeast::kInfo);
  absl::SetStderrThreshold(absl::LogSeverityAtLeast::kInfo);

  // Setup custom log directory
  string log_dir = custom_log_dir.empty() ?
    absl::GetFlag(FLAGS_log_dir) : custom_log_dir;

  // Create log directory if it doesn't exist
  try {
    if (!fs::exists(log_dir)) {
      fs::create_directories(log_dir);
    }
  } catch (const fs::filesystem_error& e) {
    LOG(ERROR) << "Failed to create log directory: " << log_dir 
               << ", error: " << e.what();
  }

  // Register custom log sink if enabled
  if (absl::GetFlag(FLAGS_enable_custom_log_sink)) {
    custom_sink_ = make_unique<CustomLogSink>(log_dir, app_name);
    absl::AddLogSink(custom_sink_.get());
  }

  initialized_ = true;
  LOG(INFO) << "Logger initialized successfully. Log directory: " << log_dir;
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

  // Install custom signal handlers manually
  struct sigaction sa;
  sa.sa_flags = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sa.sa_sigaction = [](int sig, siginfo_t* info, void* ctx) {
    Logger::WriteCrashTrace(sig);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
  };
  
  // Track installed signals
  installed_signals_ = {
    SIGSEGV,
    SIGABRT,
    SIGFPE,
    SIGILL,
    SIGBUS,
    SIGTERM,
    SIGINT,
    SIGQUIT,
    SIGSYS,
    SIGPIPE
  };
  
  for (int sig : installed_signals_) {
    sigaction(sig, &sa, nullptr);
  }

  signal_handler_initialized_ = true;

  LOG(INFO) << "Signal handlers enabled with backward-cpp";
}

//------------------------------------------------------------------------------

void Logger::WriteCrashTrace(int signal) {
  if (!custom_sink_ || !custom_sink_->log_file_.is_open()) {
    return;
  } 
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

CustomLogSink::CustomLogSink(const string& log_dir, const string& app_name) :
  log_dir_(log_dir), app_name_(app_name) {
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

void CustomLogSink::Send(const absl::LogEntry& entry) {
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
  log_file_ << oss.str();
  log_file_.flush();
}

//------------------------------------------------------------------------------

void CustomLogSink::OpenLogFile() {
  string filename = GetLogFileName();
  string filepath = log_dir_ + "/" + filename;

  log_file_.open(filepath, ios::out | ios::app);

  if (!log_file_.is_open()) {
    cerr << "Failed to open log file: " << filepath << endl;
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

}} // namespace utils::logging
