#include "golfcart_lidar/lidar_sensor_impl.hpp"

#include <stdexcept>

namespace golfcart
{

LidarSensorImpl::LidarSensorImpl(const std::string & device)
  : device_(device)
{
  // TODO: Open and configure the FHL-LD19P LiDAR (serial/UART).
  throw std::runtime_error(
    "LidarSensorImpl: hardware driver not yet implemented. "
    "Use MockLidarSensor for testing.");
}

LidarScan LidarSensorImpl::read()
{
  // TODO: implement FHL-LD19P packet parsing.
  return LidarScan{};
}

}  // namespace golfcart