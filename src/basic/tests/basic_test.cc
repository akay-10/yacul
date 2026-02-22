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
  EXPECT_TRUE(IsPowerOfTwo(16));
  EXPECT_FALSE(IsPowerOfTwo(18));

  EXPECT_EQ(CeilDiv(10, 3), 4);
  EXPECT_EQ(CeilDiv(11, 2), 6);

  EXPECT_EQ(AlignUp(10, 3), 12);
  EXPECT_EQ(AlignUp(10, 2), 10);
  EXPECT_EQ(AlignUp(17, 4), 20);

  EXPECT_EQ(AlignDown(10, 3), 9);
  EXPECT_EQ(AlignDown(10, 2), 10);
  EXPECT_EQ(AlignDown(17, 4), 16);

  EXPECT_EQ(Log2(16), 4);
  EXPECT_EQ(Log2(19), 4);

  EXPECT_EQ(NextPowerOfTwo(17), 32);
  EXPECT_EQ(NextPowerOfTwo(16), 16);

  EXPECT_EQ(PrevPowerOfTwo(17), 16);
  EXPECT_EQ(PrevPowerOfTwo(16), 16);
}
