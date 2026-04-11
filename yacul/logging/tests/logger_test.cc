#include "absl/log/globals.h"
#include "yacul/basic/basic.h"
#include "yacul/logging/logger.h"

#include <sys/types.h>
#include <sys/wait.h>
#include <thread> // this_thread
#include <unistd.h>

using namespace std;
using namespace utils::logging;

void CallStack2_Segv() {
  LOG(INFO) << "Inside CallStack2_Segv - will cause SEGV";
  this_thread::sleep_for(chrono::milliseconds(100));
  int *p = nullptr;
  *p = 31; // Intentional SEGV
}

void CallStack2_Check() {
  LOG(INFO) << "Inside CallStack2_Check - will trigger CHECK failure";
  this_thread::sleep_for(chrono::milliseconds(100));
  CHECK(false) << "Intentional CHECK failure in thread";
}

void CallStack1_Segv() {
  LOG(INFO) << "Inside CallStack1_Segv";
  CallStack2_Segv();
}

void CallStack1_Check() {
  LOG(INFO) << "Inside CallStack1_Check";
  CallStack2_Check();
}

void TestWithProcesses() {
  LOG(INFO) << "=== Testing with Child Processes ===";

  // Child process 1: SEGV
  pid_t pid1 = fork();
  if (pid1 == 0) {
    // Child process
    LOG(INFO) << "Child process " << getpid() << " - SEGV path";
    CallStack1_Segv();
    exit(0);
  }

  // Child process 2: CHECK failure
  pid_t pid2 = fork();
  if (pid2 == 0) {
    // Child process
    LOG(INFO) << "Child process " << getpid() << " - CHECK path";
    CallStack1_Check();
    exit(0);
  }

  // Parent waits for children
  int status1, status2;
  waitpid(pid1, &status1, 0);
  waitpid(pid2, &status2, 0);

  if (WIFSIGNALED(status1)) {
    LOG(INFO) << "Child 1 (pid " << pid1
              << ") terminated by signal: " << WTERMSIG(status1);
  }

  if (WIFSIGNALED(status2)) {
    LOG(INFO) << "Child 2 (pid " << pid2
              << ") terminated by signal: " << WTERMSIG(status2);
  }
}

int main(int argc, char **argv) {
  Logger::Init(argc, argv);
  Logger::EnableSignalHandlers();

  LOG(INFO) << "This is an info log message";
  LOG(WARNING) << "This is a warning log message";
  LOG(ERROR) << "This is an error log message";

  absl::SetVLogLevel("logger_test", 4);
  VLOG(4) << "This is a verbose 4 log message";

  int x = 42;
  double y = 3.14;
  string name = "Alice";
  LOG(INFO) << "Logging variables: " << LOGVARS(x, y, name);

  TestWithProcesses();

  Logger::Shutdown();
  return 0;
}
