#pragma once

#include "golfcart_odrive/motor_controller.hpp"
#include "golfcart_odrive/serial_port.hpp"

#include <mutex>
#include <string>

namespace golfcart
{

// ODrive-backed MotorController using the ODrive 0.5.6 native protocol over
// USB serial. Uses the JSON command interface for simplicity and robustness.
//
// The motors/encoders are assumed to be already tuned and configured on the
// ODrive (axis0 = left, axis1 = right). This driver does not modify motor
// configuration.
class ODriveMotorController : public MotorController
{
public:
  explicit ODriveMotorController(const std::string & device = "/dev/ttyUSB0");

  void command_velocity(double left_mps, double right_mps) override;
  void get_velocity(double & left_mps, double & right_mps) const override;
  void get_position(double & left_m, double & right_m) const override;
  std::string get_state() const override;
  std::string get_fault() const override;
  void stop() override;

private:
  // Send a JSON command and read the response. Returns the response string.
  std::string send_json(const std::string & json) const;

  // Read a single JSON response line (terminated by '\n').
  std::string read_response() const;

  mutable std::mutex mutex_;
  mutable SerialPort serial_;
  std::string device_;
};

}  // namespace golfcart
