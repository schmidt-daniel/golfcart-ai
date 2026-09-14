#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/assist_config.hpp"
#include "golfcart_msgs/msg/mode_state.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "golfcart_msgs/msg/obstacle.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Obstacle Steering Assist node (manual driving).
//
// While the operator drives manually, gently steer around nearby obstacles
// instead of only hard-stopping on them. The Safety Controller disables the
// obstacle hard-stop in MANUAL mode (the operator has full control); this node
// provides a soft steering nudge so the operator gets help but keeps control.
//
//   /obstacles/awareness (Obstacle)  →  nearest obstacle (distance, angle)
//   /mode/state (ModeState)          →  only active in MANUAL mode
//        ↓
//   steering nudge = f(distance, angle)   (away from the obstacle)
//        ↓
//   motion/request (MotionRequest, priority 1 = manual)
//
// The nudge is angular only (it steers, it does not drive). It is tunable so
// it doesn't fight the operator's own steering.
class SteeringAssistNode : public rclcpp::Node
{
public:
  SteeringAssistNode()
  : Node("steering_assist_node")
  {
    // Parameters.
    assist_gain_ = declare_parameter<double>("assist_gain", 0.5);   // rad/s per (1/distance)
    min_distance_m_ = declare_parameter<double>("min_distance_m", 0.5);
    max_distance_m_ = declare_parameter<double>("max_distance_m", 2.0);
    max_nudge_radps_ = declare_parameter<double>("max_nudge_radps", 0.4);

    // Subscriptions.
    obstacle_sub_ = create_subscription<golfcart_msgs::msg::Obstacle>(
      "obstacles/awareness", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::Obstacle::SharedPtr msg) {
        if (msg->valid) {
          obstacle_distance_ = msg->distance_m;
          obstacle_angle_ = msg->angle_rad;
          obstacle_valid_ = true;
        } else {
          obstacle_valid_ = false;
        }
      });

    mode_sub_ = create_subscription<golfcart_msgs::msg::ModeState>(
      "mode/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ModeState::SharedPtr msg) {
        manual_mode_ = (msg->mode == 0);  // 0 = MANUAL
      });

    // Assist config: the operator can enable/disable steering assist from the
    // HMI Assist screen (via the handle gateway -> /assist/config).
    assist_sub_ = create_subscription<golfcart_msgs::msg::AssistConfig>(
      "assist/config", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::AssistConfig::SharedPtr msg) {
        enabled_ = msg->steering_assist_enabled;
      });

    // Publisher for the steering nudge.
    req_pub_ = create_publisher<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS());

    // Control loop (50 Hz).
    timer_ = create_wall_timer(
      std::chrono::milliseconds(20),
      [this]() { update(); });
  }

private:
  void update()
  {
    // Only assist when enabled AND in MANUAL mode.
    if (!enabled_ || !manual_mode_ || !obstacle_valid_) {
      publish_request(0.0, 0.0, "steering_assist", 1);
      return;
    }

    // Only assist when the obstacle is within the assist range.
    if (obstacle_distance_ > max_distance_m_ || obstacle_distance_ < min_distance_m_) {
      publish_request(0.0, 0.0, "steering_assist", 1);
      return;
    }

    // Steering nudge: steer AWAY from the obstacle. The obstacle angle is
    // relative to the trolley's forward axis; a positive angle (obstacle on
    // the left) means steer right (negative angular velocity).
    // Strength grows as the obstacle gets closer (1/distance).
    double strength = assist_gain_ * (1.0 / obstacle_distance_);
    double nudge = -std::copysign(strength, obstacle_angle_);
    // Clamp to max nudge.
    if (nudge > max_nudge_radps_) {
      nudge = max_nudge_radps_;
    } else if (nudge < -max_nudge_radps_) {
      nudge = -max_nudge_radps_;
    }

    publish_request(0.0, nudge, "steering_assist", 1);
  }

  void publish_request(double linear, double angular, const std::string & source, uint8_t priority)
  {
    golfcart_msgs::msg::MotionRequest msg;
    msg.linear_velocity_mps = linear;
    msg.angular_velocity_radps = angular;
    msg.source = source;
    msg.priority = priority;
    msg.timestamp = now();
    req_pub_->publish(msg);
  }

  double assist_gain_ = 0.5;
  double min_distance_m_ = 0.5;
  double max_distance_m_ = 2.0;
  double max_nudge_radps_ = 0.4;

  bool manual_mode_ = true;
  bool enabled_ = true;  // steering assist on/off (from /assist/config)
  bool obstacle_valid_ = false;
  float obstacle_distance_ = 0.0f;
  float obstacle_angle_ = 0.0f;

  rclcpp::Subscription<golfcart_msgs::msg::Obstacle>::SharedPtr obstacle_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::ModeState>::SharedPtr mode_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::AssistConfig>::SharedPtr assist_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr req_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SteeringAssistNode>());
  rclcpp::shutdown();
  return 0;
}