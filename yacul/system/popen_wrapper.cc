// _GNU_SOURCE must be defined before any system header to expose execvpe(3).
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "yacul/system/popen_wrapper.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno> // errno
#include <climits>
#include <cstring>
#include <fcntl.h>  // fcntl, F_GETFL, F_SETFL, O_NONBLOCK
#include <poll.h>   // poll, struct pollfd
#include <signal.h> // kill, SIGTERM, SIGKILL
#include <stdexcept>
#include <sys/types.h> // pid_t
#include <sys/wait.h>  // waitpid, WIFEXITED, WEXITSTATUS, WIFSIGNALED, WTERMSIG
#include <system_error>
#include <unistd.h> // fork, pipe, dup2, execvp, read etc.
#include <utility>  // move, forward

using namespace std;

// Suppress warn_unused_result on best-effort signal writes.
#define IGNORE_RESULT(expr)                                                    \
  do {                                                                         \
    if ((expr)) {                                                              \
    }                                                                          \
  } while (0)

// ---------------------------------------------------------------------------

namespace {

// POSIX pipe(2) wrapper that throws on failure.
void CreatePipe(int fds[2]) {
  if (::pipe(fds) != 0) {
    throw system_error(errno, generic_category(), "pipe");
  }
}

// Thin RAII guard for a raw fd that closes on scope exit.
class FdGuard {
public:
  explicit FdGuard(int fd) : fd_(fd) {}
  ~FdGuard() {
    if (fd_ >= 0)
      ::close(fd_);
  }
  void Release() { fd_ = -1; }

private:
  int fd_;
};

// Read up to 'n' bytes from 'fd'. Returns number of bytes read, 0 on EOF, -1 on
// error (EAGAIN/EWOULDBLOCK treated as 0 bytes, not error).
ssize_t ReadAtMost(int fd, char *buf, size_t n) {
  ssize_t r;
  do {
    r = ::read(fd, buf, n);
  } while (r == -1 && errno == EINTR); // Retry if interrupted by signal.

  // EAGAIN/EWOULDBLOCK are not treated as errors by DrainFd, so convert them to
  // 0 bytes read.
  if (r == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    return 0;
  }
  return r;
}

// Set FD_CLOEXEC so the fd is closed in the child after exec.
void SetCloseOnExec(int fd) {
  int flags = ::fcntl(fd, F_GETFD);
  if (flags == -1)
    return;
  ::fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

} // namespace

// ---------------------------------------------------------------------------

namespace utils {
namespace system {

// ---------------------------------------------------------------------------

PopenWrapper::PopenWrapper(Options options) : opts_(move(options)) {
  stdin_pipe_[0] = stdin_pipe_[1] = -1;
  stdout_pipe_[0] = stdout_pipe_[1] = -1;
  stderr_pipe_[0] = stderr_pipe_[1] = -1;
  timer_pipe_[0] = timer_pipe_[1] = -1;
}

// ---------------------------------------------------------------------------

PopenWrapper::PopenWrapper(PopenWrapper &&other) noexcept
  : opts_(move(other.opts_)), child_pid_(other.child_pid_),
    parent_stdin_write_(other.parent_stdin_write_),
    parent_stdout_read_(other.parent_stdout_read_),
    parent_stderr_read_(other.parent_stderr_read_),
    timed_out_(other.timed_out_.load()),
    timer_thread_(move(other.timer_thread_)), start_time_(other.start_time_),
    started_(other.started_), waited_(other.waited_) {
  for (int i = 0; i < 2; ++i) {
    stdin_pipe_[i] = other.stdin_pipe_[i];
    stdout_pipe_[i] = other.stdout_pipe_[i];
    stderr_pipe_[i] = other.stderr_pipe_[i];
    timer_pipe_[i] = other.timer_pipe_[i];
    other.stdin_pipe_[i] = other.stdout_pipe_[i] = -1;
    other.stderr_pipe_[i] = other.timer_pipe_[i] = -1;
  }
  other.child_pid_ = -1;
  other.parent_stdin_write_ = -1;
  other.parent_stdout_read_ = -1;
  other.parent_stderr_read_ = -1;
  other.started_ = false;
  other.waited_ = false;
}

// ---------------------------------------------------------------------------

PopenWrapper &PopenWrapper::operator=(PopenWrapper &&other) noexcept {
  if (this != &other) {
    this->~PopenWrapper();
    new (this) PopenWrapper(move(other));
  }
  return *this;
}

// ---------------------------------------------------------------------------

PopenWrapper::~PopenWrapper() {
  // If the child is still running, kill it.
  if (child_pid_ > 0 && !waited_) {
    ::kill(child_pid_, SIGKILL);
    int status;
    ::waitpid(child_pid_, &status, 0);
  }
  // Cancel timer thread.
  if (timer_pipe_[1] >= 0) {
    char b = 1;
    IGNORE_RESULT(::write(timer_pipe_[1], &b, 1));
  }
  if (timer_thread_.joinable())
    timer_thread_.join();

  // Close all fds.
  for (int *fd :
       {&parent_stdin_write_, &parent_stdout_read_, &parent_stderr_read_}) {
    CloseFd(*fd);
  }
  for (int i = 0; i < 2; ++i) {
    CloseFd(stdin_pipe_[i]);
    CloseFd(stdout_pipe_[i]);
    CloseFd(stderr_pipe_[i]);
    CloseFd(timer_pipe_[i]);
  }
}

// ---------------------------------------------------------------------------

string PopenWrapper::ShellEscape(string_view token) {
  // Wrap in single quotes; replace embedded ' with '\'' (end quote, literal
  // apostrophe, re-open quote).
  string result;
  result.reserve(token.size() + 2);
  result += '\'';
  for (char c : token) {
    if (c == '\'') {
      // End the current single-quoted span, emit the apostrophe inside double
      // quotes, then re-open single quoting. This avoids '\'' which is
      // rejected by strict POSIX shells (e.g. dash used as /bin/sh on
      // Debian/Ubuntu).
      result += "'\"'\"'";
    } else {
      result += c;
    }
  }
  result += '\'';
  return result;
}

// ---------------------------------------------------------------------------

string PopenWrapper::BuildShellCommand(const vector<string> &argv) {
  string cmd;
  for (size_t i = 0; i < argv.size(); ++i) {
    if (i > 0)
      cmd += ' ';
    cmd += ShellEscape(argv[i]);
  }
  return cmd;
}

// ---------------------------------------------------------------------------

void PopenWrapper::CloseFd(int &fd) {
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
  }
}

// ---------------------------------------------------------------------------

void PopenWrapper::SetNonBlocking(int fd) {
  int flags = ::fcntl(fd, F_GETFL);
  if (flags == -1)
    return;
  ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ---------------------------------------------------------------------------

vector<char *> PopenWrapper::BuildArgv() const {
  // Allocate temporary strings for the shell invocation path.
  static thread_local vector<string> shell_args_storage;
  shell_args_storage.clear();

  const vector<string> *source = &opts_.command;

  if (opts_.shell) {
    // Tokens are joined with a single space and handed verbatim to "/bin/sh
    // -c"; ShellEscape must NOT be applied — it would quote metacharacters ($,
    // |, *) that the user deliberately wants the shell to interpret.
    std::string script;
    for (std::size_t i = 0; i < opts_.command.size(); ++i) {
      if (i > 0)
        script += ' ';
      script += opts_.command[i];
    }
    shell_args_storage = {"/bin/sh", "-c", std::move(script)};
    source = &shell_args_storage;
  }

  vector<char *> argv;
  argv.reserve(source->size() + 1);
  for (const auto &s : *source) {
    argv.push_back(const_cast<char *>(s.c_str()));
  }
  argv.push_back(nullptr);
  return argv;
}

// ---------------------------------------------------------------------------

vector<string> PopenWrapper::BuildEnvp() const {
  if (opts_.extra_env.empty() && !opts_.replace_env)
    return {};

  vector<string> env;
  if (!opts_.replace_env) {
    // Start with inherited environment.
    for (char **e = environ; e && *e; ++e) {
      env.emplace_back(*e);
    }
  }
  for (const auto &kv : opts_.extra_env) {
    // Allow "KEY=VALUE" to override an existing key.
    string_view sv(kv);
    auto eq = sv.find('=');
    if (eq != string_view::npos) {
      string_view key = sv.substr(0, eq + 1); // "KEY="
      auto it = find_if(env.begin(), env.end(), [&](const string &s) {
        return string_view(s).substr(0, key.size()) == key;
      });
      if (it != env.end()) {
        *it = kv;
        continue;
      }
    }
    env.push_back(kv);
  }
  return env;
}

// ---------------------------------------------------------------------------

void PopenWrapper::ForkExec() {
  const bool need_stdin =
    (opts_.mode == Mode::kWriteOnly || opts_.mode == Mode::kReadWrite);
  const bool need_stdout =
    (opts_.mode == Mode::kReadOnly || opts_.mode == Mode::kReadWrite);
  const bool need_stderr =
    opts_.capture_stderr && !opts_.merge_stderr_into_stdout;

  // Create pipes.
  if (need_stdin)
    CreatePipe(stdin_pipe_);
  if (need_stdout)
    CreatePipe(stdout_pipe_);
  if (need_stderr)
    CreatePipe(stderr_pipe_);

  // Create self-pipe for timer signalling.
  if (opts_.timeout.count() > 0) {
    CreatePipe(timer_pipe_);
    SetCloseOnExec(timer_pipe_[0]);
    SetCloseOnExec(timer_pipe_[1]);
  }

  // Collect envp before fork (heap allocations are unsafe after fork).
  std::vector<std::string> envp_strings = BuildEnvp();
  std::vector<char *> envp_ptrs;
  // Always build envp_ptrs when a custom environment is requested, even if
  // it is empty (replace_env=true + no extra_env → child gets no env vars).
  const bool use_custom_env = opts_.replace_env || !opts_.extra_env.empty();
  if (use_custom_env) {
    envp_ptrs.reserve(envp_strings.size() + 1);
    for (auto &s : envp_strings)
      envp_ptrs.push_back(const_cast<char *>(s.c_str()));
    envp_ptrs.push_back(nullptr);
  }

  std::vector<char *> argv = BuildArgv();
  const char *exe =
    opts_.executable_path.empty() ? argv[0] : opts_.executable_path.c_str();

  start_time_ = std::chrono::steady_clock::now();
  child_pid_ = ::fork();

  if (child_pid_ < 0) {
    // Cleanup pipes before throwing.
    for (int i = 0; i < 2; ++i) {
      CloseFd(stdin_pipe_[i]);
      CloseFd(stdout_pipe_[i]);
      CloseFd(stderr_pipe_[i]);
      CloseFd(timer_pipe_[i]);
    }
    throw std::system_error(errno, std::generic_category(), "fork");
  }

  if (child_pid_ == 0) {
    // ---- Child process ----

    // Redirect stdin.
    if (need_stdin) {
      ::dup2(stdin_pipe_[0], STDIN_FILENO);
      ::close(stdin_pipe_[0]);
      ::close(stdin_pipe_[1]);
    }

    // Redirect stdout.
    if (need_stdout) {
      ::dup2(stdout_pipe_[1], STDOUT_FILENO);
      ::close(stdout_pipe_[0]);
      ::close(stdout_pipe_[1]);
      if (opts_.merge_stderr_into_stdout) {
        ::dup2(STDOUT_FILENO, STDERR_FILENO);
      }
    }

    // Redirect stderr.
    if (need_stderr) {
      ::dup2(stderr_pipe_[1], STDERR_FILENO);
      ::close(stderr_pipe_[0]);
      ::close(stderr_pipe_[1]);
    }

    // Change working directory.
    if (!opts_.working_directory.empty()) {
      if (::chdir(opts_.working_directory.c_str()) != 0) {
        ::_exit(127);
      }
    }

    // Exec.
    if (use_custom_env) {
      ::execvpe(exe, argv.data(), envp_ptrs.data());
    } else {
      ::execvp(exe, argv.data());
    }

    // If exec fails, exit with a distinguishable code.
    ::_exit(127);
  }

  // ---- Parent process ----

  // Close child-side ends.
  if (need_stdin) {
    CloseFd(stdin_pipe_[0]);
    parent_stdin_write_ = stdin_pipe_[1];
    stdin_pipe_[1] = -1;
  }
  if (need_stdout) {
    CloseFd(stdout_pipe_[1]);
    parent_stdout_read_ = stdout_pipe_[0];
    stdout_pipe_[0] = -1;
    SetNonBlocking(parent_stdout_read_);
  }
  if (need_stderr) {
    CloseFd(stderr_pipe_[1]);
    parent_stderr_read_ = stderr_pipe_[0];
    stderr_pipe_[0] = -1;
    SetNonBlocking(parent_stderr_read_);
  }
  // timer_pipe_[0] (read) and timer_pipe_[1] (write) are both kept open in the
  // parent.  FD_CLOEXEC ensures neither leaks into the child via exec.
  // DriveIO polls timer_pipe_[0] for readability; the timer thread (or Wait()
  // cancellation) writes to timer_pipe_[1].
}

// ---------------------------------------------------------------------------

// Launched in a background thread; writes one byte to timer_pipe_[1] when the
// timeout expires, or exits early if a cancellation byte is already present.
static void RunTimer(int cancel_read_fd, int signal_write_fd,
                     chrono::milliseconds timeout, atomic<bool> &timed_out_flag,
                     pid_t child_pid, chrono::milliseconds grace) {
  // Wait for 'timeout' or until cancel_read_fd becomes readable.
  struct pollfd pfd{};
  pfd.fd = cancel_read_fd;
  pfd.events = POLLIN;

  int ms = static_cast<int>(
    min(timeout.count(), static_cast<decltype(timeout.count())>(INT_MAX)));
  int r = ::poll(&pfd, 1, ms);

  if (r == 0) {
    // Timeout expired; send SIGTERM then wait for grace, then SIGKILL.
    timed_out_flag.store(true, memory_order_relaxed);
    ::kill(child_pid, SIGTERM);

    pfd.fd = cancel_read_fd;
    pfd.events = POLLIN;
    int grace_ms = static_cast<int>(
      min(grace.count(), static_cast<decltype(grace.count())>(INT_MAX)));
    int r2 = ::poll(&pfd, 1, grace_ms);
    if (r2 == 0) {
      ::kill(child_pid, SIGKILL);
    }

    // Signal the main thread that we have timed out.
    char byte = 1;
    IGNORE_RESULT(::write(signal_write_fd, &byte, 1));
  }
  // If r > 0, the cancel fd became readable — we were cancelled.
}

// ---------------------------------------------------------------------------

void PopenWrapper::Start() {
  if (started_) {
    throw logic_error("PopenWrapper::Start() called more than once");
  }
  started_ = true;

  ForkExec();

  // Launch timeout timer thread if needed.
  if (opts_.timeout.count() > 0 && timer_pipe_[0] >= 0 && timer_pipe_[1] >= 0) {
    // Ensure parent_stdout_read_ / parent_stderr_read_ are non-blocking
    // already (done in ForkExec).
    timer_thread_ =
      thread(RunTimer, timer_pipe_[0], timer_pipe_[1], opts_.timeout,
             ref(timed_out_), child_pid_, opts_.kill_grace_period);
  }
}

// ---------------------------------------------------------------------------

PopenWrapper::Result PopenWrapper::Wait() {
  if (!started_) {
    throw logic_error("PopenWrapper::Wait() called before Start()");
  }
  if (waited_) {
    throw logic_error("PopenWrapper::Wait() called more than once");
  }
  waited_ = true;

  Result result;
  DriveIO(result);
  WaitpidWithTimeout(result);

  auto end_time = chrono::steady_clock::now();
  result.elapsed_ms =
    chrono::duration_cast<chrono::milliseconds>(end_time - start_time_);
  result.timed_out = timed_out_.load(memory_order_relaxed);

  // Cancel and join timer thread.
  if (timer_thread_.joinable()) {
    // Write a cancel byte to unblock the timer if it's still waiting.
    if (timer_pipe_[1] >= 0) {
      char b = 0;
      IGNORE_RESULT(::write(timer_pipe_[1], &b, 1));
      CloseFd(timer_pipe_[1]);
    }
    timer_thread_.join();
  }
  CloseFd(timer_pipe_[0]);

  return result;
}

// ---------------------------------------------------------------------------

PopenWrapper::Result PopenWrapper::Run() {
  Start();

  // Write stdin synchronously before entering the I/O loop if in write-only
  // mode (no output to interleave).
  if (opts_.mode == Mode::kWriteOnly || opts_.mode == Mode::kReadWrite) {
    if (opts_.stdin_data.empty()) {
      // Nothing to write — close stdin immediately so the child sees EOF.
      // Without this, commands like `cat` in kReadWrite mode with empty
      // stdin_data would block forever waiting for input.
      CloseStdin();
    } else if (opts_.mode == Mode::kWriteOnly) {
      // Write-only: no concurrent stdout to read, so write synchronously.
      WriteStdin(opts_.stdin_data);
      CloseStdin();
    }
    // kReadWrite with non-empty stdin_data: DriveIO handles interleaved
    // writing and reading via poll() to avoid pipe-full deadlocks.
  }

  return Wait();
}

// ---------------------------------------------------------------------------

bool PopenWrapper::DrainFd(int fd, string &buffer, const StreamCallback &cb,
                           bool &truncated) {
  vector<char> read_buf(opts_.read_buffer_size);

  while (true) {
    ssize_t n = ReadAtMost(fd, read_buf.data(), read_buf.size());
    if (n < 0) {
      // Unrecoverable error.
      return false;
    }
    if (n == 0) {
      // EAGAIN or EOF — will be determined by poll status.
      return true;
    }

    string_view chunk(read_buf.data(), static_cast<size_t>(n));

    if (cb) {
      if (!cb(chunk)) {
        // Callback requested abort.
        return false;
      }
    } else {
      // Enforce max_output_bytes.
      if (opts_.max_output_bytes > 0) {
        size_t space = opts_.max_output_bytes - buffer.size();
        if (space == 0) {
          truncated = true;
          return false;
        }
        if (chunk.size() > space) {
          chunk = chunk.substr(0, space);
          truncated = true;
        }
      }
      buffer.append(chunk.data(), chunk.size());
      if (truncated)
        return false;
    }

    if (static_cast<size_t>(n) < read_buf.size()) {
      // Likely EAGAIN next time; yield back to poll.
      break;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------

void PopenWrapper::DriveIO(Result &result) {
  // In ReadWrite mode, we need to write stdin concurrently with reading.
  // We use poll() to multiplex all fds.

  const bool has_stdout = parent_stdout_read_ >= 0;
  const bool has_stderr = parent_stderr_read_ >= 0;
  const bool has_timer = timer_pipe_[0] >= 0;

  std::size_t stdin_offset = 0;

  // We'll poll at most 4 fds: stdout, stderr, stdin (write), timer.
  std::vector<struct pollfd> pfds;
  pfds.reserve(4);

  auto rebuild_pfds = [&]() {
    pfds.clear();
    if (has_stdout && parent_stdout_read_ >= 0) {
      pfds.push_back({parent_stdout_read_, POLLIN, 0});
    }
    if (has_stderr && parent_stderr_read_ >= 0) {
      pfds.push_back({parent_stderr_read_, POLLIN, 0});
    }
    if (parent_stdin_write_ >= 0 && stdin_offset < opts_.stdin_data.size()) {
      pfds.push_back({parent_stdin_write_, POLLOUT, 0});
    }
    if (has_timer && timer_pipe_[0] >= 0) {
      pfds.push_back({timer_pipe_[0], POLLIN, 0});
    }
  };

  while (true) {
    rebuild_pfds();
    if (pfds.empty())
      break;

    int ready = ::poll(pfds.data(), static_cast<nfds_t>(pfds.size()), -1);
    if (ready < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    bool abort = false;

    for (const auto &pfd : pfds) {
      if (pfd.revents == 0)
        continue;

      if (pfd.fd == parent_stdout_read_) {
        bool ok = DrainFd(pfd.fd, result.stdout_data, opts_.stdout_callback,
                          result.output_truncated);
        if (!ok || (pfd.revents & (POLLHUP | POLLERR))) {
          CloseFd(parent_stdout_read_);
          if (!ok) {
            abort = true;
          }
        }
      } else if (pfd.fd == parent_stderr_read_) {
        bool ok = DrainFd(pfd.fd, result.stderr_data, opts_.stderr_callback,
                          result.output_truncated);
        if (!ok || (pfd.revents & (POLLHUP | POLLERR))) {
          CloseFd(parent_stderr_read_);
          if (!ok) {
            abort = true;
          }
        }
      } else if (pfd.fd == parent_stdin_write_) {
        if (pfd.revents & POLLOUT) {
          std::string_view remaining(opts_.stdin_data.data() + stdin_offset,
                                     opts_.stdin_data.size() - stdin_offset);
          ssize_t n;
          do {
            n =
              ::write(parent_stdin_write_, remaining.data(), remaining.size());
          } while (n == -1 && errno == EINTR);

          if (n > 0) {
            stdin_offset += static_cast<std::size_t>(n);
          }
          if (stdin_offset >= opts_.stdin_data.size() || n <= 0) {
            CloseStdin();
          }
        } else if (pfd.revents & (POLLHUP | POLLERR)) {
          CloseStdin();
        }
      } else if (pfd.fd == timer_pipe_[0]) {
        // Timeout fired — child has already been signalled by timer thread.
        abort = true;
      }
    }

    if (abort) {
      // If the abort came from a callback rejection or output truncation (not
      // a timer-driven abort — the timer thread handles signalling itself),
      // we must terminate the child so WaitpidWithTimeout doesn't deadlock.
      if (!timed_out_.load(std::memory_order_relaxed) && child_pid_ > 0) {
        ::kill(child_pid_, SIGTERM);
      }
      break;
    }

    // If both output fds are closed and stdin is done, we are finished.
    if (parent_stdout_read_ < 0 && parent_stderr_read_ < 0 &&
        parent_stdin_write_ < 0) {
      break;
    }
  }

  // Drain any remaining data after poll loop (edge case for POLLHUP + data).
  if (parent_stdout_read_ >= 0) {
    DrainFd(parent_stdout_read_, result.stdout_data, opts_.stdout_callback,
            result.output_truncated);
    CloseFd(parent_stdout_read_);
  }
  if (parent_stderr_read_ >= 0) {
    DrainFd(parent_stderr_read_, result.stderr_data, opts_.stderr_callback,
            result.output_truncated);
    CloseFd(parent_stderr_read_);
  }
  CloseStdin();
}

// ---------------------------------------------------------------------------

void PopenWrapper::WaitpidWithTimeout(Result &result) {
  if (child_pid_ <= 0)
    return;

  int status = 0;
  pid_t ret = -1;

  // Spin with WNOHANG so we don't block if the child is still running after a
  // timeout.  The timer thread has already sent SIGKILL in that case.
  constexpr chrono::milliseconds kPollInterval{10};
  const int max_iterations =
    opts_.timeout.count() > 0
      ? static_cast<int>(
          (opts_.timeout + opts_.kill_grace_period + chrono::seconds(1)) /
          kPollInterval)
      : numeric_limits<int>::max();

  for (int i = 0; i < max_iterations; ++i) {
    do {
      ret = ::waitpid(child_pid_, &status, WNOHANG);
    } while (ret == -1 && errno == EINTR);

    if (ret == child_pid_)
      break;
    if (ret == -1)
      break; // Error.

    // Child not yet exited.
    this_thread::sleep_for(kPollInterval);
  }

  if (ret != child_pid_) {
    // Last-ditch blocking wait (e.g., after SIGKILL).
    do {
      ret = ::waitpid(child_pid_, &status, 0);
    } while (ret == -1 && errno == EINTR);
  }

  child_pid_ = -1;
  result.raw_exit_status = status;

  if (WIFEXITED(status)) {
    result.exited_normally = true;
    result.exit_code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    result.killed_by_signal = true;
    result.signal_number = WTERMSIG(status);
  }
}

// ---------------------------------------------------------------------------

ssize_t PopenWrapper::WriteStdin(string_view data) {
  if (parent_stdin_write_ < 0)
    return -1;
  ssize_t n;
  do {
    n = ::write(parent_stdin_write_, data.data(), data.size());
  } while (n == -1 && errno == EINTR);
  return n;
}

// ---------------------------------------------------------------------------

void PopenWrapper::CloseStdin() { CloseFd(parent_stdin_write_); }

// ---------------------------------------------------------------------------

static std::optional<std::string> ReadChunkBlocking(int fd,
                                                    std::size_t buf_size) {
  if (fd < 0)
    return std::nullopt;
  // poll() blocks until data arrives or the pipe closes, making the
  // ReadStdoutChunk/ReadStderrChunk API usable in a simple while loop.
  struct pollfd pfd{};
  pfd.fd = fd;
  pfd.events = POLLIN;
  int r;
  do {
    r = ::poll(&pfd, 1, -1);
  } while (r == -1 && errno == EINTR);
  if (r <= 0)
    return std::nullopt;
  if (pfd.revents & (POLLERR | POLLNVAL))
    return std::nullopt;
  std::vector<char> buf(buf_size);
  ssize_t n = ReadAtMost(fd, buf.data(), buf.size());
  if (n <= 0)
    return std::nullopt;
  return std::string(buf.data(), static_cast<std::size_t>(n));
}

// ---------------------------------------------------------------------------

std::optional<std::string> PopenWrapper::ReadStdoutChunk() {
  return ReadChunkBlocking(parent_stdout_read_, opts_.read_buffer_size);
}

// ---------------------------------------------------------------------------

std::optional<std::string> PopenWrapper::ReadStderrChunk() {
  return ReadChunkBlocking(parent_stderr_read_, opts_.read_buffer_size);
}

// ---------------------------------------------------------------------------

void PopenWrapper::Terminate() {
  if (child_pid_ > 0)
    ::kill(child_pid_, SIGTERM);
}

// ---------------------------------------------------------------------------

void PopenWrapper::Kill() {
  if (child_pid_ > 0)
    ::kill(child_pid_, SIGKILL);
}

// ---------------------------------------------------------------------------

bool PopenWrapper::IsRunning() const {
  if (child_pid_ <= 0)
    return false;
  // kill(pid, 0) checks liveness without reaping the child. Returns 0 if the
  // process exists and we can signal it, -1 with ESRCH if not.
  return ::kill(child_pid_, 0) == 0;
}

// ---------------------------------------------------------------------------

pid_t PopenWrapper::GetPid() const { return child_pid_; }

// ---------------------------------------------------------------------------

} // namespace system
} // namespace utils
