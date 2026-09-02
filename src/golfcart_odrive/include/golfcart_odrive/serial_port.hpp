#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace golfcart
{

// Minimal POSIX serial port wrapper for the ODrive USB/UART connection.
class SerialPort
{
public:
  SerialPort() = default;
  ~SerialPort();

  SerialPort(const SerialPort &) = delete;
  SerialPort & operator=(const SerialPort &) = delete;

  // Open the device (e.g. /dev/ttyUSB0). baud_rate is ignored for USB CDC
  // virtual serial ports but used for UART. Throws on failure.
  void open(const std::string & device, int baud_rate = 115200);

  void close();

  bool is_open() const { return fd_ >= 0; }

  // Write all bytes. Throws on failure.
  void write(const std::vector<uint8_t> & data);

  // Read up to max_bytes, blocking until at least one byte is available.
  // Returns the bytes read. Throws on failure.
  std::vector<uint8_t> read(size_t max_bytes);

  // Read exactly n bytes, blocking until all are received or timeout_ms elapses.
  // Returns false on timeout.
  bool read_exact(std::vector<uint8_t> & out, size_t n, int timeout_ms);

private:
  int fd_ = -1;
};

}  // namespace golfcart