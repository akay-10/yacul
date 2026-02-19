#include "gtest/gtest.h"
#include "os_murmur_hash3.h"

TEST(OSUsageTest, MurmuruHash3Test) {
  const char *input = "Hello world!";
  uint32_t seed = 31;
  uint32_t hashes[4];
  
  MurmurHash3_x86_32(input, strlen(input), seed, hashes);
  std::ostringstream oss;
  oss << std::hex << hashes[0];
  EXPECT_EQ(oss.str(), "b1ca2320");

  MurmurHash3_x86_128(input, strlen(input), seed, hashes);
  oss.clear();
  oss << std::hex << hashes[0] << hashes[1] << hashes[2] << hashes[3];
  EXPECT_EQ(oss.str(), "b1ca2320225693c3fedac75be343ff3269cd2e");

  MurmurHash3_x64_128(input, strlen(input), seed, hashes);
  oss.clear();
  oss << std::hex << hashes[0] << hashes[1] << hashes[2] << hashes[3];
  EXPECT_EQ(oss.str(),"b1ca2320225693c3fedac75be343ff3269cd2e3173a8b"
            "111ecd02d95e03d7efc3b1fbc");
}

