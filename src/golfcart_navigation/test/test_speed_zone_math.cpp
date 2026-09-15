#include <cmath>

#include <gtest/gtest.h>

#include "speed_zone_math.hpp"

using golfcart::MapPoint;
using golfcart::point_in_polygon;

// A unit square [0,1]x[0,1].
static std::vector<MapPoint> square()
{
  return {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
}

TEST(SpeedZoneMath, Inside)
{
  EXPECT_TRUE(point_in_polygon({0.5, 0.5}, square()));
}

TEST(SpeedZoneMath, Outside)
{
  EXPECT_FALSE(point_in_polygon({1.5, 0.5}, square()));
  EXPECT_FALSE(point_in_polygon({-0.5, 0.5}, square()));
}

TEST(SpeedZoneMath, OnBoundaryIsInside)
{
  EXPECT_TRUE(point_in_polygon({0.5, 0.0}, square()));
  EXPECT_TRUE(point_in_polygon({1.0, 0.5}, square()));
}

TEST(SpeedZoneMath, Concave)
{
  // A concave "L" shape.
  std::vector<MapPoint> l = {
    {0.0, 0.0}, {2.0, 0.0}, {2.0, 1.0}, {1.0, 1.0},
    {1.0, 2.0}, {0.0, 2.0},
  };
  EXPECT_TRUE(point_in_polygon({0.5, 0.5}, l));    // in the foot
  EXPECT_TRUE(point_in_polygon({0.5, 1.5}, l));    // in the stem
  EXPECT_FALSE(point_in_polygon({1.5, 1.5}, l));   // in the notch
}

TEST(SpeedZoneMath, TooFewVertices)
{
  EXPECT_FALSE(point_in_polygon({0.5, 0.5}, {{0.0, 0.0}, {1.0, 0.0}}));
}

// ---- distance_to_polygon ----

TEST(SpeedZoneMath, DistanceInsideIsZero)
{
  EXPECT_DOUBLE_EQ(golfcart::distance_to_polygon({0.5, 0.5}, square()), 0.0);
}

TEST(SpeedZoneMath, DistanceToEdge)
{
  // Point 1 m to the right of the square's right edge (x=1).
  EXPECT_NEAR(golfcart::distance_to_polygon({2.0, 0.5}, square()), 1.0, 1e-9);
  // Point 0.5 m above the top edge (y=1).
  EXPECT_NEAR(golfcart::distance_to_polygon({0.5, 1.5}, square()), 0.5, 1e-9);
}

TEST(SpeedZoneMath, DistanceToCorner)
{
  // Point diagonally off the top-right corner (1,1): distance = sqrt(2).
  EXPECT_NEAR(golfcart::distance_to_polygon({2.0, 2.0}, square()),
              std::sqrt(2.0), 1e-9);
}

TEST(SpeedZoneMath, DistanceOnEdgeIsZero)
{
  EXPECT_NEAR(golfcart::distance_to_polygon({0.5, 0.0}, square()), 0.0, 1e-9);
}