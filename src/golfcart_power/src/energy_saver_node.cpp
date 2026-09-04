#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/imu_data.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "golfcart_msgs/msg/power_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"

namespace golfcart
{

// Energy-saving mode.
//
// A golf cart stands still a lot during a round (waiting, on the green, during
// a swing). Not all sensors/systems need to run while stationary. This node:
//
//   1. Monitors activity (IMU motion, wheel motion, safety state, joystick).
//   2. After the cart has been idle for `sleep_timeout_s`, enters SLEEP mode:
//        - publishes PowerState=SLEEP on /power/state
//        - publishes a "sleep" command on /power/sleep_cmd so other nodes can
//          reduce their rates / suspend non-essential work.
//   3. The IMU stays active. When it detects motion (or any activity arrives),
//      it wakes back to ACTIVE and publishes PowerState=ACTIVE.
//
// Safety: sleep is SUPPRESSED when there is a risk of rolling away (on a slope
// or moving), mirroring auto_shutdown_node. The cart must never sleep while it
// could roll uncontrolled.
class EnergySaverNode : public rclcpp::Node
{
public:
  EnergySaverNode()
  : Node("energy_saver_node")
  {
    sleep_timeout_s_ = declare_parameter<double>("sleep_timeout_s", 60.0);
    wake_motion_threshold_mps_ = declare_parameter<double>("wake_motion_threshold_mps", 0.02);
    max_slope_rad_ = declare_parameter<double>("max_slope_rad", 0.15);  // ~8.6 deg
    motion_threshold_mps_ = declare_parameter<double>("motion_threshold_mps", 0.05);

    last_activity_ = now();
    state_ = State::ACTIVE;

    // Subscriptions.
    imu_sub_ = create_subscription<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ImuData::SharedPtr msg) {
        if (!msg->valid) {
          return;
        }
        inclination_ = std::sqrt(
          msg->roll_rad * msg->roll_rad + msg->pitch_rad * msg->pitch_rad);

        // In SLEEP, the IMU is the wake trigger: any change in inclination
        // (motion) wakes the cart. We compare against the last stored value.
        if (state_ == State::SLEEP) {
          const double d = std::fabs(inclination_ - last_inclination_);
          if (d > wake_motion_threshold_rad_) {
            RCLCPP_INFO(get_logger(), "IMU motion detected (d=%.4f rad) - waking", d);
            wake();
          }
        }
        last_inclination_ = inclination_;
      });

    motor_sub_ = create_subscription<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorState::SharedPtr msg) {
        const double speed = (std::abs(msg->left_velocity) + std::abs(msg->right_velocity)) / 2.0;
        if (speed > motion_threshold_mps_) {
          on_activity();
        }
      });

    safety_sub_ = create_subscription<std_msgs::msg::String>(
      "safety/state", rclcpp::SensorDataQoS(),
      [this](const std_msgs::msg::String::SharedPtr msg) {
        if (msg->data != "SAFE_STOPPED") {
          on_activity();
        }
      });

    // Publishers.
    state_pub_ = create_publisher<golfcart_msgs::msg::PowerState>(
      "power/state", rclcpp::SensorDataQoS());
    sleep_cmd_pub_ = create_publisher<std_msgs::msg::String>(
      "power/sleep_cmd", rclcpp::SensorDataQoS());

    // Check timer (1 Hz).
    timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { check(); });

    publish_state();
  }

private:
  enum State { ACTIVE, SLEEP, WAKING };

  bool at_roll_away_risk() const
  {
    return inclination_ > max_slope_rad_;
  }

  void on_activity()
  {
    last_activity_ = now();
    if (state_ != State::ACTIVE) {
      wake();
    }
  }

  void wake()
  {
    if (state_ == State::ACTIVE) {
      return;
    }
    state_ = State::WAKING;
    publish_state();
    // Publish a wake command so other nodes resume full operation.
    std_msgs::msg::String cmd;
    cmd.data = "wake";
    sleep_cmd_pub_->publish(cmd);
    state_ = State::ACTIVE;
    last_activity_ = now();
    publish_state();
    RCLCPP_INFO(get_logger(), "Woke up - back to ACTIVE");
  }

  void sleep()
  {
    state_ = State::SLEEP;
    publish_state();
    std_msgs::msg::String cmd;
    cmd.data = "sleep";
    sleep_cmd_pub_->publish(cmd);
    RCLCPP_WARN(get_logger(), "Entering SLEEP (energy-saving) mode");
  }

  void check()
  {
    // Never sleep while at roll-away risk.
    if (at_roll_away_risk()) {
      if (state_ != State::ACTIVE) {
        RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 5000,
          "Roll-away risk (slope %.2f rad) - staying awake", inclination_);
        wake();
      }
      return;
    }

    if (state_ == State::ACTIVE) {
      const double idle_age = (now() - last_activity_).seconds();
      if (idle_age >= sleep_timeout_s_) {
        sleep();
      }
    }
  }

  void publish_state()
  {
    golfcart_msgs::msg::PowerState msg;
    switch (state_) {
      case State::ACTIVE: msg.state = "ACTIVE"; break;
      case State::SLEEP:  msg.state = "SLEEP";  break;
      case State::WAKING: msg.state = "WAKING"; break;
    }
    msg.timestamp = now();
    state_pub_->publish(msg);
  }

  double sleep_timeout_s_ = 60.0;
  double wake_motion_threshold_mps_ = 0.02;
  double wake_motion_threshold_rad_ = 0.01;  // ~0.57 deg inclination change
  double max_slope_rad_ = 0.15;
  double motion_threshold_mps_ = 0.05;
  double inclination_ = 0.0;
  double last_inclination_ = 0.0;
  rclcpp::Time last_activity_;
  State state_ = State::ACTIVE;

  rclcpp::Subscription<golfcart_msgs::msg::ImuData>::SharedPtr imu_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::MotorState>::SharedPtr motor_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr safety_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::PowerState>::SharedPtr state_pub_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr sleep_cmd_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::EnergySaverNode>());
  rclcpp::shutdown();
  return 0;
}