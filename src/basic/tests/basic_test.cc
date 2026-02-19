#include <sstream>

#include "basic.h"
#include "gtest/gtest.h"

TEST(BasicTest, LOGVARSTest) {
  char c = 'A';
  int i = 10;
  double d = 20.5;
  std::string s = "hello";

  std::ostringstream oss;
  oss << LOGVARS(c, i, d, s);

  EXPECT_EQ(oss.str(), "c=A,i=10,d=20.5,s=hello");
}

TEST(BasicTest, MathFunctionsTest) {
  EXPECT_EQ(CeilDiv(10, 3), 4);
  EXPECT_EQ(RoundUp(10, 3), 12);
  EXPECT_EQ(RoundDown(10, 3), 9);
  EXPECT_TRUE(IsPowerOfTwo(16));
  EXPECT_FALSE(IsPowerOfTwo(18));
  EXPECT_EQ(NextPowerOfTwo(17), 32);
  EXPECT_EQ(PrevPowerOfTwo(17), 16);
}
