#include <memory>
#include <string>

#include "golfcart_msgs/msg/motor_command.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "golfcart_odrive/mock_motor_controller.hpp"
#include "golfcart_odrive/motor_controller.hpp"
#include "golfcart_odrive/odrive_motor_controller.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// ROS 2 node that wraps a MotorController (ODrive or mock).
// Subscribes to /motor/command and publishes /motor/state.
class ODriveNode : public rclcpp::Node
{
public:
  ODriveNode()
  : Node("odrive_node")
  {
    const std::string impl = declare_parameter<std::string>("implementation", "mock");
    const std::string device = declare_parameter<std::string>("device", "/dev/ttyUSB0");

    if (impl == "mock") {
      mock_ = std::make_shared<MockMotorController>();
      controller_ = mock_;
    } else if (impl == "odrive") {
      controller_ = std::make_shared<ODriveMotorController>(device);
    } else {
      RCLCPP_FATAL(get_logger(), "Unknown implementation '%s'", impl.c_str());
      throw std::runtime_error("Unknown motor controller implementation");
    }

    cmd_sub_ = create_subscription<golfcart_msgs::msg::MotorCommand>(
      "motor/command", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorCommand::SharedPtr msg) {
        controller_->command_velocity(msg->left_motor_velocity, msg->right_motor_velocity);
      });

    state_pub_ = create_publisher<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS());

    timer_ = create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        if (mock_) {
          mock_->update(0.05);
        }
        publish_state();
      });
  }

private:
  void publish_state()
  {
    double left_v = 0.0, right_v = 0.0, left_p = 0.0, right_p = 0.0;
    controller_->get_velocity(left_v, right_v);
    controller_->get_position(left_p, right_p);

    golfcart_msgs::msg::MotorState msg;
    msg.left_velocity = left_v;
    msg.right_velocity = right_v;
    msg.left_position = left_p;
    msg.right_position = right_p;
    msg.state = controller_->get_state();
    msg.fault = controller_->get_fault();
    msg.timestamp = now();
    state_pub_->publish(msg);
  }

  std::shared_ptr<MotorController> controller_;
  std::shared_ptr<MockMotorController> mock_;
  rclcpp::Subscription<golfcart_msgs::msg::MotorCommand>::SharedPtr cmd_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotorState>::SharedPtr state_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::ODriveNode>());
  rclcpp::shutdown();
  return 0;
}
