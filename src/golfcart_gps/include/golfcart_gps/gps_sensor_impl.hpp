#pragma once

#include "golfcart_gps/gps_sensor.hpp"

#include <string>

namespace golfcart
{

// GPS-backed GpsSensor.
//
// NOTE: This is a scaffold. The actual GPS module (e.g. NMEA over UART/USB)
// parsing must be implemented before use on hardware.
class GpsSensorImpl : public GpsSensor
{
public:
  explicit GpsSensorImpl(const std::string & device = "/dev/ttyUSB0");

  GpsSample read() override;

private:
  std::string device_;
};

}  // namespace golfcart