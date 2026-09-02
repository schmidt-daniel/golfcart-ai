#include "golfcart_odrive/serial_port.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

namespace golfcart
{

SerialPort::~SerialPort()
{
  close();
}

void SerialPort::open(const std::string & device, int baud_rate)
{
  if (is_open()) {
    close();
  }

  fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) {
    throw std::runtime_error(
      "SerialPort: failed to open " + device + ": " + std::strerror(errno));
  }

  termios tty{};
  if (tcgetattr(fd_, &tty) != 0) {
    close();
    throw std::runtime_error("SerialPort: tcgetattr failed: " + std::string(std::strerror(errno)));
  }

  cfmakeraw(&tty);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~CSTOPB;
  tty.c_cflag &= ~CRTSCTS;

  // Map baud rate. USB CDC ignores this; UART requires it.
  speed_t speed = B115200;
  switch (baud_rate) {
    case 9600: speed = B9600; break;
    case 19200: speed = B19200; break;
    case 38400: speed = B38400; break;
    case 57600: speed = B57600; break;
    case 115200: speed = B115200; break;
    case 230400: speed = B230400; break;
    case 460800: speed = B460800; break;
    case 921600: speed = B921600; break;
    default: speed = B115200; break;
  }
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);

  if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
    close();
    throw std::runtime_error("SerialPort: tcsetattr failed: " + std::string(std::strerror(errno)));
  }

  // Flush any stale input.
  tcflush(fd_, TCIOFLUSH);
}

void SerialPort::close()
{
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

void SerialPort::write(const std::vector<uint8_t> & data)
{
  if (!is_open()) {
    throw std::runtime_error("SerialPort: not open");
  }
  size_t written = 0;
  while (written < data.size()) {
    ssize_t n = ::write(fd_, data.data() + written, data.size() - written);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("SerialPort: write failed: " + std::string(std::strerror(errno)));
    }
    written += static_cast<size_t>(n);
  }
}

std::vector<uint8_t> SerialPort::read(size_t max_bytes)
{
  if (!is_open()) {
    throw std::runtime_error("SerialPort: not open");
  }
  std::vector<uint8_t> buf(max_bytes);
  ssize_t n = ::read(fd_, buf.data(), buf.size());
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return {};
    }
    throw std::runtime_error("SerialPort: read failed: " + std::string(std::strerror(errno)));
  }
  buf.resize(static_cast<size_t>(n));
  return buf;
}

bool SerialPort::read_exact(std::vector<uint8_t> & out, size_t n, int timeout_ms)
{
  out.clear();
  out.reserve(n);
  const int poll_timeout = timeout_ms;
  while (out.size() < n) {
    pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int ret = ::poll(&pfd, 1, poll_timeout);
    if (ret == 0) {
      return false;  // timeout
    }
    if (ret < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("SerialPort: poll failed: " + std::string(std::strerror(errno)));
    }
    auto chunk = read(n - out.size());
    out.insert(out.end(), chunk.begin(), chunk.end());
  }
  return true;
}

}  // namespace golfcart