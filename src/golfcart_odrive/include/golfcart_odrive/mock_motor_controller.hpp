#pragma once

#include "golfcart_odrive/motor_controller.hpp"

#include <mutex>
#include <string>

namespace golfcart
{

// Mock MotorController for hardware-free testing.
// Simulates commanded velocities with a simple first-order lag and
// integrates position. Can be forced into a fault state for testing.
class MockMotorController : public MotorController
{
public:
  MockMotorController(double time_constant_s = 0.1);

  void command_velocity(double left_mps, double right_mps) override;
  void get_velocity(double & left_mps, double & right_mps) const override;
  void get_position(double & left_m, double & right_m) const override;
  std::string get_state() const override;
  std::string get_fault() const override;
  void stop() override;

  // Advance the simulation by dt seconds (call from a timer).
  void update(double dt);

  // Test hooks.
  void set_fault(const std::string & fault);
  void clear_fault();

private:
  mutable std::mutex mutex_;
  double time_constant_s_;
  double cmd_left_ = 0.0;
  double cmd_right_ = 0.0;
  double vel_left_ = 0.0;
  double vel_right_ = 0.0;
  double pos_left_ = 0.0;
  double pos_right_ = 0.0;
  std::string fault_;
};

}  // namespace golfcart
