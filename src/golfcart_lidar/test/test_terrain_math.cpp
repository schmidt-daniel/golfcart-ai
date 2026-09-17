#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "golfcart_lidar/terrain_math.hpp"

using golfcart::assess_terrain;
using golfcart::expected_ground_range;
using golfcart::TERRAIN_BUNKER;
using golfcart::TERRAIN_DROP_OFF;
using golfcart::TERRAIN_NONE;
using golfcart::TERRAIN_OBSTRUCTION;
using golfcart::TERRAIN_ROUGH_GROUND;

namespace
{

constexpr double kMountHeight = 0.65;
constexpr double kTilt = 0.436;

std::vector<double> flat_ranges(size_t count, double angle_step)
{
  std::vector<double> ranges;
  for (size_t i = 0; i < count; ++i) {
    const double angle = (static_cast<double>(i) - count / 2.0) * angle_step;
    ranges.push_back(expected_ground_range(angle, kMountHeight, kTilt));
  }
  return ranges;
}

std::vector<double> angles_for(size_t count, double angle_step)
{
  std::vector<double> angles;
  for (size_t i = 0; i < count; ++i) {
    angles.push_back((static_cast<double>(i) - count / 2.0) * angle_step);
  }
  return angles;
}

}  // namespace

TEST(TerrainMath, ExpectedGroundRange)
{
  // A steeper depression angle gives a shorter range.
  const double near_angle = expected_ground_range(0.0, kMountHeight, kTilt);
  const double far_angle = expected_ground_range(-0.3, kMountHeight, kTilt);
  EXPECT_GT(near_angle, 0.0);
  EXPECT_GT(far_angle, near_angle);
}

TEST(TerrainMath, FlatGroundNoHazard)
{
  const size_t count = 40;
  const double step = 0.02;
  const auto ranges = flat_ranges(count, step);
  const auto angles = angles_for(count, step);
  const auto result = assess_terrain(
    ranges, angles, kMountHeight, kTilt, 0.5, 0.5, 0.5, 0.25, 0.35);
  EXPECT_FALSE(result.candidate);
  EXPECT_EQ(result.type, TERRAIN_NONE);
}

TEST(TerrainMath, DropOffDetected)
{
  const size_t count = 40;
  const double step = 0.02;
  auto ranges = flat_ranges(count, step);
  const auto angles = angles_for(count, step);
  // Push the far half out to simulate a drop-off.
  for (size_t i = count / 2; i < count; ++i) {
    ranges[i] += 2.0;
  }
  const auto result = assess_terrain(
    ranges, angles, kMountHeight, kTilt, 0.5, 0.5, 0.5, 0.25, 0.35);
  EXPECT_TRUE(result.candidate);
  EXPECT_EQ(result.type, TERRAIN_DROP_OFF);
}

TEST(TerrainMath, ObstructionDetected)
{
  const size_t count = 40;
  const double step = 0.02;
  auto ranges = flat_ranges(count, step);
  const auto angles = angles_for(count, step);
  // Pull the near half in to simulate a raised obstacle.
  for (size_t i = 0; i < count / 2; ++i) {
    ranges[i] -= 1.0;
  }
  const auto result = assess_terrain(
    ranges, angles, kMountHeight, kTilt, 0.5, 0.5, 0.5, 0.25, 0.35);
  EXPECT_TRUE(result.candidate);
  EXPECT_EQ(result.type, TERRAIN_OBSTRUCTION);
}

TEST(TerrainMath, BunkerDetected)
{
  const size_t count = 40;
  const double step = 0.02;
  auto ranges = flat_ranges(count, step);
  const auto angles = angles_for(count, step);
  // Depression in the middle, rise at the far end => bunker.
  for (size_t i = count / 4; i < 3 * count / 4; ++i) {
    ranges[i] += 2.0;
  }
  for (size_t i = 3 * count / 4; i < count; ++i) {
    ranges[i] -= 1.0;
  }
  const auto result = assess_terrain(
    ranges, angles, kMountHeight, kTilt, 0.5, 0.5, 0.5, 0.25, 0.35);
  EXPECT_TRUE(result.candidate);
  EXPECT_EQ(result.type, TERRAIN_BUNKER);
}

TEST(TerrainMath, RoughGroundDetected)
{
  const size_t count = 40;
  const double step = 0.02;
  auto ranges = flat_ranges(count, step);
  const auto angles = angles_for(count, step);
  // Add scattered noise to simulate high grass. Keep it below the residual
  // thresholds (0.5) so it reaches the roughness branch, but above the
  // roughness threshold (0.35).
  for (size_t i = 0; i < count; ++i) {
    ranges[i] += (i % 2 == 0) ? 0.4 : -0.4;
  }
  const auto result = assess_terrain(
    ranges, angles, kMountHeight, kTilt, 0.5, 0.5, 0.5, 0.25, 0.35);
  EXPECT_TRUE(result.candidate);
  EXPECT_EQ(result.type, TERRAIN_ROUGH_GROUND);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}