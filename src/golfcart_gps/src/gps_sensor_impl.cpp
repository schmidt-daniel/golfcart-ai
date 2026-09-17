#include "golfcart_gps/gps_sensor_impl.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <netdb.h>
#include <sys/socket.h>
#include <stdexcept>
#include <unistd.h>

namespace golfcart
{

namespace
{

bool json_number(const std::string & line, const std::string & key, double & value)
{
  const std::string token = "\"" + key + "\":";
  const auto key_pos = line.find(token);
  if (key_pos == std::string::npos) {
    return false;
  }

  const char * begin = line.c_str() + key_pos + token.size();
  char * end = nullptr;
  value = std::strtod(begin, &end);
  return end != begin && std::isfinite(value);
}

bool json_integer(const std::string & line, const std::string & key, int & value)
{
  double number = 0.0;
  if (!json_number(line, key, number)) {
    return false;
  }
  value = static_cast<int>(number);
  return true;
}

bool json_string(const std::string & line, const std::string & key, std::string & value)
{
  const std::string token = "\"" + key + "\":\"";
  const auto start = line.find(token);
  if (start == std::string::npos) {
    return false;
  }
  const auto value_start = start + token.size();
  const auto value_end = line.find('"', value_start);
  if (value_end == std::string::npos) {
    return false;
  }
  value = line.substr(value_start, value_end - value_start);
  return true;
}

std::string json_escape(const std::string & value)
{
  std::string escaped;
  for (const char character : value) {
    if (character == '\\' || character == '"') {
      escaped.push_back('\\');
    }
    escaped.push_back(character);
  }
  return escaped;
}

}  // namespace

GpsSensorImpl::GpsSensorImpl(const std::string & device, const std::string & host, int port)
  : device_(device), host_(host), port_(port),
    last_fix_time_(std::chrono::steady_clock::time_point::min())
{
}

GpsSensorImpl::~GpsSensorImpl()
{
  close_connection();
}

bool GpsSensorImpl::connect_gpsd()
{
  if (socket_fd_ >= 0) {
    return true;
  }

  const std::string port = std::to_string(port_);
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo * results = nullptr;
  if (getaddrinfo(host_.c_str(), port.c_str(), &hints, &results) != 0) {
    return false;
  }

  for (addrinfo * result = results; result != nullptr; result = result->ai_next) {
    const int fd = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (fd < 0) {
      continue;
    }
    if (connect(fd, result->ai_addr, result->ai_addrlen) == 0) {
      socket_fd_ = fd;
      break;
    }
    close(fd);
  }
  freeaddrinfo(results);

  if (socket_fd_ < 0) {
    return false;
  }

  const std::string watch =
    "?WATCH={\"enable\":true,\"json\":true,\"device\":\"" +
    json_escape(device_) + "\"};\n";
  if (send(socket_fd_, watch.data(), watch.size(), MSG_NOSIGNAL) !=
      static_cast<ssize_t>(watch.size())) {
    close_connection();
    return false;
  }
  return true;
}

void GpsSensorImpl::close_connection()
{
  if (socket_fd_ >= 0) {
    close(socket_fd_);
    socket_fd_ = -1;
  }
  receive_buffer_.clear();
}

void GpsSensorImpl::read_available()
{
  char buffer[4096];
  while (socket_fd_ >= 0) {
    const ssize_t count = recv(socket_fd_, buffer, sizeof(buffer), MSG_DONTWAIT);
    if (count > 0) {
      receive_buffer_.append(buffer, static_cast<size_t>(count));
      while (true) {
        const auto newline = receive_buffer_.find('\n');
        if (newline == std::string::npos) {
          break;
        }
        const std::string line = receive_buffer_.substr(0, newline);
        receive_buffer_.erase(0, newline + 1);
        process_line(line);
      }
      continue;
    }
    if (count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
      close_connection();
    }
    break;
  }
}

void GpsSensorImpl::process_line(const std::string & line)
{
  std::string message_class;
  if (!json_string(line, "class", message_class)) {
    return;
  }

  if (message_class == "SKY") {
    double value = 0.0;
    int satellites = 0;
    if (json_number(line, "hdop", value)) {
      latest_.hdop = value;
    }
    if (json_number(line, "vdop", value)) {
      latest_.vdop = value;
    }
    if (json_number(line, "pdop", value)) {
      latest_.pdop = value;
    }
    for (size_t offset = 0; (offset = line.find("\"PRN\"", offset)) != std::string::npos;
         offset += 5) {
      ++satellites;
    }
    if (satellites > 0) {
      latest_.satellites = static_cast<uint8_t>(std::min(satellites, 255));
    }
    return;
  }

  if (message_class != "TPV") {
    return;
  }

  int mode = 0;
  if (json_integer(line, "mode", mode)) {
    latest_.fix_type = static_cast<uint8_t>(std::clamp(mode, 0, 255));
  }

  double value = 0.0;
  if (json_number(line, "lat", value)) {
    latest_.latitude_deg = value;
  }
  if (json_number(line, "lon", value)) {
    latest_.longitude_deg = value;
  }
  if (json_number(line, "alt", value)) {
    latest_.altitude_m = value;
  }
  if (json_number(line, "speed", value)) {
    latest_.speed_mps = value;
  }
  if (json_number(line, "track", value)) {
    constexpr double degrees_to_radians = 3.14159265358979323846 / 180.0;
    latest_.heading_rad = value * degrees_to_radians;
  }

  latest_.valid = latest_.fix_type >= 2;
  if (latest_.valid) {
    last_fix_time_ = std::chrono::steady_clock::now();
  }
}

GpsSample GpsSensorImpl::read()
{
  if (socket_fd_ < 0) {
    connect_gpsd();
  }
  read_available();

  GpsSample sample = latest_;
  if (last_fix_time_ == std::chrono::steady_clock::time_point::min() ||
      std::chrono::duration<double>(std::chrono::steady_clock::now() - last_fix_time_).count() > 3.0) {
    sample.valid = false;
  }
  return sample;
}

}  // namespace golfcart