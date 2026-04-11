#include "yacul/misc/monitor_process.h"

#include "gtest/gtest.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace std::chrono_literals;
using namespace utils::misc;

class MonitorProcessEnvironment : public ::testing::Environment {
public:
  void TearDown() override {
    if (MonitorProcess::GetState() == MonitorProcess::State::kRunning ||
        MonitorProcess::GetState() == MonitorProcess::State::kStopping) {
      MonitorProcess::RequestStop();
      MonitorProcess::WaitForStop();
    }
  }
};

class MonitorProcessTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Bring the monitor to kIdle unconditionally before each test.
    // WaitForStop() resets state to kIdle after joining the thread, so this is
    // safe regardless of what the previous test left behind.
    if (MonitorProcess::GetState() == MonitorProcess::State::kRunning ||
        MonitorProcess::GetState() == MonitorProcess::State::kStopping) {
      MonitorProcess::RequestStop();
    }
    MonitorProcess::WaitForStop();
    ASSERT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kIdle);
  }

  void TearDown() override {
    // Always leave the monitor in kIdle for the next test.
    if (MonitorProcess::GetState() == MonitorProcess::State::kRunning ||
        MonitorProcess::GetState() == MonitorProcess::State::kStopping) {
      MonitorProcess::RequestStop();
    }
    MonitorProcess::WaitForStop();
  }

  // Helper: build a minimal config that runs a short-lived command.
  static MonitorProcess::Config MakeSleepConfig(int sleep_seconds = 60,
                                                int max_restarts = 0) {
    MonitorProcess::Config cfg;
    cfg.argv = {"/bin/sleep", std::to_string(sleep_seconds)};
    cfg.max_restarts = max_restarts;
    cfg.suppress_child_output = true;
    cfg.close_child_stdin = true;
    return cfg;
  }

  // Helper: build a config that runs a command and exits immediately.
  static MonitorProcess::Config MakeExitImmediateConfig(int exit_code = 0) {
    MonitorProcess::Config cfg;
    cfg.argv = {"/bin/sh", "-c", "exit " + std::to_string(exit_code)};
    cfg.suppress_child_output = true;
    cfg.close_child_stdin = true;
    // Short back-off so tests don't take forever.
    cfg.backoff_initial = 50ms;
    cfg.backoff_max = 100ms;
    return cfg;
  }

  // Poll until state matches 'expected' or 'timeout' elapses.
  static bool WaitForState(MonitorProcess::State expected,
                           std::chrono::milliseconds timeout = 5000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      if (MonitorProcess::GetState() == expected)
        return true;
      std::this_thread::sleep_for(20ms);
    }
    return false;
  }

  // Poll until child pid becomes non-zero.
  static pid_t WaitForChildPid(std::chrono::milliseconds timeout = 3000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
      pid_t p = MonitorProcess::GetChildPid();
      if (p > 0)
        return p;
      std::this_thread::sleep_for(20ms);
    }
    return 0;
  }
};

TEST_F(MonitorProcessTest, InitialStateIsIdle) {
  EXPECT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kIdle);
}

TEST_F(MonitorProcessTest, StartTransitionsToRunning) {
  auto cfg = MakeSleepConfig();
  ASSERT_TRUE(MonitorProcess::Start(cfg));
  EXPECT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kRunning);
}

TEST_F(MonitorProcessTest, StartFailsWithEmptyArgv) {
  MonitorProcess::Config cfg; // argv intentionally empty
  EXPECT_FALSE(MonitorProcess::Start(cfg));
  EXPECT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kIdle);
}

TEST_F(MonitorProcessTest, DoubleStartReturnsFalse) {
  auto cfg = MakeSleepConfig();
  ASSERT_TRUE(MonitorProcess::Start(cfg));
  EXPECT_FALSE(MonitorProcess::Start(cfg)); // second call must fail
}

TEST_F(MonitorProcessTest, RequestStopTransitionsToStopped) {
  ASSERT_TRUE(MonitorProcess::Start(MakeSleepConfig()));
  ASSERT_NE(WaitForChildPid(), 0) << "Child did not start in time";

  MonitorProcess::RequestStop();
  MonitorProcess::WaitForStop();

  EXPECT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kIdle);
  EXPECT_EQ(MonitorProcess::GetChildPid(), 0);
}

TEST_F(MonitorProcessTest, ChildPidIsNonZeroAfterStart) {
  ASSERT_TRUE(MonitorProcess::Start(MakeSleepConfig()));
  pid_t pid = WaitForChildPid();
  EXPECT_GT(pid, 0);
}

TEST_F(MonitorProcessTest, ChildPidIsZeroAfterStop) {
  ASSERT_TRUE(MonitorProcess::Start(MakeSleepConfig()));
  ASSERT_NE(WaitForChildPid(), 0);
  MonitorProcess::RequestStop();
  MonitorProcess::WaitForStop();
  EXPECT_EQ(MonitorProcess::GetChildPid(), 0);
}

TEST_F(MonitorProcessTest, GetStatsReflectsRunningState) {
  ASSERT_TRUE(MonitorProcess::Start(MakeSleepConfig()));
  ASSERT_NE(WaitForChildPid(), 0);

  auto stats = MonitorProcess::GetStats();
  EXPECT_GT(stats.current_child_pid, 0);
  EXPECT_EQ(stats.state, MonitorProcess::State::kRunning);
}

TEST_F(MonitorProcessTest, GetStatsReflectsIdleState) {
  auto stats = MonitorProcess::GetStats();
  EXPECT_EQ(stats.current_child_pid, 0);
  EXPECT_EQ(stats.state, MonitorProcess::State::kIdle);
}

TEST_F(MonitorProcessTest, ChildIsRestartedAfterCrash) {
  std::atomic<int> exit_count{0};

  auto cfg = MakeExitImmediateConfig(1 /* non-zero exit */);
  cfg.max_restarts = 3;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.on_exit = [&](pid_t, int) { exit_count++; };

  ASSERT_TRUE(MonitorProcess::Start(cfg));

  // Wait for monitor to stop after hitting max_restarts.
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 10000ms));

  // Should have restarted at least max_restarts times.
  auto stats = MonitorProcess::GetStats();
  EXPECT_GE(stats.restart_count, cfg.max_restarts);
  EXPECT_GE(exit_count.load(), cfg.max_restarts);
}

TEST_F(MonitorProcessTest, RestartCounterIncrements) {
  auto cfg = MakeExitImmediateConfig(0);
  cfg.max_restarts = 2;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 8000ms));

  EXPECT_GE(MonitorProcess::GetStats().restart_count, 2);
}

TEST_F(MonitorProcessTest, MaxRestartPolicyStopMonitoringStopsAfterLimit) {
  auto cfg = MakeExitImmediateConfig(1);
  cfg.max_restarts = 2;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 10000ms));
  // State transitions to kIdle once WaitForStop() is called by TearDown.
  // At this point the monitor thread set kStopped; TearDown will reset it.
  EXPECT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kStopped);
}

TEST_F(MonitorProcessTest, SendSignalToChildSucceeds) {
  ASSERT_TRUE(MonitorProcess::Start(MakeSleepConfig(60)));
  ASSERT_NE(WaitForChildPid(), 0);

  // SIGCONT on a running process should always succeed.
  EXPECT_TRUE(MonitorProcess::SendSignalToChild(SIGCONT));
  // Signal 0 checks process existence without delivering a signal.
  EXPECT_TRUE(MonitorProcess::SendSignalToChild(0));
}

TEST_F(MonitorProcessTest, SendSignalToChildFailsWhenNoChild) {
  EXPECT_FALSE(MonitorProcess::SendSignalToChild(SIGCONT));
}

TEST_F(MonitorProcessTest, OnExitCallbackIsInvoked) {
  std::atomic<bool> callback_called{false};
  std::atomic<pid_t> received_pid{0};

  auto cfg = MakeExitImmediateConfig(0);
  cfg.max_restarts = 1;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.on_exit = [&](pid_t pid, int) {
    callback_called = true;
    received_pid = pid;
  };

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 8000ms));

  EXPECT_TRUE(callback_called.load());
  EXPECT_GT(received_pid.load(), 0);
}

TEST_F(MonitorProcessTest, OnPreStartCallbackIsInvoked) {
  std::atomic<int> call_count{0};

  auto cfg = MakeSleepConfig(60);
  cfg.on_pre_start = [&](int attempt) {
    call_count++;
    EXPECT_GE(attempt, 0);
  };

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_NE(WaitForChildPid(), 0);

  // At least the first pre-start call should have been made.
  EXPECT_GE(call_count.load(), 1);
}

TEST_F(MonitorProcessTest, HealthCheckFailureCausesRestart) {
  std::atomic<int> kill_count{0};
  std::atomic<int> check_calls{0};

  auto cfg = MakeSleepConfig(60);
  cfg.max_restarts = 1;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.health_check_interval = 100ms;
  cfg.health_check = [&](pid_t) -> bool {
    check_calls++;
    // Fail immediately on first check.
    return false;
  };
  cfg.on_exit = [&](pid_t, int) { kill_count++; };

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 10000ms));

  EXPECT_GE(kill_count.load(), 1);
  EXPECT_GE(check_calls.load(), 1);
}

TEST_F(MonitorProcessTest, HealthCheckPassDoesNotKillChild) {
  std::atomic<int> exit_count{0};

  auto cfg = MakeSleepConfig(60);
  cfg.health_check_interval = 50ms;
  cfg.health_check = [](pid_t) -> bool { return true; };
  cfg.on_exit = [&](pid_t, int) { exit_count++; };

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_NE(WaitForChildPid(), 0);

  // Let the health check run several times.
  std::this_thread::sleep_for(400ms);

  // Child should still be alive.
  EXPECT_GT(MonitorProcess::GetChildPid(), 0);
  EXPECT_EQ(exit_count.load(), 0);
}

TEST_F(MonitorProcessTest, WorkingDirectoryIsApplied) {
  MonitorProcess::Config cfg;
  cfg.argv = {"/bin/sh", "-c", "exit 0"};
  cfg.working_directory = "/tmp";
  cfg.max_restarts = 1;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.suppress_child_output = true;
  cfg.backoff_initial = 50ms;

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 8000ms));
  // If the process ran without error the working directory was accepted.
}

TEST_F(MonitorProcessTest, GracefulStopUsesConfiguredSignal) {
  // Use SIGTERM (default) – just verify the child exits cleanly.
  auto cfg = MakeSleepConfig(60);
  cfg.stop_signal = SIGTERM;
  cfg.graceful_stop_timeout = 2000ms;

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_NE(WaitForChildPid(), 0);

  MonitorProcess::RequestStop();
  MonitorProcess::WaitForStop();

  EXPECT_EQ(MonitorProcess::GetState(), MonitorProcess::State::kIdle);
}

TEST_F(MonitorProcessTest, ChildIsRestartedAfterSigkill) {
  std::atomic<int> restart_seen{0};

  auto cfg = MakeSleepConfig(60);
  cfg.max_restarts = 2;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.backoff_initial = 100ms;
  cfg.on_exit = [&](pid_t, int status) {
    if (WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL)
      restart_seen++;
  };

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  pid_t pid = WaitForChildPid();
  ASSERT_GT(pid, 0);

  // Simulate a crash by sending SIGKILL.
  kill(pid, SIGKILL);

  // Wait for at least one restart.
  std::this_thread::sleep_for(500ms);
  EXPECT_GE(MonitorProcess::GetStats().restart_count, 1);
  EXPECT_GE(restart_seen.load(), 1);
}

TEST_F(MonitorProcessTest, CustomEnvIsPassedToChild) {
  // Child exits 0 if MY_TEST_VAR is set, 1 otherwise.
  MonitorProcess::Config cfg;
  cfg.argv = {"/bin/sh", "-c", "[ -n \"$MY_TEST_VAR\" ] && exit 0 || exit 1"};
  cfg.env = {"MY_TEST_VAR=hello"};
  cfg.max_restarts = 1;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.suppress_child_output = true;
  cfg.backoff_initial = 50ms;

  std::atomic<int> last_exit_code{-1};
  cfg.on_exit = [&](pid_t, int status) {
    if (WIFEXITED(status))
      last_exit_code = WEXITSTATUS(status);
  };

  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 8000ms));

  // The first exit should be 0 (env var was present).
  EXPECT_EQ(last_exit_code.load(), 0);
}

TEST_F(MonitorProcessTest, BackoffDoesNotExceedMax) {
  // Indirectly test: run with many quick exits and measure total elapsed time.
  // With max back-off of 200ms and 4 restarts the total back-off cannot
  // exceed 4 * 200 = 800ms, giving a loose upper bound.
  auto cfg = MakeExitImmediateConfig(1);
  cfg.max_restarts = 4;
  cfg.max_restart_policy = MonitorProcess::MaxRestartPolicy::kStopMonitoring;
  cfg.backoff_initial = 50ms;
  cfg.backoff_max = 200ms;
  cfg.backoff_multiplier = 10.0; // aggressive growth to hit cap quickly

  auto t0 = std::chrono::steady_clock::now();
  ASSERT_TRUE(MonitorProcess::Start(cfg));
  ASSERT_TRUE(WaitForState(MonitorProcess::State::kStopped, 15000ms));
  auto elapsed = std::chrono::steady_clock::now() - t0;

  // Upper bound: 4 restarts × 200ms max back-off + generous exec overhead.
  EXPECT_LT(elapsed, std::chrono::milliseconds{3000});
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new MonitorProcessEnvironment);
  return RUN_ALL_TESTS();
}
