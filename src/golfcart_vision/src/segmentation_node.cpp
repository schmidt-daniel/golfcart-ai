#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

#include "golfcart_msgs/msg/capability_status.hpp"
#include "golfcart_msgs/msg/segmentation_status.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace golfcart
{

// Lightweight CPU course classifier. It samples a small RGB grid so it can
// run on the Pi without a Coral accelerator. A model-backed implementation can
// replace classify() later without changing the ROS topic contract.
class SegmentationNode : public rclcpp::Node
{
public:
  SegmentationNode()
  : Node("segmentation_node")
  {
    rate_hz_ = declare_parameter<double>("publish_rate_hz", 1.0);
    processing_width_ = declare_parameter<int>("processing_width", 160);
    processing_height_ = declare_parameter<int>("processing_height", 90);
    min_confidence_ = declare_parameter<double>("min_confidence", 0.35);

    cap_sub_ = create_subscription<golfcart_msgs::msg::CapabilityStatus>(
      "capability/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg) {
        camera_available_ = msg->camera;
      });

    image_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "camera/image", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::Image::SharedPtr msg) {
        if (msg->encoding != "rgb8" || msg->step < msg->width * 3 ||
            msg->data.size() < static_cast<size_t>(msg->step) * msg->height) {
          return;
        }
        const rclcpp::Time received = now();
        if ((received - last_inference_time_).seconds() <
            1.0 / std::max(rate_hz_, 0.1)) {
          return;
        }
        const Classification result = classify(*msg);
        if (result.valid) {
          class_count_ = result.class_count;
          confidence_ = result.confidence;
          last_inference_time_ = received;
          last_result_time_ = received;
        }
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::SegmentationStatus>(
      "segmentation/status", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / std::max(rate_hz_, 0.1));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_status(); });
  }

private:
  struct Classification
  {
    uint8_t class_count = 0;
    float confidence = 0.0f;
    bool valid = false;
  };

  Classification classify(const sensor_msgs::msg::Image & image) const
  {
    const int output_width = std::max(
      1, std::min(processing_width_, static_cast<int>(image.width)));
    const int output_height = std::max(
      1, std::min(processing_height_, static_cast<int>(image.height)));
    int water = 0;
    int bunker = 0;
    int fairway = 0;
    int rough = 0;
    int samples = 0;

    for (int y = 0; y < output_height; ++y) {
      const int source_y = y * static_cast<int>(image.height) / output_height;
      for (int x = 0; x < output_width; ++x) {
        const int source_x = x * static_cast<int>(image.width) / output_width;
        const size_t offset = static_cast<size_t>(source_y) * image.step +
          static_cast<size_t>(source_x) * 3;
        const double red = image.data[offset];
        const double green = image.data[offset + 1];
        const double blue = image.data[offset + 2];
        const double max_value = std::max({red, green, blue});
        const double min_value = std::min({red, green, blue});
        const double saturation = max_value > 0.0 ?
          (max_value - min_value) / max_value : 0.0;
        const double brightness = max_value / 255.0;

        if (blue > green * 1.12 && blue > red * 1.25 && saturation > 0.18) {
          ++water;
        } else if (brightness > 0.58 && saturation < 0.32) {
          ++bunker;
        } else if (green > red * 1.08 && green > blue * 1.05 && saturation > 0.16) {
          if (green > 105.0 && red > 35.0) {
            ++fairway;
          } else {
            ++rough;
          }
        } else {
          ++rough;
        }
        ++samples;
      }
    }

    if (samples == 0) {
      return {};
    }
    const int counts[] = {water, bunker, fairway, rough};
    const int recognized = std::count_if(
      std::begin(counts), std::end(counts), [](int count) { return count > 0; });
    const int largest = *std::max_element(std::begin(counts), std::end(counts));
    const float confidence = static_cast<float>(largest) / samples;
    return Classification{
      static_cast<uint8_t>(recognized), confidence, confidence >= min_confidence_};
  }

  void publish_status()
  {
    golfcart_msgs::msg::SegmentationStatus msg;
    const bool inference_live = last_result_time_.nanoseconds() != 0 &&
      (now() - last_result_time_).seconds() < 3.0;
    msg.active = inference_live;
    msg.valid = camera_available_ && inference_live;
    msg.class_count = inference_live ? class_count_ : 0;
    msg.confidence = inference_live ? confidence_ : 0.0f;
    msg.timestamp = now();
    status_pub_->publish(msg);
  }

  double rate_hz_ = 1.0;
  int processing_width_ = 160;
  int processing_height_ = 90;
  double min_confidence_ = 0.35;
  bool camera_available_ = false;
  uint8_t class_count_ = 0;
  float confidence_ = 0.0f;
  rclcpp::Time last_inference_time_;
  rclcpp::Time last_result_time_;

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
