#include <algorithm>
#include <memory>
#include <string>

#include "geometry_msgs/msg/twist.hpp"
#include "golfcart_msgs/msg/battery_state.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace golfcart
{

// Safety state machine.
enum class SafetyState
{
  SAFE_STOPPED,
  READY,
  MOVING,
  LIMITED,
  STOPPING,
  FAULT,
};

std::string to_string(SafetyState s)
{
  switch (s) {
    case SafetyState::SAFE_STOPPED: return "SAFE_STOPPED";
    case SafetyState::READY: return "READY";
    case SafetyState::MOVING: return "MOVING";
    case SafetyState::LIMITED: return "LIMITED";
    case SafetyState::STOPPING: return "STOPPING";
    case SafetyState::FAULT: return "FAULT";
  }
  return "UNKNOWN";
}

// Safety Controller: the central authority for movement.
// Receives MotionRequest, applies limits, and outputs SafeMotionCommand.
// A fault or stop request forces a safe stop.
class SafetyControllerNode : public rclcpp::Node
{
public:
  SafetyControllerNode()
  : Node("safety_controller")
  {
    max_linear_ = declare_parameter<double>("max_linear_velocity_mps", 1.0);
    max_angular_ = declare_parameter<double>("max_angular_velocity_radps", 1.0);

    req_sub_ = create_subscription<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotionRequest::SharedPtr msg) {
        handle_request(msg);
      });

    battery_sub_ = create_subscription<golfcart_msgs::msg::BatteryState>(
      "battery/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::BatteryState::SharedPtr msg) {
        battery_critical_ = msg->valid && msg->charge_percent <= 0.0f;
      });

    enable_srv_ = create_service<std_srvs::srv::Trigger>(
      "safety/enable",
      [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
             std::shared_ptr<std_srvs::srv::Trigger::Response> resp) {
        if (state_ == SafetyState::FAULT) {
          resp->success = false;
          resp->message = "Cannot enable from FAULT";
          return;
        }
        state_ = SafetyState::READY;
        resp->success = true;
        resp->message = "Enabled";
      });

    stop_srv_ = create_service<std_srvs::srv::Trigger>(
      "safety/stop",
      [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
             std::shared_ptr<std_srvs::srv::Trigger::Response> resp) {
        state_ = SafetyState::SAFE_STOPPED;
        publish_safe(0.0, 0.0);
        resp->success = true;
        resp->message = "Stopped";
      });

    fault_srv_ = create_service<std_srvs::srv::Trigger>(
      "safety/fault",
      [this](const std::shared_ptr<std_srvs::srv::Trigger::Request>,
             std::shared_ptr<std_srvs::srv::Trigger::Response> resp) {
        state_ = SafetyState::FAULT;
        publish_safe(0.0, 0.0);
        resp->success = true;
        resp->message = "Fault";
      });

    safe_pub_ = create_publisher<geometry_msgs::msg::Twist>(
      "motion/safe_command", rclcpp::SensorDataQoS());

    state_pub_ = create_publisher<std_msgs::msg::String>(
      "safety/state", rclcpp::SensorDataQoS());

    const double rate_hz = declare_parameter<double>("control_rate_hz", 50.0);
    request_timeout_s_ = declare_parameter<double>("request_timeout_s", 0.5);

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() {
        // Critical battery: force a safe stop.
        if (battery_critical_) {
          if (state_ == SafetyState::MOVING || state_ == SafetyState::LIMITED) {
            state_ = SafetyState::READY;
            publish_safe(0.0, 0.0);
          }
        }
        // If no motion request has arrived recently, stop.
        if (state_ == SafetyState::MOVING || state_ == SafetyState::LIMITED) {
          const double age = (now() - last_request_time_).seconds();
          if (age > request_timeout_s_) {
            state_ = SafetyState::READY;
            publish_safe(0.0, 0.0);
          }
        }
        std_msgs::msg::String msg;
        msg.data = to_string(state_);
        state_pub_->publish(msg);
      });
  }

private:
  SafetyState state_ = SafetyState::SAFE_STOPPED;
  double max_linear_ = 1.0;
  double max_angular_ = 1.0;
  double request_timeout_s_ = 0.5;
  bool battery_critical_ = false;
  rclcpp::Time last_request_time_;

  rclcpp::Subscription<golfcart_msgs::msg::MotionRequest>::SharedPtr req_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::BatteryState>::SharedPtr battery_sub_;
  rclcpp::Publisher<geometry_msgs::msg::Twist>::SharedPtr safe_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr state_pub_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr enable_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_srv_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr fault_srv_;
  rclcpp::TimerBase::SharedPtr timer_;

  void handle_request(const golfcart_msgs::msg::MotionRequest::SharedPtr msg)
  {
    last_request_time_ = now();
    switch (state_) {
      case SafetyState::FAULT:
      case SafetyState::SAFE_STOPPED:
        // Not enabled: no motion.
        publish_safe(0.0, 0.0);
        return;
      case SafetyState::READY:
      case SafetyState::MOVING:
      case SafetyState::LIMITED:
      case SafetyState::STOPPING:
        break;
    }

    // Apply motion limits.
    double linear = msg->linear_velocity_mps;
    double angular = msg->angular_velocity_radps;
    linear = std::clamp(linear, -max_linear_, max_linear_);
    angular = std::clamp(angular, -max_angular_, max_angular_);

    if (std::abs(linear) < 1e-6 && std::abs(angular) < 1e-6) {
      state_ = SafetyState::READY;
    } else {
      state_ = SafetyState::MOVING;
    }

    publish_safe(linear, angular);
  }

  void publish_safe(double linear, double angular)
  {
    geometry_msgs::msg::Twist msg;
    msg.linear.x = linear;
    msg.angular.z = angular;
    safe_pub_->publish(msg);
  }
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SafetyControllerNode>());
  rclcpp::shutdown();
  return 0;
}
