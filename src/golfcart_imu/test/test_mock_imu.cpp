#include <gtest/gtest.h>

#include <cmath>

#include "golfcart_imu/mock_imu_sensor.hpp"

using golfcart::MockImuSensor;

TEST(MockImu, LevelGravityDown)
{
  MockImuSensor imu(0.0, 0.0);
  auto s = imu.read();
  EXPECT_TRUE(s.valid);
  EXPECT_NEAR(s.accel_x, 0.0, 1e-6);
  EXPECT_NEAR(s.accel_y, 0.0, 1e-6);
  EXPECT_NEAR(s.accel_z, 9.81, 1e-3);  // reaction force, level -> +g
}

TEST(MockImu, RollTilt)
{
  MockImuSensor imu(0.3, 0.0);  // 0.3 rad roll
  auto s = imu.read();
  // roll = atan2(ay, az)
  const double roll = std::atan2(s.accel_y, s.accel_z);
  EXPECT_NEAR(roll, 0.3, 1e-3);
}

TEST(MockImu, PitchTilt)
{
  MockImuSensor imu(0.0, -0.2);  // -0.2 rad pitch
  auto s = imu.read();
  const double pitch = std::atan2(-s.accel_x, std::sqrt(s.accel_y * s.accel_y + s.accel_z * s.accel_z));
  EXPECT_NEAR(pitch, -0.2, 1e-3);
}

TEST(MockImu, SetTilt)
{
  MockImuSensor imu;
  imu.set_tilt(0.5, 0.0);
  auto s = imu.read();
  const double roll = std::atan2(s.accel_y, s.accel_z);
  EXPECT_NEAR(roll, 0.5, 1e-3);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}