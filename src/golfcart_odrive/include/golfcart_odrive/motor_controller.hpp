#pragma once

#include <string>

namespace golfcart
{

// Generic motor-controller abstraction.
// High-level code must not depend on ODrive-specific APIs.
class MotorController
{
public:
  virtual ~MotorController() = default;

  // Command desired wheel velocities in m/s (left, right).
  virtual void command_velocity(double left_mps, double right_mps) = 0;

  // Measured wheel velocities in m/s.
  virtual void get_velocity(double & left_mps, double & right_mps) const = 0;

  // Measured wheel positions in meters.
  virtual void get_position(double & left_m, double & right_m) const = 0;

  // Current state string (e.g. "idle", "running", "fault").
  virtual std::string get_state() const = 0;

  // Current fault string (empty if no fault).
  virtual std::string get_fault() const = 0;

  // Stop the motors.
  virtual void stop() = 0;
};

}  // namespace golfcart
