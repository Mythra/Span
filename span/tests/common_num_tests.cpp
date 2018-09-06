#include "gtest/gtest.h"

#include "span/Common.hh"

namespace {
  TEST(CommonNumTest, AbsTest) {
    const int positive_num = 31;
    const int negative_num = -31;

    EXPECT_EQ(span::int32abs(positive_num), span::int32abs(negative_num));
    EXPECT_EQ(span::int32abs(negative_num), 31);
  }

  TEST(CommonNumTest, Float2IntTest) {
    const float val = 31.2;
    const float other_val = 34.8;

    EXPECT_EQ(span::float2int32(val), 31);
    EXPECT_EQ(span::float2int32(other_val), 35);
  }

  TEST(CommonNumTest, Double2IntTest) {
    const double val = 31.2;
    const double other_val = 34.8;

    EXPECT_EQ(span::double2int32(val), 31);
    EXPECT_EQ(span::double2int32(other_val), 35);
  }
}  // namespace
