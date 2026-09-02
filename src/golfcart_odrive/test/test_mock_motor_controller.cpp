#include <gtest/gtest.h>

#include "golfcart_odrive/mock_motor_controller.hpp"

using golfcart::MockMotorController;

TEST(MockMotorController, StartsIdle)
{
  MockMotorController mc;
  EXPECT_EQ(mc.get_state(), "idle");
  EXPECT_TRUE(mc.get_fault().empty());
}

TEST(MockMotorController, CommandsVelocityAndIntegrates)
{
  MockMotorController mc(0.05);
  mc.command_velocity(1.0, 1.0);
  // Advance simulation to approach commanded velocity.
  for (int i = 0; i < 100; ++i) {
    mc.update(0.05);
  }
  double l = 0.0, r = 0.0;
  mc.get_velocity(l, r);
  EXPECT_NEAR(l, 1.0, 0.05);
  EXPECT_NEAR(r, 1.0, 0.05);
  double lp = 0.0, rp = 0.0;
  mc.get_position(lp, rp);
  EXPECT_GT(lp, 0.0);
  EXPECT_GT(rp, 0.0);
}

TEST(MockMotorController, StopZeroesCommand)
{
  MockMotorController mc;
  mc.command_velocity(1.0, -1.0);
  mc.stop();
  double l = 0.0, r = 0.0;
  mc.get_velocity(l, r);
  EXPECT_NEAR(l, 0.0, 1e-6);
  EXPECT_NEAR(r, 0.0, 1e-6);
}

TEST(MockMotorController, FaultState)
{
  MockMotorController mc;
  mc.set_fault("overcurrent");
  EXPECT_EQ(mc.get_state(), "fault");
  EXPECT_EQ(mc.get_fault(), "overcurrent");
  mc.clear_fault();
  EXPECT_TRUE(mc.get_fault().empty());
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
