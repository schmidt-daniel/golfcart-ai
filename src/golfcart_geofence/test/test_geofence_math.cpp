#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "geofence_math.hpp"

using golfcart::Point2;
using golfcart::point_in_polygon;
using golfcart::distance_to_boundary;
using golfcart::latlon_to_enu;

// A unit square [0,1]x[0,1].
static std::vector<Point2> square()
{
  return {{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}, {0.0, 1.0}};
}

TEST(GeofenceMath, PointInsideSquare)
{
  const auto poly = square();
  EXPECT_TRUE(point_in_polygon({0.5, 0.5}, poly));
  EXPECT_TRUE(point_in_polygon({0.1, 0.9}, poly));
}

TEST(GeofenceMath, PointOutsideSquare)
{
  const auto poly = square();
  EXPECT_FALSE(point_in_polygon({1.5, 0.5}, poly));
  EXPECT_FALSE(point_in_polygon({-0.1, 0.5}, poly));
  EXPECT_FALSE(point_in_polygon({0.5, 1.5}, poly));
}

TEST(GeofenceMath, PointOnBoundaryIsInside)
{
  const auto poly = square();
  EXPECT_TRUE(point_in_polygon({0.0, 0.5}, poly));   // left edge
  EXPECT_TRUE(point_in_polygon({1.0, 1.0}, poly));   // corner
}

TEST(GeofenceMath, ConcavePolygon)
{
  // A concave "L" / arrow shape.
  const std::vector<Point2> poly = {
    {0.0, 0.0}, {3.0, 0.0}, {3.0, 3.0}, {2.0, 3.0}, {2.0, 1.0}, {1.0, 1.0},
    {1.0, 3.0}, {0.0, 3.0}};
  EXPECT_TRUE(point_in_polygon({0.5, 0.5}, poly));    // inside lower-left
  EXPECT_TRUE(point_in_polygon({2.5, 2.5}, poly));    // inside upper-right
  EXPECT_FALSE(point_in_polygon({1.5, 2.0}, poly));   // in the notch (outside)
  EXPECT_TRUE(point_in_polygon({1.5, 0.5}, poly));    // inside lower band
}

TEST(GeofenceMath, CollinearEdge)
{
  // A triangle with a collinear vertex on one edge.
  const std::vector<Point2> poly = {
    {0.0, 0.0}, {2.0, 0.0}, {1.0, 0.0}, {1.0, 2.0}};
  EXPECT_TRUE(point_in_polygon({1.0, 1.0}, poly));
  EXPECT_FALSE(point_in_polygon({3.0, 1.0}, poly));
}

TEST(GeofenceMath, DegeneratePolygon)
{
  const std::vector<Point2> poly = {{0.0, 0.0}, {1.0, 1.0}};  // only 2 vertices
  EXPECT_FALSE(point_in_polygon({0.5, 0.5}, poly));
}

TEST(GeofenceMath, DistanceToBoundary)
{
  const auto poly = square();
  // Center is 0.5 m from every edge.
  EXPECT_NEAR(distance_to_boundary({0.5, 0.5}, poly), 0.5, 1e-6);
  // Just outside the right edge.
  EXPECT_NEAR(distance_to_boundary({1.2, 0.5}, poly), 0.2, 1e-6);
  // Corner distance (diagonal).
  EXPECT_NEAR(distance_to_boundary({1.2, 1.2}, poly), std::sqrt(0.08), 1e-6);
}

TEST(GeofenceMath, LatLonToEnu)
{
  // At the origin, offset is zero.
  const auto p0 = latlon_to_enu(48.0, 11.0, 48.0, 11.0);
  EXPECT_NEAR(p0.x, 0.0, 1e-6);
  EXPECT_NEAR(p0.y, 0.0, 1e-6);

  // 1e-4 deg north ~ 11.1 m; 1e-4 deg east at lat 48 ~ 7.4 m.
  const auto p1 = latlon_to_enu(48.0001, 11.0001, 48.0, 11.0);
  EXPECT_NEAR(p1.y, 11.11, 0.1);
  EXPECT_NEAR(p1.x, 7.44, 0.1);
}

int main(int argc, char ** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}