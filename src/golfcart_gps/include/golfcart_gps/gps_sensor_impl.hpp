#pragma once

#include "golfcart_gps/gps_sensor.hpp"

#include <chrono>
#include <string>

namespace golfcart
{

// GPS-backed GpsSensor using gpsd's localhost JSON protocol.
class GpsSensorImpl : public GpsSensor
{
public:
  explicit GpsSensorImpl(
    const std::string & device = "/dev/ttyUSB0",
    const std::string & host = "127.0.0.1",
    int port = 2947);

  ~GpsSensorImpl() override;

  GpsSample read() override;

private:
  bool connect_gpsd();
  void close_connection();
  void read_available();
  void process_line(const std::string & line);

  std::string device_;
  std::string host_;
  int port_ = 2947;
  int socket_fd_ = -1;
  std::string receive_buffer_;
  GpsSample latest_;
  std::chrono::steady_clock::time_point last_fix_time_;
};

}  // namespace golfcart