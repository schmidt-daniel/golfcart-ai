#include <memory>
#include <string>

#include "golfcart_msgs/msg/capability_status.hpp"
#include "golfcart_msgs/msg/mode_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_srvs/srv/set_bool.hpp"

namespace golfcart
{

// Mode constants (must match the HMI protocol ST_MODE enum).
constexpr uint8_t MODE_MANUAL = 0;
constexpr uint8_t MODE_FOLLOW = 1;
constexpr uint8_t MODE_AUTONOMOUS = 2;
constexpr uint8_t MODE_TELEOP = 3;

const char * mode_name(uint8_t mode)
{
  switch (mode) {
    case MODE_MANUAL: return "MANUAL";
    case MODE_FOLLOW: return "FOLLOW";
    case MODE_AUTONOMOUS: return "AUTONOMOUS";
    case MODE_TELEOP: return "TELEOP";
    default: return "UNKNOWN";
  }
}

// Operating-mode node.
//
// Publishes the trolley's current operating mode on /mode/state (ModeState).
// The Safety Controller subscribes to this to decide which safety stops apply
// (e.g. obstacle hard-stop only when NOT in MANUAL mode). The HMI/web render
// the mode, and the handle gateway maps mode selections to this topic.
//
// The mode can be changed via the /mode/set service (SetBool: true = MANUAL,
// false = AUTONOMOUS) or by publishing a ModeState on /mode/state.
//
// Hardware gating: FOLLOW requires the horizontal LiDAR; AUTONOMOUS requires
// GPS. If the required capability is absent (see /capability/status), the mode
// is rejected and the trolley falls back to MANUAL. Manual always works.
class ModeNode : public rclcpp::Node
{
public:
  ModeNode()
  : Node("mode_node")
  {
    // Default mode: MANUAL (the operator is in control until they choose
    // otherwise).
    mode_ = declare_parameter<int>("default_mode", MODE_MANUAL);

    state_pub_ = create_publisher<golfcart_msgs::msg::ModeState>(
      "mode/state", rclcpp::SensorDataQoS());

    // Track hardware capabilities for mode gating.
    cap_sub_ = create_subscription<golfcart_msgs::msg::CapabilityStatus>(
      "capability/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg) {
        lidar_h_ = msg->lidar_horizontal;
        gps_ = msg->gps;
      });

    // Allow the mode to be set via a service (e.g. from the HMI/web).
    set_srv_ = create_service<std_srvs::srv::SetBool>(
      "mode/set",
      [this](const std::shared_ptr<std_srvs::srv::SetBool::Request> req,
             std::shared_ptr<std_srvs::srv::SetBool::Response> resp) {
        // true = MANUAL, false = AUTONOMOUS (a simple binary toggle for now;
        // the full enum is set by publishing ModeState directly).
        const uint8_t target = req->data ? MODE_MANUAL : MODE_AUTONOMOUS;
        if (!mode_allowed(target)) {
          // Fall back to MANUAL if the requested mode needs missing hardware.
          set_mode(MODE_MANUAL);
          resp->success = false;
          resp->message = "Mode unavailable (missing hardware)";
          return;
        }
        set_mode(target);
        resp->success = true;
        resp->message = mode_name(mode_);
      });

    // Publish the initial mode.
    publish_mode();
  }

private:
  // Whether a mode is allowed given the current hardware capabilities.
  bool mode_allowed(uint8_t mode)
  {
    switch (mode) {
      case MODE_FOLLOW:
        return lidar_h_;
      case MODE_AUTONOMOUS:
        return gps_;
      case MODE_MANUAL:
      case MODE_TELEOP:
        return true;
      default:
        return false;
    }
  }

  void set_mode(uint8_t mode)
  {
    mode_ = mode;
    publish_mode();
  }

  void publish_mode()
  {
    golfcart_msgs::msg::ModeState msg;
    msg.mode = mode_;
    msg.mode_name = mode_name(mode_);
    msg.timestamp = now();
    state_pub_->publish(msg);
  }

  uint8_t mode_ = MODE_MANUAL;
  bool lidar_h_ = false;
  bool gps_ = false;

  rclcpp::Publisher<golfcart_msgs::msg::ModeState>::SharedPtr state_pub_;
  rclcpp::Subscription<golfcart_msgs::msg::CapabilityStatus>::SharedPtr cap_sub_;
  rclcpp::Service<std_srvs::srv::SetBool>::SharedPtr set_srv_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::ModeNode>());
  rclcpp::shutdown();
  return 0;
}