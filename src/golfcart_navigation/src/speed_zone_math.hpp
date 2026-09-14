// Pure math for the Speed Zones feature. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Provides:
//  - ray-casting point-in-polygon (concave-safe, any vertex order)
//
// All geometry is in the map frame (meters, x = east, y = north).

#ifndef GOLFCART_NAVIGATION__SPEED_ZONE_MATH_HPP_
#define GOLFCART_NAVIGATION__SPEED_ZONE_MATH_HPP_

#include <cmath>
#include <vector>

namespace golfcart
{

// A 2D point in the map frame (meters).
struct MapPoint
{
  double x;
  double y;
};

// Ray-casting point-in-polygon test. Handles concave polygons and any vertex
// order (CW or CCW). Points lying exactly on an edge or vertex are treated as
// inside. poly must have >= 3 vertices.
inline bool point_in_polygon(const MapPoint & p, const std::vector<MapPoint> & poly)
{
  if (poly.size() < 3) {
    return false;
  }
  const size_t n = poly.size();

  // On-boundary check: if p lies exactly on any edge, treat as inside.
  for (size_t i = 0; i < n; ++i) {
    const MapPoint & a = poly[i];
    const MapPoint & b = poly[(i + 1) % n];
    const double cross = (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    const double dot = (p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y);
    const double len2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
    if (std::abs(cross) < 1e-9 && dot >= 0.0 && dot <= len2) {
      return true;  // on the segment
    }
  }

  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const MapPoint & a = poly[i];
    const MapPoint & b = poly[j];
    // Standard even-odd ray cast (horizontal ray to +x).
    const bool crosses = ((a.y > p.y) != (b.y > p.y)) &&
      (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
    if (crosses) {
      inside = !inside;
    }
  }
  return inside;
}

}  // namespace golfcart

#endif  // GOLFCART_NAVIGATION__SPEED_ZONE_MATH_HPP_