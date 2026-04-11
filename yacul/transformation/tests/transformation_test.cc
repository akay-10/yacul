#include <cstdio>
#include <gtest/gtest.h>
#include <string>

// ---------------------------------------------------------------------------
// Registered as a global GTest environment.  Use this for any one-time
// process-level initialisation (e.g. OpenSSL RAND seed, CPU feature probing).
// ---------------------------------------------------------------------------
class DataTransformEnvironment : public ::testing::Environment {
public:
  void SetUp() override {
    std::puts("DataTransform test environment initialised.");
  }

  void TearDown() override {
    std::puts("DataTransform test environment torn down.");
  }
};

// ---------------------------------------------------------------------------
// main()
// ---------------------------------------------------------------------------
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new DataTransformEnvironment());
  return RUN_ALL_TESTS();
}
