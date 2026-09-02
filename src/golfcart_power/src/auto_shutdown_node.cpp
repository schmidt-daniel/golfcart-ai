#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/imu_data.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

namespace golfcart
{

// Auto-shutdown watchdog.
//
// Monitors activity (joystick/motion) and shuts down (or signals a low-power
// state) after the trolley has been idle for a configurable period.
//
// Safety: shutdown is SUPPRESSED when there is a risk of rolling away:
//   - the trolley is on a slope (IMU inclination exceeds a threshold), or
//   - the trolley is moving (wheel motion detected).
class AutoShutdownNode : public rclcpp::Node
{
public:
  AutoShutdownNode()
  : Node("auto_shutdown_node")
  {
    idle_timeout_s_ = declare_parameter<double>("idle_timeout_s", 300.0);
    max_slope_rad_ = declare_parameter<double>("max_slope_rad", 0.15);  // ~8.6 deg
    motion_threshold_mps_ = declare_parameter<double>("motion_threshold_mps", 0.05);
    shutdown_cmd_ = declare_parameter<std::string>("shutdown_command", "echo 'auto-shutdown'");

    // Track last activity time.
    last_activity_ = now();

    // Subscriptions.
    imu_sub_ = create_subscription<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ImuData::SharedPtr msg) {
        if (msg->valid) {
          // Inclination magnitude from roll/pitch.
          inclination_ = std::sqrt(
            msg->roll_rad * msg->roll_rad + msg->pitch_rad * msg->pitch_rad);
        }
      });

    motor_sub_ = create_subscription<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorState::SharedPtr msg) {
        const double speed = (std::abs(msg->left_velocity) + std::abs(msg->right_velocity)) / 2.0;
        if (speed > motion_threshold_mps_) {
          last_activity_ = now();
        }
      });

    safety_sub_ = create_subscription<std_msgs::msg::String>(
      "safety/state", rclcpp::SensorDataQoS(),
      [this](const std_msgs::msg::String::SharedPtr msg) {
        // Any non-stopped safety state counts as activity.
        if (msg->data != "SAFE_STOPPED") {
          last_activity_ = now();
        }
      });

    // Status publisher (for diagnostics / HMI).
    status_pub_ = create_publisher<std_msgs::msg::String>(
      "power/status", rclcpp::SensorDataQoS());

    // Check timer (1 Hz).
    timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { check(); });
  }

private:
  bool at_roll_away_risk() const
  {
    // On a slope -> could roll if unpowered.
    if (inclination_ > max_slope_rad_) {
      return true;
    }
    return false;
  }

  void check()
  {
    const double idle_age = (now() - last_activity_).seconds();

    // Suppress shutdown if at roll-away risk.
    if (at_roll_away_risk()) {
      RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 5000,
        "Roll-away risk (slope %.2f rad) - auto-shutdown suppressed", inclination_);
      publish_status("SUPPRESSED_ROLL_AWAY");
      return;
    }

    if (idle_age >= idle_timeout_s_) {
      RCLCPP_WARN(get_logger(), "Idle for %.0f s - triggering shutdown", idle_age);
      publish_status("SHUTDOWN");
      // Execute the shutdown command (graceful power off).
      // NOTE: In a real deployment this would be a safe power-off command.
      // For testing, it is a configurable command (default just logs).
      const int ret = std::system(shutdown_cmd_.c_str());
      (void)ret;
      // Stop the timer so we don't repeatedly trigger.
      timer_->cancel();
    } else {
      publish_status("IDLE");
    }
  }

  void publish_status(const std::string & status)
  {
    std_msgs::msg::String msg;
    msg.data = status;
    status_pub_->publish(msg);
  }

  double idle_timeout_s_ = 300.0;
  double max_slope_rad_ = 0.15;
  double motion_threshold_mps_ = 0.05;
  std::string shutdown_cmd_;
  double inclination_ = 0.0;
  rclcpp::Time last_activity_;

  rclcpp::Subscription<golfcart_msgs::msg::ImuData>::SharedPtr imu_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::MotorState>::SharedPtr motor_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr safety_sub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::AutoShutdownNode>());
  rclcpp::shutdown();
  return 0;
}