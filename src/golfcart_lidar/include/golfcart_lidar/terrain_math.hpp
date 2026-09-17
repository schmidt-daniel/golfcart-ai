#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace golfcart
{

// Terrain hazard types (mirror golfcart_msgs/TerrainHazard).
enum TerrainHazardType : uint8_t
{
  TERRAIN_NONE = 0,
  TERRAIN_DROP_OFF = 1,
  TERRAIN_ROUGH_GROUND = 2,
  TERRAIN_OBSTRUCTION = 3,
  TERRAIN_NO_RETURN = 4,
  TERRAIN_BUNKER = 5,
};

struct TerrainAssessment
{
  uint8_t type = TERRAIN_NONE;
  double confidence = 0.0;
  double coverage = 0.0;
  double nearest_range_m = 0.0;
  bool candidate = false;
};

// Expected flat-ground range for a beam at angle `angle` (rad, 0 = forward)
// from a LiDAR mounted at height `mount_height_m` tilted down by `tilt_rad`.
// The beam's depression angle below horizontal is (tilt_rad + angle).
inline double expected_ground_range(
  double angle, double mount_height_m, double tilt_rad)
{
  const double depression = tilt_rad + angle;
  if (depression <= 0.0) {
    return std::numeric_limits<double>::infinity();
  }
  return mount_height_m / std::sin(depression);
}

// Classify a tilted scan into a terrain hazard using a ground-plane residual
// model plus statistical roughness. `ranges` are the valid forward-sector
// ranges (m); `angles` are the corresponding beam angles (rad, 0 = forward).
inline TerrainAssessment assess_terrain(
  const std::vector<double> & ranges,
  const std::vector<double> & angles,
  double mount_height_m,
  double tilt_rad,
  double drop_off_residual_m,
  double obstacle_residual_m,
  double min_valid_fraction,
  double hazard_fraction,
  double roughness_threshold_m)
{
  TerrainAssessment assessment;
  if (ranges.empty() || ranges.size() != angles.size()) {
    return assessment;
  }

  std::vector<double> residuals;
  residuals.reserve(ranges.size());
  size_t drop_count = 0;
  size_t obstacle_count = 0;
  double nearest = std::numeric_limits<double>::infinity();

  for (size_t i = 0; i < ranges.size(); ++i) {
    const double expected = expected_ground_range(angles[i], mount_height_m, tilt_rad);
    if (!std::isfinite(expected)) {
      continue;
    }
    const double residual = ranges[i] - expected;
    residuals.push_back(residual);
    nearest = std::min(nearest, ranges[i]);
    if (residual >= drop_off_residual_m) {
      ++drop_count;
    }
    if (residual <= -obstacle_residual_m) {
      ++obstacle_count;
    }
  }

  assessment.nearest_range_m = std::isfinite(nearest) ? nearest : 0.0;
  if (residuals.empty()) {
    return assessment;
  }
  assessment.coverage = static_cast<double>(residuals.size()) / ranges.size();

  const double drop_fraction = static_cast<double>(drop_count) / residuals.size();
  const double obstacle_fraction = static_cast<double>(obstacle_count) / residuals.size();

  if (assessment.coverage < min_valid_fraction) {
    assessment.type = TERRAIN_NO_RETURN;
    assessment.confidence = 1.0 - assessment.coverage;
    assessment.candidate = true;
    return assessment;
  }

  // Bunker: a depression (drop) followed by a rise (obstacle) in the same scan.
  if (drop_fraction >= hazard_fraction && obstacle_fraction >= hazard_fraction) {
    assessment.type = TERRAIN_BUNKER;
    assessment.confidence = std::min(1.0, drop_fraction + obstacle_fraction);
    assessment.candidate = true;
    return assessment;
  }
  if (drop_fraction >= hazard_fraction) {
    assessment.type = TERRAIN_DROP_OFF;
    assessment.confidence = drop_fraction;
    assessment.candidate = true;
    return assessment;
  }
  if (obstacle_fraction >= hazard_fraction) {
    assessment.type = TERRAIN_OBSTRUCTION;
    assessment.confidence = obstacle_fraction;
    assessment.candidate = true;
    return assessment;
  }

  // Statistical roughness: high variance / scattered returns => high grass.
  double mean = 0.0;
  for (const double residual : residuals) {
    mean += residual;
  }
  mean /= residuals.size();
  double variance = 0.0;
  for (const double residual : residuals) {
    const double delta = residual - mean;
    variance += delta * delta;
  }
  const double roughness = std::sqrt(variance / residuals.size());
  if (roughness >= roughness_threshold_m) {
    assessment.type = TERRAIN_ROUGH_GROUND;
    assessment.confidence = std::min(1.0, roughness / roughness_threshold_m);
    assessment.candidate = true;
  }
  return assessment;
}

}  // namespace golfcart