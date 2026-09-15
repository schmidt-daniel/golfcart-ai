// Pure math for the Battery Range Estimator. No ROS dependencies so it can be
// unit-tested directly with gtest.
//
// Provides:
//  - a slope-bucketed energy model (Wh/m per slope bucket)
//  - online EMA learning of Wh/m from measured energy + distance
//  - range estimation from remaining charge
//  - OK / CAUTION / CRITICAL warning states
//
// The model learns from measured battery current over time, so it improves
// with every round instead of relying on a fixed calibration.

#ifndef GOLFCART_NAVIGATION__RANGE_ESTIMATOR_MATH_HPP_
#define GOLFCART_NAVIGATION__RANGE_ESTIMATOR_MATH_HPP_

#include <algorithm>
#include <cmath>

namespace golfcart
{

// Slope buckets for the energy model. Bucket index = slope_deg mapped to a
// fixed set: DOWNHILL / FLAT / UPHILL.
enum SlopeBucket
{
  BUCKET_DOWNHILL = 0,
  BUCKET_FLAT = 1,
  BUCKET_UPHILL = 2,
  BUCKET_COUNT = 3,
};

// Map a slope (deg) to a bucket. Positive = uphill.
inline SlopeBucket slope_to_bucket(double slope_deg)
{
  if (slope_deg < -2.0) {
    return BUCKET_DOWNHILL;
  }
  if (slope_deg > 2.0) {
    return BUCKET_UPHILL;
  }
  return BUCKET_FLAT;
}

// The self-learning energy model.
class EnergyModel
{
public:
  // Default constructor (zeroed) so the struct can be a member; callers should
  // use the parameterized constructor.
  EnergyModel()
  : ema_alpha_(0.3)
  {
    for (int i = 0; i < BUCKET_COUNT; ++i) {
      wh_per_m_[i] = 0.02;
    }
  }

  // default_wh_per_m: initial flat-ground consumption (Wh/m).
  // ema_alpha: learning rate (0-1) for the EMA update.
  EnergyModel(double default_wh_per_m, double ema_alpha)
  : ema_alpha_(ema_alpha)
  {
    for (int i = 0; i < BUCKET_COUNT; ++i) {
      wh_per_m_[i] = default_wh_per_m;
    }
  }

  // Update the model from a measured energy delta (Wh) over a distance (m)
  // at the given slope. Uses EMA so the model adapts gradually.
  void learn(double slope_deg, double energy_wh, double distance_m)
  {
    if (distance_m <= 0.0 || energy_wh < 0.0) {
      return;
    }
    const SlopeBucket b = slope_to_bucket(slope_deg);
    const double measured = energy_wh / distance_m;
    // Clamp the measurement to a sane range so a bad sample can't produce a
    // wildly optimistic estimate.
    const double clamped = std::clamp(measured, 0.001, 0.5);
    wh_per_m_[b] = (1.0 - ema_alpha_) * wh_per_m_[b] + ema_alpha_ * clamped;
  }

  // Wh/m for a given slope.
  double wh_per_m(double slope_deg) const
  {
    return wh_per_m_[slope_to_bucket(slope_deg)];
  }

  // Current flat-ground Wh/m (for reporting).
  double flat_wh_per_m() const { return wh_per_m_[BUCKET_FLAT]; }

private:
  double wh_per_m_[BUCKET_COUNT];
  double ema_alpha_;
};

// Estimate the remaining range (m) from the usable energy (Wh) and the model.
inline double estimate_range_m(const EnergyModel & model, double usable_wh,
                               double slope_deg)
{
  const double wpm = model.wh_per_m(slope_deg);
  if (wpm <= 0.0) {
    return 0.0;
  }
  return usable_wh / wpm;
}

// Warning state given the estimated range and the distance needed.
//   OK       - range covers remaining + return + margin.
//   CAUTION  - range covers remaining + return, but not the margin.
//   CRITICAL - range does not cover remaining + return.
inline std::string range_state(double range_m, double remaining_m,
                               double return_m, double margin_m)
{
  const double needed = remaining_m + return_m;
  if (range_m >= needed + margin_m) {
    return "OK";
  }
  if (range_m >= needed) {
    return "CAUTION";
  }
  return "CRITICAL";
}

}  // namespace golfcart

#endif  // GOLFCART_NAVIGATION__RANGE_ESTIMATOR_MATH_HPP_