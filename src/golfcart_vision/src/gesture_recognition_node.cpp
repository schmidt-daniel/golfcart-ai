#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "golfcart_msgs/msg/gesture_command.hpp"
#include "golfcart_msgs/msg/pose_keypoints.hpp"
#include "golfcart_vision/gesture_math.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"

namespace golfcart
{

// Gesture Recognition node.
// Consumes body keypoints (shoulder/elbow/wrist) and classifies the motion
// into a gesture, publishing a GestureCommand on /gesture/command.
//
// The keypoint source is a lightweight CPU skin-based arm estimator that runs
// on /camera/image, so gestures work without a Coral accelerator. A MediaPipe
// Pose backend can replace estimate_keypoints() later without changing the
// classification or the ROS topic contract.
//
// Publishes:
//   /gesture/command  (golfcart_msgs/GestureCommand)
//     gesture = NONE/WINDMILL/STOP/FOLLOW/SLOW
//
// Gestures are advisory — the gesture_controller_node maps them to actions.
class GestureRecognitionNode : public rclcpp::Node
{
public:
  GestureRecognitionNode()
  : Node("gesture_recognition_node")
  {
    confidence_threshold_ = declare_parameter<double>("confidence_threshold", 0.7);
    const int debounce_frames = declare_parameter<int>("debounce_frames", 15);
    rate_hz_ = declare_parameter<double>("publish_rate_hz", 10.0);
    processing_width_ = declare_parameter<int>("processing_width", 160);
    processing_height_ = declare_parameter<int>("processing_height", 90);

    debouncer_ = GestureDebouncer(debounce_frames);

    command_pub_ = create_publisher<golfcart_msgs::msg::GestureCommand>(
      "gesture/command", rclcpp::SensorDataQoS());

    // Coral pose keypoints take priority when available; the CPU estimator is
    // the fallback when no keypoints arrive.
    pose_sub_ = create_subscription<golfcart_msgs::msg::PoseKeypoints>(
      "pose/keypoints", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::PoseKeypoints::SharedPtr msg) {
        if (!msg->valid) {
          return;
        }
        const Keypoints keypoints{
          Point2{msg->shoulder_l_x, msg->shoulder_l_y},
          Point2{msg->elbow_l_x, msg->elbow_l_y},
          Point2{msg->wrist_l_x, msg->wrist_l_y},
          Point2{msg->shoulder_r_x, msg->shoulder_r_y},
          Point2{msg->elbow_r_x, msg->elbow_r_y},
          Point2{msg->wrist_r_x, msg->wrist_r_y},
          msg->confidence, true};
        last_ = classify(
          keypoints.shoulder_l, keypoints.elbow_l, keypoints.wrist_l,
          keypoints.shoulder_r, keypoints.elbow_r, keypoints.wrist_r);
        confidence_ = keypoints.confidence;
        have_ = true;
        last_pose_time_ = now();
      });

    image_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "camera/image", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::Image::SharedPtr msg) {
        // If Coral keypoints are arriving, skip the CPU estimator.
        if ((now() - last_pose_time_).seconds() < 1.0) {
          return;
        }
        if (msg->encoding != "rgb8" || msg->step < msg->width * 3 ||
            msg->data.size() < static_cast<size_t>(msg->step) * msg->height) {
          return;
        }
        const rclcpp::Time received = now();
        if ((received - last_inference_time_).seconds() < 1.0 / std::max(rate_hz_, 0.1)) {
          return;
        }
        const Keypoints keypoints = estimate_keypoints(*msg);
        if (keypoints.valid) {
          last_ = classify(
            keypoints.shoulder_l, keypoints.elbow_l, keypoints.wrist_l,
            keypoints.shoulder_r, keypoints.elbow_r, keypoints.wrist_r);
          confidence_ = keypoints.confidence;
          have_ = true;
          last_inference_time_ = received;
        }
      });

    const auto period = std::chrono::duration<double>(1.0 / std::max(rate_hz_, 0.1));
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { update(); });
  }

private:
  struct Keypoints
  {
    Point2 shoulder_l;
    Point2 elbow_l;
    Point2 wrist_l;
    Point2 shoulder_r;
    Point2 elbow_r;
    Point2 wrist_r;
    double confidence = 0.0;
    bool valid = false;
  };

  // Lightweight CPU arm keypoint estimator. It finds skin-colored blobs in the
  // upper half of the image and estimates shoulder/elbow/wrist positions from
  // their vertical distribution. This is a coarse fallback, not a learned
  // pose model; MediaPipe Pose can replace it later.
  Keypoints estimate_keypoints(const sensor_msgs::msg::Image & image) const
  {
    const int width = std::max(1, std::min(processing_width_, static_cast<int>(image.width)));
    const int height = std::max(1, std::min(processing_height_, static_cast<int>(image.height)));
    const int upper_height = std::max(1, height / 2);

    std::vector<double> skin_x;
    std::vector<double> skin_y;
    skin_x.reserve(static_cast<size_t>(width) * upper_height);
    skin_y.reserve(static_cast<size_t>(width) * upper_height);

    for (int y = 0; y < upper_height; ++y) {
      const int source_y = y * static_cast<int>(image.height) / height;
      for (int x = 0; x < width; ++x) {
        const int source_x = x * static_cast<int>(image.width) / width;
        const size_t offset = static_cast<size_t>(source_y) * image.step +
          static_cast<size_t>(source_x) * 3;
        const double red = image.data[offset];
        const double green = image.data[offset + 1];
        const double blue = image.data[offset + 2];
        const double max_value = std::max({red, green, blue});
        const double min_value = std::min({red, green, blue});
        const double saturation = max_value > 0.0 ?
          (max_value - min_value) / max_value : 0.0;
        // Skin heuristic: warm hue, moderate saturation, not too dark/bright.
        if (red > 60.0 && red > green * 1.15 && red > blue * 1.3 &&
            saturation > 0.15 && saturation < 0.75) {
          skin_x.push_back(static_cast<double>(x) / width);
          skin_y.push_back(static_cast<double>(y) / height);
        }
      }
    }

    Keypoints keypoints;
    if (skin_x.size() < 20) {
      return keypoints;
    }

    // Split skin pixels into left/right halves by the horizontal centroid.
    double mean_x = 0.0;
    for (const double x : skin_x) {
      mean_x += x;
    }
    mean_x /= skin_x.size();

    std::vector<double> left_x, left_y, right_x, right_y;
    for (size_t i = 0; i < skin_x.size(); ++i) {
      if (skin_x[i] < mean_x) {
        left_x.push_back(skin_x[i]);
        left_y.push_back(skin_y[i]);
      } else {
        right_x.push_back(skin_x[i]);
        right_y.push_back(skin_y[i]);
      }
    }

    if (left_x.size() < 10 || right_x.size() < 10) {
      return keypoints;
    }

    // Estimate shoulder/elbow/wrist from the vertical distribution of each arm.
    // Shoulder = top, wrist = bottom, elbow = middle.
    const auto estimate_arm = [](const std::vector<double> & xs,
                                 const std::vector<double> & ys,
                                 Point2 & shoulder, Point2 & elbow, Point2 & wrist) {
      double min_y = 1.0, max_y = 0.0;
      for (const double y : ys) {
        min_y = std::min(min_y, y);
        max_y = std::max(max_y, y);
      }
      const double span = std::max(1e-6, max_y - min_y);
      double shoulder_x = 0.0, elbow_x = 0.0, wrist_x = 0.0;
      int shoulder_n = 0, elbow_n = 0, wrist_n = 0;
      for (size_t i = 0; i < xs.size(); ++i) {
        const double t = (ys[i] - min_y) / span;
        if (t < 0.33) {
          shoulder_x += xs[i];
          ++shoulder_n;
        } else if (t < 0.66) {
          elbow_x += xs[i];
          ++elbow_n;
        } else {
          wrist_x += xs[i];
          ++wrist_n;
        }
      }
      if (shoulder_n == 0 || elbow_n == 0 || wrist_n == 0) {
        return false;
      }
      shoulder = Point2{shoulder_x / shoulder_n, min_y};
      elbow = Point2{elbow_x / elbow_n, min_y + span * 0.5};
      wrist = Point2{wrist_x / wrist_n, max_y};
      return true;
    };

    if (!estimate_arm(left_x, left_y, keypoints.shoulder_l, keypoints.elbow_l,
                      keypoints.wrist_l) ||
        !estimate_arm(right_x, right_y, keypoints.shoulder_r, keypoints.elbow_r,
                      keypoints.wrist_r)) {
      return keypoints;
    }

    keypoints.confidence = std::min(1.0, static_cast<double>(skin_x.size()) /
      (width * upper_height) * 8.0);
    keypoints.valid = true;
    return keypoints;
  }

  uint8_t classify(const Point2 & shoulder_l, const Point2 & elbow_l,
                   const Point2 & wrist_l, const Point2 & shoulder_r,
                   const Point2 & elbow_r, const Point2 & wrist_r)
  {
    // STOP: both hands up.
    if (is_double_palm_up(shoulder_l, wrist_l, shoulder_r, wrist_r)) {
      return GESTURE_STOP;
    }
    // SLOW: hand lowered (use the right arm; fall back to left).
    if (is_pat_down(shoulder_r, wrist_r) || is_pat_down(shoulder_l, wrist_l)) {
      return GESTURE_SLOW;
    }
    // FOLLOW: choo-choo (right arm, fall back to left).
    if (is_choo_choo(shoulder_r, elbow_r, wrist_r, 0.05) ||
        is_choo_choo(shoulder_l, elbow_l, wrist_l, 0.05)) {
      return GESTURE_FOLLOW;
    }
    // SUMMON: windmill (right arm, fall back to left).
    if (is_windmill(shoulder_r, elbow_r, wrist_r, 0.05) ||
        is_windmill(shoulder_l, elbow_l, wrist_l, 0.05)) {
      return GESTURE_SUMMON;
    }
    return GESTURE_NONE;
  }

  void update()
  {
    golfcart_msgs::msg::GestureCommand msg;
    msg.stamp = now();
    msg.confidence = 0.0f;
    msg.gesture = GESTURE_NONE;

    if (!have_) {
      command_pub_->publish(msg);
      return;
    }

    // Only accept above the confidence threshold.
    const uint8_t candidate = confidence_ >= confidence_threshold_ ? last_ : GESTURE_NONE;
    msg.gesture = debouncer_.update(candidate);
    msg.confidence = static_cast<float>(confidence_);
    command_pub_->publish(msg);
  }

  double confidence_threshold_ = 0.7;
  double rate_hz_ = 10.0;
  GestureDebouncer debouncer_;
  int processing_width_ = 160;
  int processing_height_ = 90;

  bool have_ = false;
  uint8_t last_ = GESTURE_NONE;
  double confidence_ = 0.0;
  rclcpp::Time last_inference_time_;
  rclcpp::Time last_pose_time_;

  rclcpp::Subscription<golfcart_msgs::msg::PoseKeypoints>::SharedPtr pose_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr image_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::GestureCommand>::SharedPtr command_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::GestureRecognitionNode>());
  rclcpp::shutdown();
  return 0;
}