/**
 * @file tests/unit/test_stream_policy.cpp
 * @brief Adaptive FEC and the still-picture encode rate.
 */
#include <src/stream_policy.h>

#include "../tests_common.h"

using namespace std::chrono_literals;


TEST(StreamPolicy, FecStartsAtTheCeilingFallsWhenCleanAndReturnsOnLoss) {
  const stream_policy::clock::time_point t0 {1000s};
  stream_policy::adaptive_fec_t fec {t0};
  EXPECT_EQ(fec.percentage(20, t0), 20);
  EXPECT_EQ(fec.percentage(20, t0 + 9s), 20);
  EXPECT_EQ(fec.percentage(20, t0 + 10s), 5);
  fec.loss(t0 + 40s);
  EXPECT_EQ(fec.percentage(20, t0 + 40s), 20);
  EXPECT_EQ(fec.percentage(20, t0 + 69s), 20);
  EXPECT_EQ(fec.percentage(20, t0 + 70s), 5);
  EXPECT_EQ(fec.percentage(3, t0 + 70s), 3);  // a ceiling under the floor stays
}

TEST(StreamPolicy, StillPictureKeepsTheActiveRateWhileConvergingThenIdles) {
  const stream_policy::clock::time_point t0 {1000s};
  stream_policy::still_pacer_t idles {41ms, true};
  idles.new_picture(t0);
  EXPECT_EQ(idles.wait(t0 + 100ms), 41ms);
  EXPECT_EQ(idles.wait(t0 + 499ms), 41ms);
  EXPECT_EQ(idles.wait(t0 + 500ms), 500ms);
  idles.new_picture(t0 + 2s);
  EXPECT_EQ(idles.wait(t0 + 2s), 41ms);

  stream_policy::still_pacer_t today {41ms, false};
  today.new_picture(t0);
  EXPECT_EQ(today.wait(t0 + 10s), 41ms);
}
