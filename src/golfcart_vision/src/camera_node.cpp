#include <cstdint>
#include <fcntl.h>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace golfcart
{

// Camera node.
// Publishes the RGB camera feed on /camera/image (sensor_msgs/Image) from the
// Pi Camera Module 3.
//
// Captures RGB frames through rpicam-still, the Raspberry Pi libcamera CLI.
// Downstream consumers (segmentation_node, gesture recognition, and the web
// hazard view) subscribe to /camera/image.
//
// Publishes:
//   /camera/image  (sensor_msgs/Image)  RGB, downscaled, low rate (~2-5 Hz)
class CameraNode : public rclcpp::Node
{
public:
  CameraNode()
  : Node("camera_node")
  {
    const double rate_hz = declare_parameter<double>("publish_rate_hz", 2.0);
    width_ = declare_parameter<int>("width", 640);
    height_ = declare_parameter<int>("height", 480);
    capture_binary_ = declare_parameter<std::string>(
      "capture_binary", "rpicam-still");

    image_pub_ = create_publisher<sensor_msgs::msg::Image>(
      "camera/image", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_frame(); });
  }

  // Feed a synthetic RGB frame for tests. Hardware capture takes precedence
  // only when no synthetic frame has been supplied.
  void feed_frame(const std::vector<uint8_t> & rgb)
  {
    if (rgb.size() == static_cast<size_t>(width_) * height_ * 3) {
      synthetic_ = rgb;
    }
  }

private:
  bool capture_frame(std::vector<uint8_t> & frame)
  {
    const size_t expected_size = static_cast<size_t>(width_) * height_ * 3;
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
      return false;
    }

    const std::string width = std::to_string(width_);
    const std::string height = std::to_string(height_);
    const pid_t child = fork();
    if (child < 0) {
      close(pipe_fds[0]);
      close(pipe_fds[1]);
      return false;
    }
    if (child == 0) {
      dup2(pipe_fds[1], STDOUT_FILENO);
      close(pipe_fds[0]);
      close(pipe_fds[1]);
      const int null_fd = open("/dev/null", O_WRONLY);
      if (null_fd >= 0) {
        dup2(null_fd, STDERR_FILENO);
        close(null_fd);
      }
      execlp(
        capture_binary_.c_str(), capture_binary_.c_str(),
        "--nopreview", "--immediate", "--timeout", "200",
        "--width", width.c_str(), "--height", height.c_str(),
        "--encoding", "rgb", "--output", "-", static_cast<char *>(nullptr));
      _exit(127);
    }

    close(pipe_fds[1]);
    frame.assign(expected_size, 0);
    size_t received = 0;
    while (received < expected_size) {
      const ssize_t count = read(
        pipe_fds[0], frame.data() + received, expected_size - received);
      if (count <= 0) {
        break;
      }
      received += static_cast<size_t>(count);
    }
    close(pipe_fds[0]);

    int status = 0;
    waitpid(child, &status, 0);
    if (received != expected_size || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      frame.clear();
      return false;
    }
    return true;
  }

  void publish_frame()
  {
    sensor_msgs::msg::Image msg;
    msg.header.stamp = now();
    msg.header.frame_id = "camera";
    msg.width = static_cast<uint32_t>(width_);
    msg.height = static_cast<uint32_t>(height_);
    msg.encoding = "rgb8";
    msg.is_bigendian = false;
    msg.step = static_cast<uint32_t>(width_ * 3);

    if (!synthetic_.empty()) {
      msg.data = synthetic_;
    } else if (!capture_frame(msg.data)) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Unable to capture camera frame with %s; /camera/image remains silent",
        capture_binary_.c_str());
      return;
    }
    image_pub_->publish(msg);
  }

  int width_;
  int height_;
  std::string capture_binary_;
  std::vector<uint8_t> synthetic_;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr image_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::CameraNode>());
  rclcpp::shutdown();
  return 0;
}