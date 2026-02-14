#include "logger.h"
#include "absl/flags/internal/flag.h"

using namespace std;
namespace fs = filesystem;

// Define flags for log directory and prefix
ABSL_FLAG(std::string, log_dir, "/home/akayhomie/code/utils/logs",
          "Directory to dump logs");
ABSL_FLAG(bool, enable_custom_log_sink, true,
          "Enable custom log sink for directory output");
ABSL_FLAG(bool, enable_backtrace_cpp_signal_handlers, true,
          "Enable backward-cpp signal handlers for crash reporting");

namespace utils { namespace logging {

// Initialize static members
bool Logger::initialized_ = false;
CustomLogSink::UPtr Logger::custom_sink_ = nullptr;
unique_ptr<backward::SignalHandling> Logger::signal_handler_ = nullptr;

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

  if (!absl::GetFlag(FLAGS_enable_backtrace_cpp_signal_handlers)) {
    // Initialize Abseil symbolizer first
    absl::InitializeSymbolizer(app_name.c_str());
  }

  initialized_ = true;
  LOG(INFO) << "Logger initialized successfully. Log directory: " << log_dir;
}

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
  // Disable signal handlers
  if (signal_handler_) {
    signal_handler_.reset();
  }
  initialized_ = false;
}

void Logger::EnableSignalHandlers() {
  if (signal_handler_) {
    LOG(WARNING) << "Signal handlers already enabled";
    return;
  }
  if (!absl::GetFlag(FLAGS_enable_backtrace_cpp_signal_handlers)) {
    LOG(INFO) << "Backward-cpp signal handlers disabled by flag";
    // Setup signal handling
    absl::FailureSignalHandlerOptions options;
    options.symbolize_stacktrace = true;
    options.use_alternate_stack = true;
    absl::InstallFailureSignalHandler(options);
    return;
  }

  #if defined(BACKWARD_HAS_LIBUNWIND)
    LOG(INFO) << "  Using: libunwind";
#elif defined(BACKWARD_HAS_BACKTRACE)
    LOG(INFO) << "  Using: backtrace";
#elif defined(BACKWARD_HAS_UNWIND)
    LOG(INFO) << "  Using: unwind";
#else
    LOG(INFO) << "  Using: UNKNOWN";
#endif

    LOG(INFO) << "Backward-cpp Symbol Resolution:";
    
#if defined(BACKWARD_HAS_BFD)
    LOG(INFO) << "  BFD: YES (best)";
#else
    LOG(INFO) << "  BFD: NO";
#endif

#if defined(BACKWARD_HAS_DW)
    LOG(INFO) << "  DW: YES (good)";
#else
    LOG(INFO) << "  DW: NO";
#endif

#if defined(BACKWARD_HAS_DWARF)
    LOG(INFO) << "  DWARF: YES (good)";
#else
    LOG(INFO) << "  DWARF: NO";
#endif

#if defined(BACKWARD_HAS_BACKTRACE_SYMBOL)
    LOG(INFO) << "  BACKTRACE_SYMBOL: YES (basic)";
#else
    LOG(INFO) << "  BACKTRACE_SYMBOL: NO";
#endif

  // Create backward signal handler with all common signals
  signal_handler_ = make_unique<backward::SignalHandling>(
    initializer_list<int>{
      SIGSEGV, // Segmentation fault
      SIGABRT, // Abort signal
      SIGFPE,  // Floating point exception
      SIGILL,  // Illegal instruction
      SIGBUS,  // Bus error
      SIGTERM, // Termination request
      SIGINT,  // Interrupt from keyboard (Ctrl+C)
      SIGQUIT, // Quit from keyboard
      SIGSYS,  // Bad system call
      SIGPIPE  // Broken pipe
    }
  );
  LOG(INFO) << "Signal handlers enabled with backward-cpp";
}

void Logger::DisableSignalHandlers() {
  if (signal_handler_) {
    signal_handler_.reset();
    LOG(INFO) << "Signal handlers disabled";
  }
}

CustomLogSink::CustomLogSink(const string& log_dir, const string& app_name) :
  log_dir_(log_dir), app_name_(app_name) {
  OpenLogFile();
}

CustomLogSink::~CustomLogSink() {
  lock_guard<mutex> lock(file_mutex_);
  if (log_file_.is_open()) {
    log_file_.close();
  }
}

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

void CustomLogSink::OpenLogFile() {
  string filename = GetLogFileName();
  string filepath = log_dir_ + "/" + filename;

  log_file_.open(filepath, ios::out | ios::app);

  if (!log_file_.is_open()) {
    cerr << "Failed to open log file: " << filepath << endl;
  }
}

string CustomLogSink::GetLogFileName() const {
  // Generate filename with timestamp and process ID
  auto now = chrono::system_clock::now();
  auto time_t = chrono::system_clock::to_time_t(now);
  ostringstream oss;
  oss << app_name_ << "_" << put_time(localtime(&time_t), "%Y%m%d_%H%M%S")
      << "_" << getpid() << ".log";
  return oss.str();
}

}} // namespace utils::logging
