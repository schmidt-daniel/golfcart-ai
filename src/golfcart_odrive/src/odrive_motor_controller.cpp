#include "golfcart_odrive/odrive_motor_controller.hpp"

#include "golfcart_odrive/odrive_protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace golfcart
{

namespace
{

// ODrive axis states (ODrive 0.5.6).
constexpr int AXIS_STATE_IDLE = 1;
constexpr int AXIS_STATE_CLOSED_LOOP_CONTROL = 8;

// Parse a float from a JSON response of the form "path": value
double parse_json_number(const std::string & response, const std::string & key)
{
  const auto pos = response.find(key);
  if (pos == std::string::npos) {
    throw std::runtime_error("ODrive: key '" + key + "' not found in response: " + response);
  }
  const auto colon = response.find(':', pos);
  if (colon == std::string::npos) {
    throw std::runtime_error("ODrive: malformed response: " + response);
  }
  return std::stod(response.substr(colon + 1));
}

}  // namespace

ODriveMotorController::ODriveMotorController(const std::string & device)
  : device_(device)
{
  serial_.open(device_);
}

std::string ODriveMotorController::send_json(const std::string & json) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto frame = build_json_command(json);
  serial_.write(frame);
  return read_response();
}

std::string ODriveMotorController::read_response() const
{
  std::string line;
  std::vector<uint8_t> buf;
  // Read until newline or timeout.
  while (true) {
    if (!serial_.read_exact(buf, 1, 500)) {
      throw std::runtime_error("ODrive: response timeout");
    }
    const char c = static_cast<char>(buf[0]);
    if (c == '\n') {
      break;
    }
    line.push_back(c);
  }
  return line;
}

void ODriveMotorController::command_velocity(double left_mps, double right_mps)
{
  // Set both axes to closed-loop control and command velocity.
  // axis0 = left, axis1 = right.
  char cmd[256];
  std::snprintf(
    cmd, sizeof(cmd),
    "{\"axis0.requested_state\":8,\"axis1.requested_state\":8,"
    "\"axis0.controller.input_vel\":%.6f,\"axis1.controller.input_vel\":%.6f}",
    left_mps, right_mps);
  send_json(cmd);
}

void ODriveMotorController::get_velocity(double & left_mps, double & right_mps) const
{
  const std::string resp = send_json(
    "{\"axis0.encoder.vel_estimate\":0,\"axis1.encoder.vel_estimate\":0}");
  left_mps = parse_json_number(resp, "axis0.encoder.vel_estimate");
  right_mps = parse_json_number(resp, "axis1.encoder.vel_estimate");
}

void ODriveMotorController::get_position(double & left_m, double & right_m) const
{
  const std::string resp = send_json(
    "{\"axis0.encoder.pos_estimate\":0,\"axis1.encoder.pos_estimate\":0}");
  left_m = parse_json_number(resp, "axis0.encoder.pos_estimate");
  right_m = parse_json_number(resp, "axis1.encoder.pos_estimate");
}

std::string ODriveMotorController::get_state() const
{
  const std::string resp = send_json(
    "{\"axis0.current_state\":0,\"axis1.current_state\":0}");
  const int s0 = static_cast<int>(parse_json_number(resp, "axis0.current_state"));
  const int s1 = static_cast<int>(parse_json_number(resp, "axis1.current_state"));
  if (s0 == AXIS_STATE_CLOSED_LOOP_CONTROL || s1 == AXIS_STATE_CLOSED_LOOP_CONTROL) {
    return "running";
  }
  if (s0 == AXIS_STATE_IDLE || s1 == AXIS_STATE_IDLE) {
    return "idle";
  }
  return "unknown";
}

std::string ODriveMotorController::get_fault() const
{
  const std::string resp = send_json(
    "{\"axis0.error\":0,\"axis1.error\":0}");
  const int e0 = static_cast<int>(parse_json_number(resp, "axis0.error"));
  const int e1 = static_cast<int>(parse_json_number(resp, "axis1.error"));
  if (e0 != 0 || e1 != 0) {
    return "axis_error_" + std::to_string(e0) + "_" + std::to_string(e1);
  }
  return "";
}

void ODriveMotorController::stop()
{
  // Return both axes to idle.
  send_json("{\"axis0.requested_state\":1,\"axis1.requested_state\":1}");
}

}  // namespace golfcart
