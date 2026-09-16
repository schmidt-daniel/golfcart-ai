#include <cstdint>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace golfcart
{

// Camera node.
// Publishes the RGB camera feed on /camera/image (sensor_msgs/Image) from the
// Pi Camera Module 3.
//
// The actual capture source (libcamera / rpicam on the Pi) is hardware-
// dependent and abstracted here via capture_frame(). In the hardware phase
// this reads from the Pi Camera; for testing / simulation it can be fed
// synthetic frames. Downstream consumers (segmentation_node, gesture
// recognition, the web hazard view) subscribe to /camera/image.
//
// Publishes:
//   /camera/image  (sensor_msgs/Image)  RGB, downscaled, low rate (~2-5 Hz)
class CameraNode : public rclcpp::Node
{
public:
  CameraNode()
  : Node("camera_node")
  {
    const double rate_hz = declare_parameter<double>("publish_rate_hz", 5.0);
    width_ = declare_parameter<int>("width", 640);
    height_ = declare_parameter<int>("height", 480);

    image_pub_ = create_publisher<sensor_msgs::msg::Image>(
      "camera/image", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_frame(); });
  }

  // Feed a synthetic RGB frame (width*height*3 bytes) for testing. When not
  // fed, the node publishes a placeholder frame so the pipeline stays live.
  void feed_frame(const std::vector<uint8_t> & rgb)
  {
    if (rgb.size() == static_cast<size_t>(width_) * height_ * 3) {
      synthetic_ = rgb;
    }
  }

private:
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
    } else {
      // Placeholder: solid dark frame so consumers see a live (if blank) feed.
      msg.data.assign(static_cast<size_t>(width_) * height_ * 3, 0);
    }
    image_pub_->publish(msg);
  }

  int width_;
  int height_;
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