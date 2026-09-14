#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/handle_force.hpp"
#include "golfcart_msgs/msg/imu_data.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

namespace golfcart
{

// Push Assist (pedelec-style) behavior node.
//
// Detects how hard the user pushes (or brakes) the handle via the load cell
// in the handle unit, and requests proportional motor assistance — the same
// principle as an electric bike. The motor amplifies the user's input rather
// than driving independently.
//
//   handle/force (HandleForce)  →  calibrated push/brake force (N)
//   imu/data (ImuData)          →  pitch for slope compensation
//        ↓
//   user_force_effective = force - gravity_component(pitch)
//   assist = user_force_effective × assist_gain   (clamped, dead-zoned)
//        ↓
//   motion/request (MotionRequest, priority 2)
//
// Priority 2 sits above manual teleop (priority 1) so assist can override the
// joystick, but below safety behaviors (priority 3) like rollback/hill brake.
class PushAssistNode : public rclcpp::Node
{
public:
  PushAssistNode()
  : Node("push_assist_node")
  {
    // Parameters.
    assist_gain_ = declare_parameter<double>("assist_gain", 0.5);       // m/s per N
    max_assist_mps_ = declare_parameter<double>("max_assist_mps", 1.0); // clamp
    deadzone_n_ = declare_parameter<double>("deadzone_n", 2.0);         // N
    hysteresis_n_ = declare_parameter<double>("hysteresis_n", 0.5);     // N
    slope_comp_gain_ = declare_parameter<double>("slope_comp_gain", 0.5);  // m/s per rad of pitch
    brake_gain_ = declare_parameter<double>("brake_gain", 0.3);         // m/s per N (negative)

    // Subscriptions.
    force_sub_ = create_subscription<golfcart_msgs::msg::HandleForce>(
      "handle/force", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::HandleForce::SharedPtr msg) {
        if (msg->valid) {
          force_n_ = msg->force_n;
        }
      });

    imu_sub_ = create_subscription<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ImuData::SharedPtr msg) {
        if (msg->valid) {
          pitch_rad_ = msg->pitch_rad;
        }
      });

    // Publisher for assist motion requests.
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
  // Compute the effective user force after slope compensation.
  // On an uphill slope (positive pitch), gravity pulls the cart back, so the
  // user must push harder; subtract the gravity component so assist is based
  // on *user* effort, not the hill.
  double effective_force() const
  {
    // Positive pitch = nose up = uphill. Gravity component along the slope.
    double gravity_comp = slope_comp_gain_ * std::sin(pitch_rad_);
    return force_n_ - gravity_comp;
  }

  void update()
  {
    double f = effective_force();

    // --- Dead zone + hysteresis ---
    // No assist below the dead zone (avoid noise-triggered assist). Use
    // hysteresis to prevent rapid on/off switching around the threshold.
    bool active = assist_active_;
    if (!active && f > deadzone_n_ + hysteresis_n_) {
      active = true;
    } else if (active && f < deadzone_n_ - hysteresis_n_) {
      active = false;
    }
    assist_active_ = active;

    if (!active) {
      // No assist. If the user is braking (negative force), apply a brake.
      if (f < -deadzone_n_) {
        double brake = std::max(f * brake_gain_, -max_assist_mps_);
        publish_request(brake, 0.0, "push_brake", 2);
        publish_status("PUSH_BRAKE");
      } else {
        publish_request(0.0, 0.0, "push_assist", 2);
        publish_status("IDLE");
      }
      return;
    }

    // --- Proportional assist ---
    double assist = f * assist_gain_;
    // Clamp to max assist.
    if (assist > max_assist_mps_) {
      assist = max_assist_mps_;
    }
    publish_request(assist, 0.0, "push_assist", 2);
    publish_status("PUSH_ASSIST");
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
    // Reuse the behavior/status topic for diagnostics.
    auto pub = status_pub_;
    if (pub == nullptr) {
      return;
    }
    std_msgs::msg::String msg;
    msg.data = status;
    pub->publish(msg);
  }

  double assist_gain_ = 0.5;
  double max_assist_mps_ = 1.0;
  double deadzone_n_ = 2.0;
  double hysteresis_n_ = 0.5;
  double slope_comp_gain_ = 0.5;
  double brake_gain_ = 0.3;

  double force_n_ = 0.0;
  double pitch_rad_ = 0.0;
  bool assist_active_ = false;

  rclcpp::Subscription<golfcart_msgs::msg::HandleForce>::SharedPtr force_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::ImuData>::SharedPtr imu_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr req_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::PushAssistNode>());
  rclcpp::shutdown();
  return 0;
}