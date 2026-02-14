#include "absl/log/globals.h"
#include "basic/basic.h"
#include "logger.h"
#include <random>

using namespace std;
using namespace utils::logging;

void CallStack2() {
  LOG(INFO) << "Inside CallStack2";
  random_device rd; 
  mt19937 gen(rd()); 
  uniform_int_distribution<> distrib(0, 1);
  if (true || distrib(gen) == 0) {
    int *p = nullptr;
    // SEGV
    *p = 31;
  }
  CHECK(false) << "Intentional FATAL";
}

void CallStack1() {
  LOG(INFO) << "Inside CallStack1";
  CallStack2();
}

int main (int argc, char** argv) {
  Logger::Init(argc, argv, "logger_test");
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
  
  CallStack1();

  utils::logging::Logger::Shutdown();
  return 0;
}
