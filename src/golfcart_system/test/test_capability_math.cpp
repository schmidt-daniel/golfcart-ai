#include "golfcart_system/capability_math.hpp"
#include "gtest/gtest.h"

namespace golfcart
{

TEST(CapabilityTracker, AbsentBeforeFirstHeartbeat)
{
  CapabilityTracker t(2.0);
  EXPECT_FALSE(t.present(0.0));
}

TEST(CapabilityTracker, PresentAfterValidHeartbeat)
{
  CapabilityTracker t(2.0);
  EXPECT_TRUE(t.update(1.0, true));
  EXPECT_TRUE(t.present(1.5));
}

TEST(CapabilityTracker, AbsentAfterInvalidHeartbeat)
{
  CapabilityTracker t(2.0);
  t.update(1.0, false);
  EXPECT_FALSE(t.present(1.5));
}

TEST(CapabilityTracker, AbsentAfterTimeout)
{
  CapabilityTracker t(2.0);
  t.update(1.0, true);
  // 2.5 s later (> 2.0 s timeout) -> absent.
  EXPECT_FALSE(t.present(3.5));
}

TEST(CapabilityTracker, PresentWithinTimeout)
{
  CapabilityTracker t(2.0);
  t.update(1.0, true);
  // 1.5 s later (< 2.0 s timeout) -> present.
  EXPECT_TRUE(t.present(2.5));
}

TEST(CapabilityTracker, ResetClears)
{
  CapabilityTracker t(2.0);
  t.update(1.0, true);
  t.reset();
  EXPECT_FALSE(t.present(1.5));
}

TEST(CapabilityTracker, RecoversAfterInvalid)
{
  CapabilityTracker t(2.0);
  t.update(1.0, false);
  EXPECT_FALSE(t.present(1.5));
  // A new valid heartbeat recovers it.
  EXPECT_TRUE(t.update(2.0, true));
  EXPECT_TRUE(t.present(2.5));
}

}  // namespace golfcart