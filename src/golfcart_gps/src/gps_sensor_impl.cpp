#include "golfcart_gps/gps_sensor_impl.hpp"

#include <stdexcept>

namespace golfcart
{

GpsSensorImpl::GpsSensorImpl(const std::string & device)
  : device_(device)
{
  // TODO: Open and configure the GPS module (e.g. NMEA over UART/USB).
  throw std::runtime_error(
    "GpsSensorImpl: hardware driver not yet implemented. "
    "Use MockGpsSensor for testing.");
}

GpsSample GpsSensorImpl::read()
{
  // TODO: implement NMEA parsing.
  return GpsSample{};
}

}  // namespace golfcart