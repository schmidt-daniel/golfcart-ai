#include "golfcart_follow/reid_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

TEST(ReIdMath, IdleByDefault)
{
  ReIdTracker t(30.0, 3.0);
  EXPECT_EQ(t.state(), REID_IDLE);
  EXPECT_FALSE(t.searching());
  EXPECT_FALSE(t.reacquired());
}

TEST(ReIdMath, BeginSearchEntersSearching)
{
  ReIdTracker t(30.0, 3.0);
  t.begin_search(0.0, 1.0, 0.0);
  EXPECT_TRUE(t.searching());
  EXPECT_EQ(t.state(), REID_SEARCHING);
}

TEST(ReIdMath, ReacquireWithinGap)
{
  ReIdTracker t(30.0, 3.0);
  t.begin_search(0.0, 1.0, 0.0);
  // Candidate 2 m from last-known position -> re-acquired.
  EXPECT_EQ(t.update(1.0, 2.0, 1.0), REID_REACQUIRED);
  EXPECT_TRUE(t.reacquired());
}

TEST(ReIdMath, NoReacquireOutsideGap)
{
  ReIdTracker t(30.0, 3.0);
  t.begin_search(0.0, 0.0, 0.0);
  // Candidate 10 m away -> still searching.
  EXPECT_EQ(t.update(1.0, 10.0, 0.0), REID_SEARCHING);
  EXPECT_TRUE(t.searching());
}

TEST(ReIdMath, TimeoutGivesUp)
{
  ReIdTracker t(5.0, 3.0);
  t.begin_search(0.0, 0.0, 0.0);
  // 6 s later (> 5 s timeout) -> timeout, even if within gap.
  EXPECT_EQ(t.update(6.0, 1.0, 0.0), REID_TIMEOUT);
  EXPECT_FALSE(t.searching());
}

TEST(ReIdMath, ResetClears)
{
  ReIdTracker t(30.0, 3.0);
  t.begin_search(0.0, 0.0, 0.0);
  t.update(1.0, 1.0, 0.0);
  EXPECT_TRUE(t.reacquired());
  t.reset();
  EXPECT_EQ(t.state(), REID_IDLE);
}

}  // namespace golfcart