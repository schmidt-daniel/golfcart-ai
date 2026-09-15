#include <cmath>
#include <memory>

#include "golfcart_control/slip_math.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "golfcart_msgs/msg/slip_status.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Wheel-Slip / Traction Detection node.
// Compares the wheel-derived forward speed (from /motor/state) to the actual
// fused-pose forward speed (from /odometry/filtered). When the wheels are
// spinning much faster than the trolley is actually moving, that's slip.
//
// Publishes:
//   /slip/status  (golfcart_msgs/SlipStatus)
//     slipping = true when sustained slip is detected (debounced)
//
// Slip is a warning, not a fault — the Safety Controller may warn and/or
// limit speed, but does not hard-stop by default.
class WheelSlipNode : public rclcpp::Node
{
public:
  WheelSlipNode()
  : Node("wheel_slip_node")
  {
    slip_threshold_ = declare_parameter<double>("slip_threshold", 0.3);
    debounce_s_ = declare_parameter<double>("debounce_s", 0.5);
    const double rate_hz = declare_parameter<double>("publish_rate_hz", 20.0);

    detector_ = SlipDetector(slip_threshold_, debounce_s_);

    motor_sub_ = create_subscription<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorState::SharedPtr msg) {
        wheel_speed_ = (msg->left_velocity + msg->right_velocity) / 2.0;
        have_wheel_ = true;
      });

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        actual_speed_ = std::abs(msg->twist.twist.linear.x);
        have_actual_ = true;
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::SlipStatus>(
      "slip/status", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { update(); });
  }

private:
  void update()
  {
    golfcart_msgs::msg::SlipStatus msg;
    msg.timestamp = now();
    msg.valid = false;
    msg.slipping = false;
    msg.slip_ratio = 0.0;
    msg.wheel_speed_mps = wheel_speed_;
    msg.actual_speed_mps = actual_speed_;

    if (!have_wheel_ || !have_actual_) {
      status_pub_->publish(msg);
      return;
    }

    const double ratio = slip_ratio(wheel_speed_, actual_speed_);
    const double dt = (now() - last_update_time_).seconds();
    last_update_time_ = now();
    const bool slipping = detector_.update(ratio, dt);

    msg.valid = true;
    msg.slipping = slipping;
    msg.slip_ratio = ratio;
    msg.wheel_speed_mps = wheel_speed_;
    msg.actual_speed_mps = actual_speed_;
    status_pub_->publish(msg);
  }

  double slip_threshold_ = 0.3;
  double debounce_s_ = 0.5;
  SlipDetector detector_;

  bool have_wheel_ = false;
  bool have_actual_ = false;
  double wheel_speed_ = 0.0;
  double actual_speed_ = 0.0;
  rclcpp::Time last_update_time_;

  rclcpp::Subscription<golfcart_msgs::msg::MotorState>::SharedPtr motor_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::SlipStatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::WheelSlipNode>());
  rclcpp::shutdown();
  return 0;
}