// Pure math for Trip Logging. No ROS dependencies so it can be unit-tested
// directly with gtest.
//
// Tracks a round's distance, energy, and duration, and computes the summary.

#ifndef GOLFCART_NAVIGATION__TRIP_MATH_HPP_
#define GOLFCART_NAVIGATION__TRIP_MATH_HPP_

namespace golfcart
{

// Accumulates round statistics (distance, energy, duration).
class TripTracker
{
public:
  // Start a new round at time `start_s`.
  void start(double start_s)
  {
    active_ = true;
    start_s_ = start_s;
    distance_m_ = 0.0;
    energy_wh_ = 0.0;
  }

  // Accumulate a distance delta (m) and energy delta (Wh).
  void accumulate(double dist_m, double energy_wh)
  {
    if (!active_) {
      return;
    }
    if (dist_m > 0.0) {
      distance_m_ += dist_m;
    }
    if (energy_wh > 0.0) {
      energy_wh_ += energy_wh;
    }
  }

  // End the round at time `end_s`. Returns the duration (s).
  double end(double end_s)
  {
    active_ = false;
    return end_s - start_s_;
  }

  bool active() const { return active_; }
  double distance_m() const { return distance_m_; }
  double energy_wh() const { return energy_wh_; }

  // Average speed (m/s) over the round, given the duration.
  double avg_speed_mps(double duration_s) const
  {
    if (duration_s <= 0.0) {
      return 0.0;
    }
    return distance_m_ / duration_s;
  }

private:
  bool active_ = false;
  double start_s_ = 0.0;
  double distance_m_ = 0.0;
  double energy_wh_ = 0.0;
};

}  // namespace golfcart

#endif  // GOLFCART_NAVIGATION__TRIP_MATH_HPP_