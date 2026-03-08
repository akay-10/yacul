#ifndef UTILS_MISC_MONITOR_PROCESS_H
#define UTILS_MISC_MONITOR_PROCESS_H

#include "basic/basic.h"

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace utils {
namespace misc {

// MonitorProcess: A static (non-instantiable) class that forks a child process,
// monitors it for abnormal termination or signal delivery, and automatically
// restarts it according to a configurable restart policy.
//
// Design constraints
// ------------------
//  * All state lives in static storage – no instances are created.
//  * Thread-safe: all public entry points are protected by an internal mutex
//    except the signal handlers (which only touch async-signal-safe objects).
//  * The monitoring loop runs in a dedicated POSIX thread so the caller's
//    thread is never blocked.
//  * Production hardening: exponential back-off, max-restart cap, per-restart
//    and lifetime callbacks, health-check hook, graceful shutdown.
//
// Typical usage
// -------------
//   MonitorProcess::Config cfg;
//   cfg.argv          = {"/usr/bin/my-service", "--port=8080"};
//   cfg.max_restarts  = 10;
//   cfg.on_exit       = [](pid_t pid, int status) { LOG(...); };
//
//   MonitorProcess::Start(cfg);
//   // ... application does other work ...
//   MonitorProcess::RequestStop();
//   MonitorProcess::WaitForStop();
//
class MonitorProcess {
public:
  // -----------------------------------------------------------------------
  // Public types
  // -----------------------------------------------------------------------

  // Callback invoked each time the child process exits (naturally or via
  // signal).  Called from the monitor thread – must be re-entrant.
  using ExitCallback = std::function<void(pid_t pid, int wait_status)>;

  // Callback invoked just before the child is (re)started.
  // |attempt| is 0-based (0 = first start, 1 = first restart, …).
  using PreStartCallback = std::function<void(int attempt)>;

  // Health-check hook.  If provided, the monitor thread calls it every
  // |health_check_interval| and sends SIGKILL to the child when it returns
  // false.  The child is then restarted as if it had crashed.
  using HealthCheckCallback = std::function<bool(pid_t child_pid)>;

  // Restart policy when the consecutive-restart count reaches |max_restarts|.
  enum class MaxRestartPolicy {
    kStopMonitoring, // Stop the monitor; leave the child dead.
    kResetCounter,   // Reset the counter and keep restarting indefinitely.
  };

  // -----------------------------------------------------------------------
  // Configuration
  // -----------------------------------------------------------------------
  struct Config {
    // Mandatory: argv[0] is the executable path; subsequent entries are args.
    std::vector<std::string> argv;

    // Optional: environment variables ("KEY=VALUE").  If empty the child
    // inherits the parent's environment.
    std::vector<std::string> env;

    // Optional: working directory for the child.  Empty = inherit parent's.
    std::string working_directory;

    // Maximum number of consecutive restarts before |max_restart_policy|
    // is applied.  0 means unlimited.
    int max_restarts = 0;

    MaxRestartPolicy max_restart_policy = MaxRestartPolicy::kStopMonitoring;

    // Initial back-off before the first restart (ms).
    std::chrono::milliseconds backoff_initial{500};

    // Multiplier applied to back-off after each consecutive restart.
    double backoff_multiplier = 1.5;

    // Upper bound on back-off regardless of multiplier.
    std::chrono::milliseconds backoff_max{30'000};

    // If the child runs for at least this duration before dying, the
    // consecutive-restart counter is reset (the process is considered to have
    // started successfully).  0 = never reset.
    std::chrono::seconds stable_run_threshold{5};

    // How often the monitor thread polls the health-check hook.
    // Ignored when |health_check| is null.
    std::chrono::milliseconds health_check_interval{1'000};

    // Signal sent to request a graceful stop of the child.
    int stop_signal = SIGTERM;

    // How long to wait for the child to exit after |stop_signal| before
    // sending SIGKILL.
    std::chrono::milliseconds graceful_stop_timeout{5'000};

    // Callbacks – all optional.
    ExitCallback on_exit;
    PreStartCallback on_pre_start;
    HealthCheckCallback health_check;

    // If true, stdout/stderr of the child are redirected to /dev/null.
    bool suppress_child_output = false;

    // If true, the child's stdin is closed (redirected to /dev/null).
    bool close_child_stdin = true;
  };

  // -----------------------------------------------------------------------
  // Monitor state (observable by callers)
  // -----------------------------------------------------------------------
  enum class State {
    kIdle,     // Start() has not been called yet (or after WaitForStop).
    kRunning,  // Monitor thread + child process are active.
    kStopping, // RequestStop() has been called; waiting for child to exit.
    kStopped,  // Monitor thread has exited cleanly.
  };

  // Snapshot of runtime counters (returned by GetStats()).
  struct Stats {
    pid_t current_child_pid;       // 0 if not running.
    int restart_count;             // Total restarts so far.
    int consecutive_restart_count; // Resets on stable run.
    State state;
    std::chrono::steady_clock::time_point last_start_time;
  };

  // -----------------------------------------------------------------------
  // Public API (all static)
  // -----------------------------------------------------------------------

  // Start monitoring.  Spawns the monitor thread which immediately forks
  // the child.  Returns true on success; false if already running or if
  // |cfg| is invalid (e.g. empty argv).
  static bool Start(Config cfg);

  // Ask the monitor to stop: sends |stop_signal| to the child, waits up to
  // |graceful_stop_timeout|, then SIGKILL's it, and tears down the monitor
  // thread.  Non-blocking: the caller must call WaitForStop() to block until
  // completion.
  static void RequestStop();

  // Block until the monitor thread has fully exited.  Safe to call even if
  // the monitor has already stopped.
  static void WaitForStop();

  // Send an arbitrary signal to the currently running child.  Returns false
  // if no child is alive or signal delivery fails.
  static bool SendSignalToChild(int signum);

  // Return an atomic snapshot of current statistics.
  static Stats GetStats();

  // Return the current state.
  static State GetState();

  // Return the PID of the current child (0 if none).
  static pid_t GetChildPid();

  // Prevent any object construction – this is a static-only class.
  MonitorProcess() = delete;
  ~MonitorProcess() = delete;
  DISALLOW_COPY_AND_ASSIGN(MonitorProcess);
  DISALLOW_MOVE_AND_ASSIGN(MonitorProcess);

private:
  // -----------------------------------------------------------------------
  // Internal helpers
  // -----------------------------------------------------------------------

  // Entry point for the monitor thread.
  static void *MonitorThreadEntry(void *arg);

  // Core monitor loop executed by the monitor thread.
  static void MonitorLoop();

  // Fork + exec the child process.  Returns the child PID on success, -1 on
  // error.
  static pid_t SpawnChild();

  // Wait for the child to exit (with an optional timeout for health-check
  // polling).  Returns true if the child exited, false if it is still alive
  // (timed-out wait).
  static bool WaitForChildWithTimeout(std::chrono::milliseconds timeout,
                                      int *out_status);

  // Perform graceful shutdown: send stop_signal, wait, escalate to SIGKILL.
  static void GracefulKill(pid_t pid);

  // Compute the back-off duration for the given consecutive-restart index.
  static std::chrono::milliseconds ComputeBackoff(int consecutive);

  // Redirect fd to /dev/null.
  static void RedirectToDevNull(int fd);

  // -----------------------------------------------------------------------
  // Internal state (all static)
  // -----------------------------------------------------------------------

  // Guards all mutable state below.
  static std::mutex state_mutex_;

  // Configuration supplied via Start().
  static Config config_;

  // Current high-level state.
  static std::atomic<State> state_;

  // PID of the currently monitored child.  0 when none.
  static std::atomic<pid_t> child_pid_;

  // Counters.
  static std::atomic<int> restart_count_;
  static std::atomic<int> consecutive_restart_count_;

  // Timestamp of when the current child was started.
  static std::chrono::steady_clock::time_point last_start_time_;

  // POSIX thread handle for the monitor thread.
  static pthread_t monitor_thread_;
  static bool monitor_thread_valid_;

  // Flag set by RequestStop() to break the monitor loop.
  static std::atomic<bool> stop_requested_;
};

} // namespace misc
} // namespace utils

#endif // UTILS_MISC_MONITOR_PROCESS_H
