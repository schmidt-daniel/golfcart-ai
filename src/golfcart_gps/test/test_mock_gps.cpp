#include <gtest/gtest.h>

#include "golfcart_gps/mock_gps_sensor.hpp"

using golfcart::MockGpsSensor;

TEST(MockGps, DefaultPosition)
{
  MockGpsSensor gps;
  auto s = gps.read();
  EXPECT_TRUE(s.valid);
  EXPECT_NEAR(s.latitude_deg, 51.5, 1e-6);
  EXPECT_NEAR(s.longitude_deg, -0.12, 1e-6);
}

TEST(MockGps, SetPosition)
{
  MockGpsSensor gps;
  gps.set_position(52.0, 13.0);
  auto s = gps.read();
  EXPECT_NEAR(s.latitude_deg, 52.0, 1e-6);
  EXPECT_NEAR(s.longitude_deg, 13.0, 1e-6);
}

TEST(MockGps, CustomInit)
{
  MockGpsSensor gps(40.0, -74.0, 10.0);
  auto s = gps.read();
  EXPECT_NEAR(s.latitude_deg, 40.0, 1e-6);
  EXPECT_NEAR(s.longitude_deg, -74.0, 1e-6);
  EXPECT_NEAR(s.altitude_m, 10.0, 1e-6);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}