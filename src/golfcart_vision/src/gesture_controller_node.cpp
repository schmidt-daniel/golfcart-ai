#include <memory>

#include "golfcart_msgs/msg/gesture_command.hpp"
#include "golfcart_msgs/msg/mode_state.hpp"
#include "golfcart_msgs/srv/summon_trigger.hpp"
#include "golfcart_vision/gesture_math.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_srvs/srv/trigger.hpp"

namespace golfcart
{

// Mode constants (must match mode_node / ModeState.msg).
constexpr uint8_t MODE_MANUAL = 0;
constexpr uint8_t MODE_FOLLOW = 1;
constexpr uint8_t MODE_AUTONOMOUS = 2;
constexpr uint8_t MODE_TELEOP = 3;

// Gesture Controller node.
// Consumes recognized gestures from /gesture/command and maps them to actions
// on the existing command paths:
//   WINDMILL -> summon (requires windmill + hold confirmation)
//   STOP     -> safety stop request
//   FOLLOW   -> set mode to FOLLOW
//   SLOW     -> publish a temporary speed limit
//
// Gestures are advisory — they never bypass the Safety Controller. A STOP
// gesture is a request; the Safety Controller still arbitrates. Gestures are
// ignored while the operator is actively driving (MANUAL mode with joystick).
class GestureControllerNode : public rclcpp::Node
{
public:
  GestureControllerNode()
  : Node("gesture_controller_node")
  {
    enabled_ = declare_parameter<bool>("enabled", true);
    slow_speed_mps_ = declare_parameter<double>("slow_speed_mps", 0.5);
    ignore_in_manual_ = declare_parameter<bool>("ignore_in_manual", true);
    summon_hold_s_ = declare_parameter<double>("summon_hold_s", 1.0);

    gesture_sub_ = create_subscription<golfcart_msgs::msg::GestureCommand>(
      "gesture/command", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::GestureCommand::SharedPtr msg) {
        on_gesture(msg);
      });

    mode_sub_ = create_subscription<golfcart_msgs::msg::ModeState>(
      "mode/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ModeState::SharedPtr msg) {
        mode_ = msg->mode;
      });

    summon_client_ = create_client<golfcart_msgs::srv::SummonTrigger>("summon");
    safety_stop_client_ = create_client<std_srvs::srv::Trigger>("safety/stop");

    mode_pub_ = create_publisher<golfcart_msgs::msg::ModeState>(
      "mode/state", rclcpp::SensorDataQoS());
    speed_limit_pub_ = create_publisher<golfcart_msgs::msg::ModeState>(
      "gesture/speed_limit", rclcpp::SensorDataQoS());
  }

private:
  void on_gesture(const golfcart_msgs::msg::GestureCommand::SharedPtr msg)
  {
    if (!enabled_) {
      return;
    }
    // Ignore while the operator is actively driving in MANUAL mode.
    if (ignore_in_manual_ && mode_ == MODE_MANUAL) {
      return;
    }

    switch (msg->gesture) {
      case GESTURE_SUMMON:
        handle_windmill();
        break;
      case GESTURE_STOP:
        request_stop();
        break;
      case GESTURE_FOLLOW:
        set_mode(MODE_FOLLOW);
        break;
      case GESTURE_SLOW:
        publish_slow();
        break;
      default:
        break;
    }
  }

  // SUMMON requires windmill + hold: the windmill must persist for
  // summon_hold_s before triggering summon.
  void handle_windmill()
  {
    const double dt = (now() - windmill_since_).seconds();
    if (windmill_since_ == rclcpp::Time(0)) {
      windmill_since_ = now();
      return;
    }
    if (dt >= summon_hold_s_) {
      trigger_summon();
      windmill_since_ = rclcpp::Time(0);
    }
  }

  void trigger_summon()
  {
    if (!summon_client_->service_is_ready()) {
      RCLCPP_WARN(this->get_logger(), "summon service not ready");
      return;
    }
    auto req = std::make_shared<golfcart_msgs::srv::SummonTrigger::Request>();
    req->lat = 0.0;
    req->lon = 0.0;
    req->mode = "CURRENT";
    req->cancel = false;
    summon_client_->async_send_request(req);
  }

  void request_stop()
  {
    if (!safety_stop_client_->service_is_ready()) {
      RCLCPP_WARN(this->get_logger(), "safety/stop service not ready");
      return;
    }
    auto req = std::make_shared<std_srvs::srv::Trigger::Request>();
    safety_stop_client_->async_send_request(req);
  }

  void set_mode(uint8_t mode)
  {
    golfcart_msgs::msg::ModeState msg;
    msg.mode = mode;
    msg.mode_name = mode == MODE_FOLLOW ? "FOLLOW" : "AUTONOMOUS";
    mode_pub_->publish(msg);
  }

  void publish_slow()
  {
    // Publish a temporary speed limit on /gesture/speed_limit. The Safety
    // Controller consumes this to cap speed. (Reuses ModeState as a simple
    // carrier for the limit value in the mode field for now.)
    golfcart_msgs::msg::ModeState msg;
    msg.mode = static_cast<uint8_t>(slow_speed_mps_ * 100.0);
    msg.mode_name = "SLOW";
    speed_limit_pub_->publish(msg);
  }

  bool enabled_ = true;
  double slow_speed_mps_ = 0.5;
  bool ignore_in_manual_ = true;
  double summon_hold_s_ = 1.0;
  uint8_t mode_ = MODE_MANUAL;
  rclcpp::Time windmill_since_;

  rclcpp::Subscription<golfcart_msgs::msg::GestureCommand>::SharedPtr gesture_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::ModeState>::SharedPtr mode_sub_;
  rclcpp::Client<golfcart_msgs::srv::SummonTrigger>::SharedPtr summon_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr safety_stop_client_;
  rclcpp::Publisher<golfcart_msgs::msg::ModeState>::SharedPtr mode_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::ModeState>::SharedPtr speed_limit_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::GestureControllerNode>());
  rclcpp::shutdown();
  return 0;
}