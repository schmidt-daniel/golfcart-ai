#include "golfcart_imu/imu_sensor_impl.hpp"

#include <stdexcept>

namespace golfcart
{

ImuSensorImpl::ImuSensorImpl(const std::string & device)
  : device_(device)
{
  // TODO: Open and configure the IMU over I2C (e.g. MPU-6050 / BNO055).
  throw std::runtime_error(
    "ImuSensorImpl: hardware driver not yet implemented. "
    "Use MockImuSensor for testing.");
}

ImuSample ImuSensorImpl::read()
{
  // TODO: implement I2C read.
  return ImuSample{};
}

}  // namespace golfcart