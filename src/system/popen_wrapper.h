#ifndef UTILS_SYSTEM_POPEN_WRAPPER_H
#define UTILS_SYSTEM_POPEN_WRAPPER_H

#include <memory> // shared_ptr
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include "basic/basic.h"

namespace utils {
namespace system {

// PopenWrapper provides a production-grade wrapper around popen()/pclose() and
// raw POSIX pipe/fork primitives for launching child processes, capturing
// stdout/stderr independently, writing to stdin, enforcing timeouts, and
// retrieving exit status.
//
// Design goals:
//  - Zero-copy reads via user-supplied callbacks.
//  - Non-blocking I/O with epoll on Linux.
//  - Full stdin support (write, then close).
//  - Independent stderr capture (requires fork+exec path, not popen).
//  - Configurable timeouts with SIGKILL escalation.
//  - Thread-safe: a single instance must not be used from multiple threads
//    concurrently, but separate instances are fully independent.
//
// Usage example:
//   PopenWrapper::Options opts;
//   opts.command = {"ls", "-la", "/tmp"};
//   opts.capture_stderr = true;
//   opts.timeout = std::chrono::seconds(5);
//
//   PopenWrapper proc(opts);
//   auto result = proc.Run();
//   if (result.ok()) {
//     std::cout << result->stdout_data;
//   }

class PopenWrapper {
public:
  typedef std::shared_ptr<PopenWrapper> Ptr;
  typedef std::shared_ptr<const PopenWrapper> PtrConst;

  // ---------------------------------------------------------------------------
  // Public types
  // ---------------------------------------------------------------------------

  // Mode controls which streams the wrapper opens.
  enum class Mode {
    kReadOnly,  // Read child's stdout only (like popen with "r").
    kWriteOnly, // Write to child's stdin only (like popen with "w").
    kReadWrite, // Bidirectional: write stdin, read stdout (and optionally
                // stderr).
  };

  // StreamCallback is invoked incrementally as data arrives. Return false to
  // abort reading early (the child will receive SIGTERM then SIGKILL).
  using StreamCallback = std::function<bool(std::string_view chunk)>;

  // Options fully describes how the child process is configured.
  struct Options {
    // Command and arguments. argv[0] is used as the executable path unless
    // 'executable_path' is set. If 'shell' is true the entire vector is
    // joined and passed to "/bin/sh -c".
    std::vector<std::string> command;

    // If non-empty, override the executable path (argv[0] still names the
    // process).
    std::string executable_path;

    // Run the command through the shell.
    bool shell = false;

    // Open mode (read/write/both).
    Mode mode = Mode::kReadOnly;

    // Capture stderr into Result::stderr_data (requires fork+exec).
    // When false, stderr inherits the parent's stderr fd.
    bool capture_stderr = false;

    // Merge stderr into stdout stream.
    bool merge_stderr_into_stdout = false;

    // Working directory for the child. Empty means inherit.
    std::string working_directory;

    // Additional environment variables as "KEY=VALUE".  These are appended to
    // (or replace, depending on 'replace_env') the inherited environment.
    std::vector<std::string> extra_env;

    // If true, the child inherits NO environment; 'extra_env' becomes the
    // entire environment.
    bool replace_env = false;

    // Maximum wall-clock time to wait for the child to finish. Zero means no
    // timeout.
    std::chrono::milliseconds timeout{0};

    // Grace period after SIGTERM before SIGKILL is sent. Ignored when timeout
    // is zero.
    std::chrono::milliseconds kill_grace_period{std::chrono::seconds(3)};

    // Maximum bytes to buffer from stdout/stderr. Zero means unlimited.
    std::size_t max_output_bytes = 0;

    // Callback invoked for each chunk of stdout data. When set, data is NOT
    // accumulated in Result::stdout_data (use one or the other).
    StreamCallback stdout_callback;

    // Callback invoked for each chunk of stderr data.
    StreamCallback stderr_callback;

    // Read buffer size (bytes).
    std::size_t read_buffer_size = 65536;

    // Data to write to the child's stdin before reading. Used in kWriteOnly
    // or kReadWrite mode.
    std::string stdin_data;
  };

  // Result holds the outcome of a completed Run().
  struct Result {
    // Captured stdout bytes. Empty if 'stdout_callback' was set.
    std::string stdout_data;

    // Captured stderr bytes. Empty unless 'capture_stderr' was true and
    // 'stderr_callback' was not set.
    std::string stderr_data;

    // Raw exit status as returned by waitpid().
    int raw_exit_status = -1;

    // True if the child exited normally (WIFEXITED).
    bool exited_normally = false;

    // Exit code (only meaningful when 'exited_normally' is true).
    int exit_code = -1;

    // True if the child was killed by a signal.
    bool killed_by_signal = false;

    // Signal number that killed the child (only meaningful when
    // 'killed_by_signal' is true).
    int signal_number = -1;

    // True if the child was killed due to a timeout.
    bool timed_out = false;

    // True if the output was truncated because 'max_output_bytes' was reached.
    bool output_truncated = false;

    // Wall-clock time from process start to exit.
    std::chrono::milliseconds elapsed_ms{0};

    // Convenience: returns true for a clean exit with code 0.
    bool Success() const { return exited_normally && exit_code == 0; }
  };

  // ---------------------------------------------------------------------------
  // Construction / destruction
  // ---------------------------------------------------------------------------

  explicit PopenWrapper(Options options);

  // Not copyable.
  DISALLOW_COPY_AND_ASSIGN(PopenWrapper);

  // Movable.
  PopenWrapper(PopenWrapper &&) noexcept;
  PopenWrapper &operator=(PopenWrapper &&) noexcept;

  ~PopenWrapper();

  // ---------------------------------------------------------------------------
  // Execution API
  // ---------------------------------------------------------------------------

  // Run() launches the child process, performs I/O, waits for completion, and
  // returns a Result. Throws std::system_error on fork/exec/pipe failures.
  // May be called only once per instance.
  Result Run();

  // Async variant: Start() forks the child; the caller then drives I/O via
  // WriteStdin() / ReadChunk(), and finally calls Wait().
  void Start();

  // Write bytes to the child's stdin. Only valid after Start() and before
  // CloseStdin(). Returns the number of bytes actually written, or -1 on
  // error (errno is set).
  ssize_t WriteStdin(std::string_view data);

  // Close the child's stdin pipe (signals EOF to the child).
  void CloseStdin();

  // Read one chunk from stdout. Returns empty optional when EOF or error.
  std::optional<std::string> ReadStdoutChunk();

  // Read one chunk from stderr. Returns empty optional when EOF or error.
  std::optional<std::string> ReadStderrChunk();

  // Wait for the child to finish and collect exit status. Blocks until the
  // child exits or the timeout expires. Returns the completed Result.
  Result Wait();

  // Send SIGTERM to the child (no-op if not running).
  void Terminate();

  // Send SIGKILL to the child (no-op if not running).
  void Kill();

  // True if a child process is currently running.
  bool IsRunning() const;

  // PID of the child, or -1 if not started.
  pid_t GetPid() const;

  // ---------------------------------------------------------------------------
  // Static helpers
  // ---------------------------------------------------------------------------

  // Shell-escape a single token for safe inclusion in a shell command string.
  static std::string ShellEscape(std::string_view token);

  // Join a vector of tokens into a shell-escaped command string.
  static std::string BuildShellCommand(const std::vector<std::string> &argv);

private:
  // ---------------------------------------------------------------------------
  // Internal helpers
  // ---------------------------------------------------------------------------

  // Fork + exec the child process, setting up all pipe fds.
  void ForkExec();

  // Perform the full I/O loop (epoll on Linux, select elsewhere) after Start().
  void DriveIO(Result &result);

  // Drain a single fd into a string buffer, honouring max_output_bytes.
  // Returns false when the fd reaches EOF.
  bool DrainFd(int fd, std::string &buffer, const StreamCallback &cb,
               bool &truncated);

  // Close and reset a file descriptor.
  static void CloseFd(int &fd);

  // Set O_NONBLOCK on a file descriptor.
  static void SetNonBlocking(int fd);

  // Build the argv for exec.
  std::vector<char *> BuildArgv() const;

  // Build the envp for exec, or nullptr to inherit.
  std::vector<std::string> BuildEnvp() const;

  // Wait for the child with optional timeout. Populates raw_exit_status.
  void WaitpidWithTimeout(Result &result);

  // ---------------------------------------------------------------------------
  // Data members
  // ---------------------------------------------------------------------------

  Options opts_;

  pid_t child_pid_ = -1;

  // Pipe file descriptors: [0]=read end, [1]=write end.
  int stdin_pipe_[2] = {-1, -1};
  int stdout_pipe_[2] = {-1, -1};
  int stderr_pipe_[2] = {-1, -1};

  // Parent-side fds (after fork, child ends are closed in the parent).
  int parent_stdin_write_ = -1;
  int parent_stdout_read_ = -1;
  int parent_stderr_read_ = -1;

  // Self-pipe for timeout signalling from a timer thread.
  int timer_pipe_[2] = {-1, -1};

  std::atomic<bool> timed_out_{false};
  std::thread timer_thread_;

  std::chrono::steady_clock::time_point start_time_;

  bool started_ = false;
  bool waited_ = false;
};

} // namespace system
} // namespace utils

#endif // UTILS_SYSTEM_POPEN_WRAPPER_H
