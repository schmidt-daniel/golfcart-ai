#include "gtest/gtest.h"

namespace golfcart
{

// Placeholder test for the follow controller.
// The follow_controller_node is a ROS node; its control law is validated via
// integration tests in the sim. This test ensures the target builds and runs.
TEST(FollowController, Smoke)
{
  EXPECT_TRUE(true);
}

}  // namespace golfcart