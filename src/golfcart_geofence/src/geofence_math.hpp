// Pure math for the Geofence feature. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Provides:
//  - lat/lon -> local ENU meters (equirectangular, same as summon_math)
//  - ray-casting point-in-polygon (concave-safe)
//  - distance from a point to the boundary polygon (min over segments)
//
// All geometry is done in a local ENU frame (x = east, y = north) derived
// from lat/lon. The map frame is a rotation of this ENU frame by the map
// origin rotation; callers convert as needed.

#ifndef GOLFCART_GEOFENCE__GEOFENCE_MATH_HPP_
#define GOLFCART_GEOFENCE__GEOFENCE_MATH_HPP_

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace golfcart
{

// A 2D point in the local ENU frame (x = east, y = north), meters.
struct Point2
{
  double x;
  double y;
};

// Earth radius (m), used for the equirectangular approximation.
constexpr double kEarthRadiusM = 6371000.0;

// Convert a lat/lon offset (relative to origin) into ENU meters.
inline Point2 latlon_to_enu(double lat_deg, double lon_deg,
                            double origin_lat_deg, double origin_lon_deg)
{
  const double dlat = (lat_deg - origin_lat_deg) * M_PI / 180.0;
  const double dlon = (lon_deg - origin_lon_deg) * M_PI / 180.0;
  const double cos_lat = std::cos(origin_lat_deg * M_PI / 180.0);
  return {kEarthRadiusM * dlon * cos_lat, kEarthRadiusM * dlat};
}

// Ray-casting point-in-polygon test. Handles concave polygons and any vertex
// order (CW or CCW). Points lying exactly on an edge or vertex are treated as
// inside (a cart exactly on the boundary line is still "inside").
// poly must have >= 3 vertices.
inline bool point_in_polygon(const Point2 & p, const std::vector<Point2> & poly)
{
  if (poly.size() < 3) {
    return false;
  }
  const size_t n = poly.size();

  // On-boundary check: if p lies exactly on any edge, treat as inside.
  for (size_t i = 0; i < n; ++i) {
    const Point2 & a = poly[i];
    const Point2 & b = poly[(i + 1) % n];
    const double cross = (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
    const double dot = (p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y);
    const double len2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
    if (std::abs(cross) < 1e-9 && dot >= 0.0 && dot <= len2) {
      return true;  // on the segment
    }
  }

  bool inside = false;
  for (size_t i = 0, j = n - 1; i < n; j = i++) {
    const Point2 & a = poly[i];
    const Point2 & b = poly[j];
    // Standard even-odd ray cast (horizontal ray to +x).
    const bool crosses = ((a.y > p.y) != (b.y > p.y)) &&
      (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x);
    if (crosses) {
      inside = !inside;
    }
  }
  return inside;
}

// Squared distance from point p to segment [a, b].
inline double dist2_point_to_segment(const Point2 & p, const Point2 & a, const Point2 & b)
{
  const double dx = b.x - a.x;
  const double dy = b.y - a.y;
  const double len2 = dx * dx + dy * dy;
  if (len2 < 1e-12) {
    const double ex = p.x - a.x;
    const double ey = p.y - a.y;
    return ex * ex + ey * ey;
  }
  // Project p onto the segment, clamped to [0,1].
  double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
  t = std::clamp(t, 0.0, 1.0);
  const double px = a.x + t * dx;
  const double py = a.y + t * dy;
  const double ex = p.x - px;
  const double ey = p.y - py;
  return ex * ex + ey * ey;
}

// Minimum distance from point p to the boundary polygon (min over all edges).
// Returns a non-negative distance in meters.
inline double distance_to_boundary(const Point2 & p, const std::vector<Point2> & poly)
{
  if (poly.size() < 2) {
    return std::numeric_limits<double>::infinity();
  }
  double best = std::numeric_limits<double>::infinity();
  const size_t n = poly.size();
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    best = std::min(best, std::sqrt(dist2_point_to_segment(p, poly[i], poly[j])));
  }
  return best;
}

}  // namespace golfcart

#endif  // GOLFCART_GEOFENCE__GEOFENCE_MATH_HPP_