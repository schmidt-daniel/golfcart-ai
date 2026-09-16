#include <memory>

#include "golfcart_msgs/msg/capability_status.hpp"
#include "golfcart_msgs/msg/segmentation_status.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace golfcart
{

// Segmentation node.
// Runs live course segmentation (fairway/rough/water/bunkers/etc.) on the
// camera feed, gated on the camera + Coral capabilities.
//
// The actual model inference (zero-shot SAM / a fine-tuned model on the Coral
// NPU) is hardware-dependent and abstracted here. This node:
//   - subscribes to /camera/image (RGB from the Pi Camera)
//   - tracks camera + Coral capability from /capability/status
//   - publishes /segmentation/status (active, class_count, confidence)
//
// When the camera or Coral is absent, segmentation is inactive (active=false).
class SegmentationNode : public rclcpp::Node
{
public:
  SegmentationNode()
  : Node("segmentation_node")
  {
    const double rate_hz = declare_parameter<double>("publish_rate_hz", 5.0);

    // Track camera + Coral capability: segmentation requires both.
    cap_sub_ = create_subscription<golfcart_msgs::msg::CapabilityStatus>(
      "capability/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg) {
        camera_available_ = msg->camera;
        coral_available_ = msg->coral;
      });

    // Camera feed (RGB). Presence of frames also implies the camera is live.
    image_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "camera/image", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::Image::SharedPtr) {
        last_image_time_ = now();
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::SegmentationStatus>(
      "segmentation/status", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_status(); });
  }

private:
  void publish_status()
  {
    golfcart_msgs::msg::SegmentationStatus msg;
    // Active only when camera + Coral are present AND frames are arriving.
    const bool frames_live =
      (now() - last_image_time_).seconds() < image_timeout_s_;
    msg.active = camera_available_ && coral_available_ && frames_live;
    msg.valid = true;
    msg.class_count = msg.active ? class_count_ : 0;
    msg.confidence = msg.active ? confidence_ : 0.0f;
    msg.timestamp = now();
    status_pub_->publish(msg);
  }

  bool camera_available_ = false;
  bool coral_available_ = false;
  rclcpp::Time last_image_time_;
  double image_timeout_s_ = 2.0;
  uint8_t class_count_ = 0;   // set by the model once running
  float confidence_ = 0.0f;   // set by the model once running

  rclcpp::Subscription<golfcart_msgs::msg::CapabilityStatus>::SharedPtr cap_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::SegmentationStatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SegmentationNode>());
  rclcpp::shutdown();
  return 0;
}