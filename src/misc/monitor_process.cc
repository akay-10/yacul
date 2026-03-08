#include "monitor_process.h"

#include <fcntl.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std;

namespace utils {
namespace misc {

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

mutex MonitorProcess::state_mutex_;
MonitorProcess::Config MonitorProcess::config_;
atomic<MonitorProcess::State> MonitorProcess::state_{
    MonitorProcess::State::kIdle};
atomic<pid_t> MonitorProcess::child_pid_{0};
atomic<int> MonitorProcess::restart_count_{0};
atomic<int> MonitorProcess::consecutive_restart_count_{0};
chrono::steady_clock::time_point MonitorProcess::last_start_time_;
pthread_t MonitorProcess::monitor_thread_{};
bool MonitorProcess::monitor_thread_valid_{false};
atomic<bool> MonitorProcess::stop_requested_{false};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool MonitorProcess::Start(Config cfg) {
  if (cfg.argv.empty()) {
    cerr << "[MonitorProcess] Config error: argv must not be empty.\n";
    return false;
  }

  lock_guard<mutex> lock(state_mutex_);

  if (state_.load() == State::kRunning || state_.load() == State::kStopping) {
    cerr << "[MonitorProcess] Already running; call RequestStop() first.\n";
    return false;
  }

  config_ = move(cfg);
  stop_requested_ = false;
  restart_count_ = 0;
  consecutive_restart_count_ = 0;
  child_pid_ = 0;
  state_ = State::kRunning;

  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  int rc = pthread_create(&monitor_thread_, &attr,
                          &MonitorProcess::MonitorThreadEntry, nullptr);
  pthread_attr_destroy(&attr);

  if (rc != 0) {
    state_ = State::kIdle;
    cerr << "[MonitorProcess] pthread_create failed: " << strerror(rc) << "\n";
    return false;
  }

  monitor_thread_valid_ = true;
  return true;
}

// ---------------------------------------------------------------------------

void MonitorProcess::RequestStop() {
  bool already_stopping =
      (state_.load() == State::kStopping || state_.load() == State::kStopped ||
       state_.load() == State::kIdle);

  if (already_stopping)
    return;

  // Set the flag first so the monitor thread sees it even if child_pid_ is
  // still 0 (i.e. SpawnChild() has not returned yet).
  stop_requested_ = true;
  state_ = State::kStopping;

  // Best-effort signal delivery to any already-running child.  If child_pid_
  // is 0 here the monitor thread will see stop_requested_ before it calls
  // waitpid and will handle the kill itself.
  pid_t pid = child_pid_.load();
  if (pid > 0) {
    kill(pid, config_.stop_signal);
  }
  // Do NOT block here.  The actual reaping is done by the monitor thread;
  // the caller must call WaitForStop() to block until completion.
}

// ---------------------------------------------------------------------------

void MonitorProcess::WaitForStop() {
  bool valid = false;
  pthread_t tid{};
  {
    lock_guard<mutex> lock(state_mutex_);
    valid = monitor_thread_valid_;
    tid = monitor_thread_;
  }

  if (valid) {
    pthread_join(tid, nullptr);
    lock_guard<mutex> lock(state_mutex_);
    monitor_thread_valid_ = false;
  }

  // After the thread is fully joined (or was never started), the monitor is
  // back in a clean idle state and Start() can be called again.
  state_ = State::kIdle;
}

// ---------------------------------------------------------------------------

bool MonitorProcess::SendSignalToChild(int signum) {
  pid_t pid = child_pid_.load();
  if (pid <= 0)
    return false;
  return (kill(pid, signum) == 0);
}

// ---------------------------------------------------------------------------

MonitorProcess::Stats MonitorProcess::GetStats() {
  Stats s;
  s.current_child_pid = child_pid_.load();
  s.restart_count = restart_count_.load();
  s.consecutive_restart_count = consecutive_restart_count_.load();
  s.state = state_.load();
  {
    lock_guard<mutex> lock(state_mutex_);
    s.last_start_time = last_start_time_;
  }
  return s;
}

// ---------------------------------------------------------------------------

MonitorProcess::State MonitorProcess::GetState() { return state_.load(); }

// ---------------------------------------------------------------------------

pid_t MonitorProcess::GetChildPid() { return child_pid_.load(); }

// ---------------------------------------------------------------------------
// Monitor thread
// ---------------------------------------------------------------------------

void *MonitorProcess::MonitorThreadEntry(void * /*arg*/) {
  MonitorLoop();
  return nullptr;
}

// ---------------------------------------------------------------------------

void MonitorProcess::MonitorLoop() {
  while (!stop_requested_.load()) {
    // ------------------------------------------------------------------
    // Pre-start callback
    // ------------------------------------------------------------------
    int attempt = restart_count_.load();
    if (config_.on_pre_start) {
      config_.on_pre_start(attempt);
    }

    // ------------------------------------------------------------------
    // Spawn child
    // ------------------------------------------------------------------
    pid_t pid = SpawnChild();
    if (pid < 0) {
      cerr << "[MonitorProcess] SpawnChild() failed, errno=" << errno << "\n";
      // Back-off and retry unless stop was requested.
      auto delay = ComputeBackoff(consecutive_restart_count_.load());
      this_thread::sleep_for(delay);
      continue;
    }

    child_pid_ = pid;
    {
      lock_guard<mutex> lock(state_mutex_);
      last_start_time_ = chrono::steady_clock::now();
    }

    // ------------------------------------------------------------------
    // Monitor loop: health-check + waitpid
    // ------------------------------------------------------------------
    int wait_status = 0;
    bool child_exited = false;

    while (!child_exited) {
      if (stop_requested_.load()) {
        // RequestStop() already sent stop_signal (if child was alive then).
        // Re-send in case child_pid_ was 0 when RequestStop() ran.
        kill(pid, config_.stop_signal);
        bool exited = WaitForChildWithTimeout(config_.graceful_stop_timeout,
                                              &wait_status);
        if (!exited) {
          kill(pid, SIGKILL);
          waitpid(pid, &wait_status, 0);
        }
        child_exited = true;
        break;
      }

      // Poll with a timeout so we can run the health-check.
      auto poll_interval = config_.health_check ? config_.health_check_interval
                                                : chrono::milliseconds{500};

      child_exited = WaitForChildWithTimeout(poll_interval, &wait_status);

      if (!child_exited && config_.health_check) {
        bool healthy = config_.health_check(pid);
        if (!healthy) {
          cerr << "[MonitorProcess] Health-check failed for pid=" << pid
               << "; killing child.\n";
          kill(pid, SIGKILL);
          waitpid(pid, &wait_status, 0);
          child_exited = true;
        }
      }
    }

    // ------------------------------------------------------------------
    // Child has exited
    // ------------------------------------------------------------------
    child_pid_ = 0;

    if (config_.on_exit) {
      config_.on_exit(pid, wait_status);
    }

    if (stop_requested_.load())
      break;

    // Determine whether the child ran long enough to be "stable".
    auto run_duration = chrono::duration_cast<chrono::seconds>(
        chrono::steady_clock::now() - last_start_time_);

    if (config_.stable_run_threshold.count() > 0 &&
        run_duration >= config_.stable_run_threshold) {
      consecutive_restart_count_ = 0;
    }

    restart_count_++;
    consecutive_restart_count_++;

    // ------------------------------------------------------------------
    // Max-restart policy
    // ------------------------------------------------------------------
    int max = config_.max_restarts;
    if (max > 0 && consecutive_restart_count_.load() > max) {
      if (config_.max_restart_policy == MaxRestartPolicy::kStopMonitoring) {
        cerr << "[MonitorProcess] Max restarts (" << max
             << ") reached. Stopping monitor.\n";
        break;
      } else {
        consecutive_restart_count_ = 0;
      }
    }

    // ------------------------------------------------------------------
    // Back-off before next restart
    // ------------------------------------------------------------------
    auto backoff = ComputeBackoff(consecutive_restart_count_.load());
    cerr << "[MonitorProcess] Child pid=" << pid
         << " exited (status=" << wait_status << "). Restarting in "
         << backoff.count() << "ms "
         << "(restart #" << restart_count_.load() << ").\n";

    // Sleep in small increments so we can react to stop_requested_.
    auto remaining = backoff;
    constexpr auto kSlice = chrono::milliseconds{100};
    while (remaining > chrono::milliseconds{0} && !stop_requested_.load()) {
      auto sleep_for = min(remaining, kSlice);
      this_thread::sleep_for(sleep_for);
      remaining -= sleep_for;
    }
  } // outer while

  child_pid_ = 0;
  state_ = State::kStopped;
}

// ---------------------------------------------------------------------------
// SpawnChild
// ---------------------------------------------------------------------------

pid_t MonitorProcess::SpawnChild() {
  // Build argv / envp arrays before forking to avoid heap allocation in child.
  vector<char *> argv_ptrs;
  argv_ptrs.reserve(config_.argv.size() + 1);
  for (auto &s : config_.argv)
    argv_ptrs.push_back(const_cast<char *>(s.c_str()));
  argv_ptrs.push_back(nullptr);

  vector<char *> envp_ptrs;
  if (!config_.env.empty()) {
    envp_ptrs.reserve(config_.env.size() + 1);
    for (auto &s : config_.env)
      envp_ptrs.push_back(const_cast<char *>(s.c_str()));
    envp_ptrs.push_back(nullptr);
  }

  pid_t pid = fork();

  if (pid < 0) {
    // fork() failed.
    return -1;
  }

  if (pid == 0) {
    // ---------------------------------------------------------------
    // Child process
    // ---------------------------------------------------------------

    // New process group so signals from the terminal don't reach us.
    setsid();

    if (config_.close_child_stdin)
      RedirectToDevNull(STDIN_FILENO);
    if (config_.suppress_child_output) {
      RedirectToDevNull(STDOUT_FILENO);
      RedirectToDevNull(STDERR_FILENO);
    }

    if (!config_.working_directory.empty()) {
      if (chdir(config_.working_directory.c_str()) != 0) {
        _exit(127);
      }
    }

    if (!envp_ptrs.empty()) {
      execve(argv_ptrs[0], argv_ptrs.data(), envp_ptrs.data());
    } else {
      execv(argv_ptrs[0], argv_ptrs.data());
    }

    // exec failed.
    _exit(127);
  }

  // Parent: return child PID.
  return pid;
}

// ---------------------------------------------------------------------------
// WaitForChildWithTimeout
// ---------------------------------------------------------------------------

bool MonitorProcess::WaitForChildWithTimeout(chrono::milliseconds timeout,
                                             int *out_status) {
  // Use a polling loop with short sleeps; this avoids SIGCHLD handler
  // complexity while remaining responsive.
  constexpr auto kPollInterval = chrono::milliseconds{10};
  auto deadline = chrono::steady_clock::now() + timeout;

  while (chrono::steady_clock::now() < deadline) {
    int status = 0;
    pid_t result = waitpid(child_pid_.load(), &status, WNOHANG);
    if (result > 0) {
      if (out_status)
        *out_status = status;
      return true;
    }
    if (result < 0 && errno != EINTR) {
      // Child no longer exists (e.g. already reaped).
      if (out_status)
        *out_status = 0;
      return true;
    }
    this_thread::sleep_for(kPollInterval);
  }
  return false;
}

// ---------------------------------------------------------------------------
// GracefulKill
// ---------------------------------------------------------------------------

void MonitorProcess::GracefulKill(pid_t pid) {
  if (pid <= 0)
    return;

  kill(pid, config_.stop_signal);

  int status = 0;
  bool exited = WaitForChildWithTimeout(config_.graceful_stop_timeout, &status);

  if (!exited) {
    cerr << "[MonitorProcess] Graceful stop timed out for pid=" << pid
         << ". Sending SIGKILL.\n";
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
  }
}

// ---------------------------------------------------------------------------
// ComputeBackoff
// ---------------------------------------------------------------------------

chrono::milliseconds MonitorProcess::ComputeBackoff(int consecutive) {
  if (consecutive <= 0)
    return config_.backoff_initial;

  double ms = static_cast<double>(config_.backoff_initial.count());
  for (int i = 0; i < consecutive; ++i) {
    ms *= config_.backoff_multiplier;
    if (ms >= static_cast<double>(config_.backoff_max.count())) {
      return config_.backoff_max;
    }
  }
  return chrono::milliseconds{static_cast<long long>(ms)};
}

// ---------------------------------------------------------------------------
// RedirectToDevNull
// ---------------------------------------------------------------------------

void MonitorProcess::RedirectToDevNull(int fd) {
  int null_fd = open("/dev/null", O_RDWR);
  if (null_fd < 0)
    return;
  dup2(null_fd, fd);
  close(null_fd);
}

} // namespace misc
} // namespace utils
