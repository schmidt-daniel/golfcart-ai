#include <memory>
#include <mutex>

#include "geometry_msgs/msg/twist.hpp"
#include "golfcart_control/differential_drive.hpp"
#include "golfcart_msgs/msg/motor_command.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Motion Controller: converts a Safety-approved linear/angular command
// into differential-drive wheel velocities and publishes /motor/command.
// Runs a fixed 50 Hz control loop, holding the latest approved command.
class MotionControllerNode : public rclcpp::Node
{
public:
  MotionControllerNode()
  : Node("motion_controller")
  {
    drive_.wheel_separation_m = declare_parameter<double>("wheel_separation_m", 0.5);
    const double rate_hz = declare_parameter<double>("control_rate_hz", 50.0);

    cmd_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      "motion/safe_command", rclcpp::SensorDataQoS(),
      [this](const geometry_msgs::msg::Twist::SharedPtr msg) {
        std::lock_guard<std::mutex> lock(mutex_);
        last_linear_ = msg->linear.x;
        last_angular_ = msg->angular.z;
      });

    motor_cmd_pub_ = create_publisher<golfcart_msgs::msg::MotorCommand>(
      "motor/command", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() {
        double linear = 0.0, angular = 0.0;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          linear = last_linear_;
          angular = last_angular_;
        }
        double left = 0.0, right = 0.0;
        drive_.to_wheel_velocities(linear, angular, left, right);

        golfcart_msgs::msg::MotorCommand out;
        out.left_motor_velocity = left;
        out.right_motor_velocity = right;
        out.timestamp = now();
        motor_cmd_pub_->publish(out);
      });
  }

private:
  DifferentialDrive drive_;
  std::mutex mutex_;
  double last_linear_ = 0.0;
  double last_angular_ = 0.0;
  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotorCommand>::SharedPtr motor_cmd_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::MotionControllerNode>());
  rclcpp::shutdown();
  return 0;
}
