#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/imu_data.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

namespace golfcart
{

// Hill Assist, Hill Descent Brake, and Rollback Protection behavior node.
//
// Produces MotionRequest messages that flow through the Safety Controller.
// These behaviors are safety-related and use a higher priority than manual
// teleop so they can override user commands when needed.
//
// - Rollback Protection: detects unintended backward movement (from wheel
//   encoders) when no backward command is active, and requests a stop/brake.
// - Hill Descent Brake: detects downhill movement and limits speed.
// - Hill Assist: detects uphill and provides propulsion assistance.
class HillRollbackNode : public rclcpp::Node
{
public:
  HillRollbackNode()
  : Node("hill_rollback_node")
  {
    // Parameters.
    slope_threshold_rad_ = declare_parameter<double>("slope_threshold_rad", 0.1);  // ~5.7 deg
    rollback_threshold_mps_ = declare_parameter<double>("rollback_threshold_mps", 0.05);
    descent_max_speed_mps_ = declare_parameter<double>("descent_max_speed_mps", 0.3);
    assist_speed_mps_ = declare_parameter<double>("assist_speed_mps", 0.2);
    hysteresis_rad_ = declare_parameter<double>("hysteresis_rad", 0.03);

    // Subscriptions.
    imu_sub_ = create_subscription<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ImuData::SharedPtr msg) {
        if (msg->valid) {
          pitch_ = msg->pitch_rad;
        }
      });

    motor_sub_ = create_subscription<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorState::SharedPtr msg) {
        wheel_velocity_ = (msg->left_velocity + msg->right_velocity) / 2.0;
      });

    // Publisher for behavior motion requests.
    req_pub_ = create_publisher<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS());

    // Status publisher for diagnostics.
    status_pub_ = create_publisher<std_msgs::msg::String>(
      "behavior/status", rclcpp::SensorDataQoS());

    // Control loop (50 Hz).
    timer_ = create_wall_timer(
      std::chrono::milliseconds(20),
      [this]() { update(); });
  }

private:
  // Determine the slope state with hysteresis.
  enum class SlopeState { LEVEL, UPHILL, DOWNHILL };

  SlopeState slope_state() const
  {
    // Positive pitch = nose up = uphill (per REP-103 convention).
    if (pitch_ > slope_threshold_rad_ + hysteresis_rad_) {
      return SlopeState::UPHILL;
    }
    if (pitch_ < -slope_threshold_rad_ - hysteresis_rad_) {
      return SlopeState::DOWNHILL;
    }
    return SlopeState::LEVEL;
  }

  void update()
  {
    const SlopeState slope = slope_state();

    // --- Rollback Protection ---
    // If the trolley is moving backward (negative wheel velocity) while on a
    // slope and no backward command is active, request a stop/brake.
    // (This node only issues safety requests; it does not command forward
    // motion on its own.)
    if (wheel_velocity_ < -rollback_threshold_mps_ && slope != SlopeState::LEVEL) {
      // Unintended backward movement on a slope -> brake.
      publish_request(0.0, 0.0, "rollback_protection", 3);
      publish_status("ROLLBACK_BRAKE");
      return;
    }

    // --- Hill Descent Brake ---
    // On a downhill slope, limit forward speed to prevent uncontrolled
    // acceleration. If moving downhill too fast, request a brake.
    if (slope == SlopeState::DOWNHILL) {
      if (wheel_velocity_ > descent_max_speed_mps_) {
        publish_request(0.0, 0.0, "hill_descent_brake", 3);
        publish_status("DESCENT_BRAKE");
        return;
      }
      // Otherwise allow limited forward motion.
      publish_request(descent_max_speed_mps_, 0.0, "hill_descent_brake", 2);
      publish_status("DESCENT_LIMIT");
      return;
    }

    // --- Hill Assist ---
    // On an uphill slope, provide propulsion assistance.
    if (slope == SlopeState::UPHILL) {
      publish_request(assist_speed_mps_, 0.0, "hill_assist", 2);
      publish_status("HILL_ASSIST");
      return;
    }

    // Level ground: no behavior action.
    publish_status("LEVEL");
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

  void publish_status(const std::string & status)
  {
    std_msgs::msg::String msg;
    msg.data = status;
    status_pub_->publish(msg);
  }

  double slope_threshold_rad_ = 0.1;
  double rollback_threshold_mps_ = 0.05;
  double descent_max_speed_mps_ = 0.3;
  double assist_speed_mps_ = 0.2;
  double hysteresis_rad_ = 0.03;

  double pitch_ = 0.0;
  double wheel_velocity_ = 0.0;

  rclcpp::Subscription<golfcart_msgs::msg::ImuData>::SharedPtr imu_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::MotorState>::SharedPtr motor_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr req_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::HillRollbackNode>());
  rclcpp::shutdown();
  return 0;
}