#include "golfcart_odrive/mock_motor_controller.hpp"

#include <algorithm>
#include <cmath>

namespace golfcart
{

MockMotorController::MockMotorController(double time_constant_s)
  : time_constant_s_(time_constant_s)
{
}

void MockMotorController::command_velocity(double left_mps, double right_mps)
{
  std::lock_guard<std::mutex> lock(mutex_);
  cmd_left_ = left_mps;
  cmd_right_ = right_mps;
}

void MockMotorController::get_velocity(double & left_mps, double & right_mps) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  left_mps = vel_left_;
  right_mps = vel_right_;
}

void MockMotorController::get_position(double & left_m, double & right_m) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  left_m = pos_left_;
  right_m = pos_right_;
}

std::string MockMotorController::get_state() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!fault_.empty()) {
    return "fault";
  }
  return (std::abs(cmd_left_) > 1e-6 || std::abs(cmd_right_) > 1e-6) ? "running" : "idle";
}

std::string MockMotorController::get_fault() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return fault_;
}

void MockMotorController::stop()
{
  std::lock_guard<std::mutex> lock(mutex_);
  cmd_left_ = 0.0;
  cmd_right_ = 0.0;
}

void MockMotorController::update(double dt)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const double alpha = dt / (time_constant_s_ + dt);
  vel_left_ += alpha * (cmd_left_ - vel_left_);
  vel_right_ += alpha * (cmd_right_ - vel_right_);
  pos_left_ += vel_left_ * dt;
  pos_right_ += vel_right_ * dt;
}

void MockMotorController::set_fault(const std::string & fault)
{
  std::lock_guard<std::mutex> lock(mutex_);
  fault_ = fault;
}

void MockMotorController::clear_fault()
{
  std::lock_guard<std::mutex> lock(mutex_);
  fault_.clear();
}

}  // namespace golfcart
