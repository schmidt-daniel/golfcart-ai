#include <cmath>
#include <fstream>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/battery_state.hpp"
#include "golfcart_msgs/msg/hole_session.hpp"
#include "golfcart_msgs/msg/range_status.hpp"
#include "golfcart_msgs/msg/slope_status.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

#include "range_estimator_math.hpp"

namespace golfcart
{

// Battery Range Estimator node.
// Estimates the trolley's remaining range from battery charge, terrain slope,
// and remaining hole distance, and warns the operator if it may not make it
// back. The energy model (Wh/m per slope bucket) LEARNS from measured battery
// current over time via an online EMA, so it improves with every round.
//
// Publishes:
//   /range/status  (golfcart_msgs/RangeStatus)
//     state = OK / CAUTION / CRITICAL
//
// Informational only - never commands motion.
class RangeEstimatorNode : public rclcpp::Node
{
public:
  RangeEstimatorNode()
  : Node("range_estimator_node")
  {
    battery_capacity_wh_ = declare_parameter<double>("battery_capacity_wh", 500.0);
    reserve_wh_ = declare_parameter<double>("reserve_wh", 50.0);
    return_margin_m_ = declare_parameter<double>("return_margin_m", 50.0);
    ema_alpha_ = declare_parameter<double>("ema_alpha", 0.3);
    update_period_s_ = declare_parameter<double>("update_period_s", 5.0);
    default_wh_per_m_ = declare_parameter<double>("default_wh_per_m", 0.02);
    model_file_ = declare_parameter<std::string>("model_file", "/var/lib/golfcart/range_model.txt");

    model_ = EnergyModel(default_wh_per_m_, ema_alpha_);
    load_model();

    battery_sub_ = create_subscription<golfcart_msgs::msg::BatteryState>(
      "battery/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::BatteryState::SharedPtr msg) {
        handle_battery(msg);
      });

    slope_sub_ = create_subscription<golfcart_msgs::msg::SlopeStatus>(
      "slope/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::SlopeStatus::SharedPtr msg) {
        if (msg->valid) {
          slope_deg_ = msg->slope_deg;
        }
      });

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        handle_odom(msg);
      });

    hole_sub_ = create_subscription<golfcart_msgs::msg::HoleSession>(
      "course/hole", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::HoleSession::SharedPtr msg) {
        remaining_m_ = msg->remaining_m;
        hole_distance_m_ = msg->distance_m;
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::RangeStatus>(
      "range/status", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(update_period_s_);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { update(); });
  }

private:
  void handle_battery(const golfcart_msgs::msg::BatteryState::SharedPtr msg)
  {
    if (!msg->valid) {
      return;
    }
    voltage_v_ = msg->voltage_v;
    current_a_ = msg->current_a;
    charge_percent_ = msg->charge_percent;
    battery_valid_ = true;
  }

  void handle_odom(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    const auto & p = msg->pose.pose.position;
    const double dt = (now() - last_odom_time_).seconds();
    last_odom_time_ = now();

    if (last_odom_valid_) {
      const double dx = p.x - last_pose_x_;
      const double dy = p.y - last_pose_y_;
      const double dist = std::hypot(dx, dy);
      window_distance_m_ += dist;

      // Energy = V * A * dt (Wh). Only count while moving to avoid idle draw.
      if (battery_valid_ && dist > 0.01) {
        window_energy_wh_ += voltage_v_ * current_a_ * dt / 3600.0;
      }
    }
    last_pose_x_ = p.x;
    last_pose_y_ = p.y;
    last_odom_valid_ = true;
  }

  void update()
  {
    // Learn from the accumulated window.
    if (window_distance_m_ > 1.0) {
      model_.learn(slope_deg_, window_energy_wh_, window_distance_m_);
    }
    window_energy_wh_ = 0.0;
    window_distance_m_ = 0.0;

    // Persist the learned model so it survives reboots (multi-round learning).
    save_model();

    golfcart_msgs::msg::RangeStatus msg;
    msg.timestamp = now();

    if (!battery_valid_) {
      msg.state = "OK";
      msg.range_m = 0.0;
      msg.remaining_m = remaining_m_;
      msg.return_m = 0.0;
      msg.can_finish = false;
      msg.wh_per_m = model_.flat_wh_per_m();
      status_pub_->publish(msg);
      return;
    }

    // Usable energy = charge% * capacity - reserve.
    const double usable_wh =
      std::max(0.0, battery_capacity_wh_ * (charge_percent_ / 100.0) - reserve_wh_);

    const double range_m = estimate_range_m(model_, usable_wh, slope_deg_);

    // Return distance: assume the trolley must drive back the same distance it
    // came (use the hole distance as a proxy for the round trip).
    const double return_m = remaining_m_ > 0.0 ? remaining_m_ : hole_distance_m_;

    msg.range_m = range_m;
    msg.remaining_m = remaining_m_;
    msg.return_m = return_m;
    msg.can_finish = range_m >= remaining_m_ + return_m;
    msg.wh_per_m = model_.flat_wh_per_m();
    msg.state = range_state(range_m, remaining_m_, return_m, return_margin_m_);

    status_pub_->publish(msg);
  }

  // Load the learned model from disk (multi-round persistence). On failure,
  // the model keeps its default values.
  void load_model()
  {
    std::ifstream f(model_file_);
    if (!f.is_open()) {
      RCLCPP_INFO(get_logger(), "No saved range model at %s (using defaults)",
                  model_file_.c_str());
      return;
    }
    std::string line;
    std::getline(f, line);
    if (model_.deserialize(line)) {
      RCLCPP_INFO(get_logger(), "Loaded range model: %s", line.c_str());
    } else {
      RCLCPP_WARN(get_logger(), "Failed to parse saved range model");
    }
  }

  // Save the learned model to disk.
  void save_model()
  {
    // Ensure the directory exists.
    const std::size_t slash = model_file_.rfind('/');
    if (slash != std::string::npos) {
      const std::string dir = model_file_.substr(0, slash);
      std::string cmd = "mkdir -p " + dir;
      if (system(cmd.c_str()) != 0) {
        RCLCPP_WARN(get_logger(), "Could not create model dir %s", dir.c_str());
      }
    }
    std::ofstream f(model_file_);
    if (!f.is_open()) {
      RCLCPP_WARN(get_logger(), "Could not write range model to %s",
                  model_file_.c_str());
      return;
    }
    f << model_.serialize() << "\n";
    f.close();
  }

  double battery_capacity_wh_ = 500.0;
  double reserve_wh_ = 50.0;
  double return_margin_m_ = 50.0;
  double ema_alpha_ = 0.3;
  double update_period_s_ = 5.0;
  double default_wh_per_m_ = 0.02;
  std::string model_file_ = "/var/lib/golfcart/range_model.txt";

  EnergyModel model_;

  // Battery.
  bool battery_valid_ = false;
  float voltage_v_ = 0.0f;
  float current_a_ = 0.0f;
  float charge_percent_ = 0.0f;

  // Slope.
  float slope_deg_ = 0.0f;

  // Course.
  double remaining_m_ = 0.0;
  double hole_distance_m_ = 0.0;

  // Learning window.
  double window_energy_wh_ = 0.0;
  double window_distance_m_ = 0.0;
  bool last_odom_valid_ = false;
  double last_pose_x_ = 0.0;
  double last_pose_y_ = 0.0;
  rclcpp::Time last_odom_time_;

  rclcpp::Subscription<golfcart_msgs::msg::BatteryState>::SharedPtr battery_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::SlopeStatus>::SharedPtr slope_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::HoleSession>::SharedPtr hole_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::RangeStatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::RangeEstimatorNode>());
  rclcpp::shutdown();
  return 0;
}