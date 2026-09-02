#include "golfcart_imu/mock_imu_sensor.hpp"

#include <cmath>

namespace golfcart
{

MockImuSensor::MockImuSensor(double roll_rad, double pitch_rad)
  : roll_rad_(roll_rad), pitch_rad_(pitch_rad)
{
}

ImuSample MockImuSensor::read()
{
  std::lock_guard<std::mutex> lock(mutex_);
  ImuSample s;
  // Stationary IMU: the accelerometer measures the reaction to gravity
  // (level -> az = +g). Chosen so that the imu_node formulas
  //   roll  = atan2(ay, az)
  //   pitch = atan2(-ax, sqrt(ay^2 + az^2))
  // return the true roll/pitch:
  //   ax = -g * sin(pitch)
  //   ay =  g * sin(roll) * cos(pitch)
  //   az =  g * cos(roll) * cos(pitch)
  const double g = 9.81;
  const double cr = std::cos(roll_rad_);
  const double sr = std::sin(roll_rad_);
  const double cp = std::cos(pitch_rad_);
  const double sp = std::sin(pitch_rad_);

  s.accel_x = -g * sp;
  s.accel_y = g * sr * cp;
  s.accel_z = g * cr * cp;
  s.gyro_x = 0.0;
  s.gyro_y = 0.0;
  s.gyro_z = 0.0;
  s.valid = true;
  return s;
}

void MockImuSensor::set_tilt(double roll_rad, double pitch_rad)
{
  std::lock_guard<std::mutex> lock(mutex_);
  roll_rad_ = roll_rad;
  pitch_rad_ = pitch_rad;
}

}  // namespace golfcart