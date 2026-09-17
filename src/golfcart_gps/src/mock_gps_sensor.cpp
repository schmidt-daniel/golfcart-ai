#include "golfcart_gps/mock_gps_sensor.hpp"

namespace golfcart
{

MockGpsSensor::MockGpsSensor(double latitude_deg, double longitude_deg, double altitude_m)
  : latitude_deg_(latitude_deg), longitude_deg_(longitude_deg), altitude_m_(altitude_m)
{
}

GpsSample MockGpsSensor::read()
{
  std::lock_guard<std::mutex> lock(mutex_);
  GpsSample s;
  s.latitude_deg = latitude_deg_;
  s.longitude_deg = longitude_deg_;
  s.altitude_m = altitude_m_;
  s.speed_mps = 0.0;
  s.heading_rad = 0.0;
  s.valid = true;
  s.fix_type = 3;
  s.satellites = 8;
  s.hdop = 1.0;
  s.vdop = 1.5;
  s.pdop = 1.8;
  return s;
}

void MockGpsSensor::set_position(double latitude_deg, double longitude_deg)
{
  std::lock_guard<std::mutex> lock(mutex_);
  latitude_deg_ = latitude_deg;
  longitude_deg_ = longitude_deg;
}

}  // namespace golfcart