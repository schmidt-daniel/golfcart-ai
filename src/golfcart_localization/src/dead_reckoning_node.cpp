#include <memory>
#include <string>

#include "golfcart_msgs/msg/dead_reckoning_status.hpp"
#include "golfcart_msgs/msg/gps_fix.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "golfcart_msgs/msg/navigation_status.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

#include "dead_reckoning_math.hpp"

namespace golfcart
{

// Dead-Reckoning Fallback node (policy).
// Watches the localization quality (/localization/quality) and, when GPS is
// lost (LOST), allows the trolley to keep driving on the fused pose for a
// bounded time/distance (the dead-reckoning budget). Beyond the budget it
// publishes a priority-3 zero MotionRequest so the Safety Controller stops.
//
// States (published on /dead_reckoning/status):
//   OK                 - GPS healthy, no DR.
//   DEGRADED           - covariance high; warn operator, keep driving.
//   DRIVING_DR         - GPS lost; driving on dead reckoning within budget.
//   DR_BUDGET_EXCEEDED - budget exhausted; safe-stop requested.
//
// The geofence OUT_OF_FIX stop remains as a backstop if this node is absent.
class DeadReckoningNode : public rclcpp::Node
{
public:
  DeadReckoningNode()
  : Node("dead_reckoning_node")
  {
    max_dr_time_s_ = declare_parameter<double>("max_dr_time_s", 30.0);
    max_dr_distance_m_ = declare_parameter<double>("max_dr_distance_m", 50.0);
    stop_priority_ = declare_parameter<uint8_t>("stop_priority", 3);

    budget_ = DeadReckoningBudget(max_dr_time_s_, max_dr_distance_m_);

    quality_sub_ = create_subscription<golfcart_msgs::msg::NavigationStatus>(
      "localization/quality", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::NavigationStatus::SharedPtr msg) {
        handle_quality(msg);
      });

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        handle_odom(msg);
      });

    gps_sub_ = create_subscription<golfcart_msgs::msg::GpsFix>(
      "gps/fix", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::GpsFix::SharedPtr msg) {
        handle_gps(msg);
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::DeadReckoningStatus>(
      "dead_reckoning/status", rclcpp::SensorDataQoS());
    motion_pub_ = create_publisher<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS());

    status_timer_ = create_wall_timer(
      std::chrono::milliseconds(200),
      [this]() { publish_status(); });
  }

private:
  void handle_quality(const golfcart_msgs::msg::NavigationStatus::SharedPtr msg)
  {
    quality_state_ = msg->state;
    if (quality_state_ == "OK") {
      // GPS healthy again: reset the DR budget.
      budget_.reset();
      dr_active_ = false;
      stop_sent_ = false;
    } else if (quality_state_ == "LOST") {
      if (!dr_active_) {
        // Entering dead reckoning.
        dr_active_ = true;
        budget_.reset();
        stop_sent_ = false;
      }
    }
    // DEGRADED: keep driving, warn operator (no DR budget consumed).
  }

  void handle_odom(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    if (!dr_active_) {
      return;
    }
    const auto & p = msg->pose.pose.position;
    const double dt = (now() - last_odom_time_).seconds();
    last_odom_time_ = now();
    budget_.update(Pose2D{p.x, p.y, 0.0}, dt);

    if (budget_.exceeded() && !stop_sent_) {
      stop_sent_ = true;
      publish_stop();
    }
  }

  void handle_gps(const golfcart_msgs::msg::GpsFix::SharedPtr msg)
  {
    if (msg->valid) {
      // A valid fix means GPS is back; the quality node will flip to OK.
      // Reset here too so we don't keep a stale DR session.
      budget_.reset();
      dr_active_ = false;
      stop_sent_ = false;
    }
  }

  void publish_stop()
  {
    golfcart_msgs::msg::MotionRequest req;
    req.linear_velocity_mps = 0.0f;
    req.angular_velocity_radps = 0.0f;
    req.source = "dead_reckoning_node";
    req.priority = stop_priority_;
    req.timestamp = now();
    motion_pub_->publish(req);
    RCLCPP_WARN(get_logger(), "Dead-reckoning budget exceeded; requesting stop");
  }

  void publish_status()
  {
    golfcart_msgs::msg::DeadReckoningStatus msg;
    msg.timestamp = now();
    msg.budget_time_s = max_dr_time_s_;
    msg.budget_distance_m = max_dr_distance_m_;
    msg.used_time_s = budget_.used_time_s();
    msg.used_distance_m = budget_.used_distance_m();
    msg.remaining_time_s = budget_.remaining_time_s();
    msg.remaining_distance_m = budget_.remaining_distance_m();

    if (quality_state_ == "OK") {
      msg.state = "OK";
    } else if (quality_state_ == "DEGRADED") {
      msg.state = "DEGRADED";
    } else if (dr_active_ && budget_.exceeded()) {
      msg.state = "DR_BUDGET_EXCEEDED";
    } else if (dr_active_) {
      msg.state = "DRIVING_DR";
    } else {
      msg.state = "OK";
    }
    status_pub_->publish(msg);
  }

  double max_dr_time_s_ = 30.0;
  double max_dr_distance_m_ = 50.0;
  uint8_t stop_priority_ = 3;
  std::string quality_state_ = "OK";
  bool dr_active_ = false;
  bool stop_sent_ = false;
  rclcpp::Time last_odom_time_;
  DeadReckoningBudget budget_;

  rclcpp::Subscription<golfcart_msgs::msg::NavigationStatus>::SharedPtr quality_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::GpsFix>::SharedPtr gps_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::DeadReckoningStatus>::SharedPtr status_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr motion_pub_;
  rclcpp::TimerBase::SharedPtr status_timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::DeadReckoningNode>());
  rclcpp::shutdown();
  return 0;
}