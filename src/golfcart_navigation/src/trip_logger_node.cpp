#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "golfcart_msgs/msg/battery_state.hpp"
#include "golfcart_msgs/msg/course_map.hpp"
#include "golfcart_msgs/msg/hole_session.hpp"
#include "golfcart_msgs/msg/trip_summary.hpp"
#include "golfcart_msgs/srv/end_round.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

#include "trip_math.hpp"

namespace golfcart
{

// Trip Logger node.
// Tracks a round (course + tee selection -> "End round") and logs a summary
// per round to a JSON file. Publishes /trip/summary for the current round.
//
// Round lifecycle:
//   - START: a course + tee is selected (HoleSession on /course/hole)
//   - END:   the /end_round service is called (HMI "End round" button)
//
// Logs distance, energy, duration, and average speed per round.
class TripLoggerNode : public rclcpp::Node
{
public:
  TripLoggerNode()
  : Node("trip_logger_node")
  {
    log_file_ = declare_parameter<std::string>("log_file", "/var/lib/golfcart/trips.json");

    // Round start: a course + tee is selected.
    hole_sub_ = create_subscription<golfcart_msgs::msg::HoleSession>(
      "course/hole", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::HoleSession::SharedPtr msg) {
        start_round(msg->tee_id);
      });

    // Course name + tee names (for the log).
    course_map_sub_ = create_subscription<golfcart_msgs::msg::CourseMap>(
      "course/map", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::CourseMap::SharedPtr msg) {
        course_name_ = msg->course_name;
        tee_ids_ = msg->tee_ids;
        tee_names_ = msg->tee_names;
      });

    // Distance: from odometry.
    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        on_odom(msg);
      });

    // Energy: from battery current.
    battery_sub_ = create_subscription<golfcart_msgs::msg::BatteryState>(
      "battery/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::BatteryState::SharedPtr msg) {
        on_battery(msg);
      });

    // End round service.
    end_srv_ = create_service<golfcart_msgs::srv::EndRound>(
      "end_round",
      [this](const std::shared_ptr<golfcart_msgs::srv::EndRound::Request>,
             std::shared_ptr<golfcart_msgs::srv::EndRound::Response> resp) {
        end_round();
        resp->success = true;
        resp->message = "Round ended";
      });

    summary_pub_ = create_publisher<golfcart_msgs::msg::TripSummary>(
      "trip/summary", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_summary(); });
  }

private:
  void start_round(const std::string & tee_id)
  {
    // A new course/tee selection starts a fresh round.
    if (tracker_.active()) {
      // End the previous round first.
      end_round();
    }
    tee_name_ = tee_name_for(tee_id);
    tracker_.start(now().seconds());
    RCLCPP_INFO(get_logger(), "Round started: %s / %s",
                course_name_.c_str(), tee_name_.c_str());
  }

  // Resolve a tee ID to its display name (fall back to the ID).
  std::string tee_name_for(const std::string & tee_id)
  {
    for (std::size_t i = 0; i < tee_ids_.size() && i < tee_names_.size(); ++i) {
      if (tee_ids_[i] == tee_id) {
        return tee_names_[i];
      }
    }
    return tee_id;
  }

  void on_odom(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    if (!tracker_.active()) {
      return;
    }
    const double x = msg->pose.pose.position.x;
    const double y = msg->pose.pose.position.y;
    const double dt = (now() - last_odom_time_).seconds();
    last_odom_time_ = now();
    if (last_odom_valid_) {
      const double dist = std::hypot(x - last_x_, y - last_y_);
      // Energy = V * A * dt / 3600 (Wh), only while moving.
      double energy = 0.0;
      if (battery_valid_ && dist > 0.01) {
        energy = voltage_v_ * current_a_ * dt / 3600.0;
      }
      tracker_.accumulate(dist, energy);
    }
    last_x_ = x;
    last_y_ = y;
    last_odom_valid_ = true;
  }

  void on_battery(const golfcart_msgs::msg::BatteryState::SharedPtr msg)
  {
    if (!msg->valid) {
      return;
    }
    voltage_v_ = msg->voltage_v;
    current_a_ = msg->current_a;
    battery_valid_ = true;
  }

  void end_round()
  {
    if (!tracker_.active()) {
      return;
    }
    const double duration_s = tracker_.end(now().seconds());
    const double dist = tracker_.distance_m();
    const double energy = tracker_.energy_wh();
    const double avg = tracker_.avg_speed_mps(duration_s);

    RCLCPP_INFO(get_logger(),
                "Round ended: %.1f m, %.1f Wh, %.0f s, %.2f m/s",
                dist, energy, duration_s, avg);

    // Append to the JSON log.
    append_log(course_name_, tee_name_, dist, energy, duration_s, avg);

    // Publish the final summary.
    publish_summary();
  }

  void append_log(const std::string & course, const std::string & tee,
                  double dist, double energy, double duration_s, double avg)
  {
    std::ofstream f(log_file_, std::ios::app);
    if (!f.is_open()) {
      RCLCPP_WARN(get_logger(), "Could not open trip log %s", log_file_.c_str());
      return;
    }
    // JSON Lines: one object per round.
    f << "{\"course\":\"" << course << "\",\"tee\":\"" << tee
      << "\",\"distance_m\":" << dist
      << ",\"energy_wh\":" << energy
      << ",\"duration_s\":" << duration_s
      << ",\"avg_speed_mps\":" << avg
      << ",\"ts\":" << now().seconds() << "}\n";
    f.close();
  }

  void publish_summary()
  {
    golfcart_msgs::msg::TripSummary msg;
    msg.active = tracker_.active();
    msg.course_name = course_name_;
    msg.tee_name = tee_name_;
    msg.distance_m = tracker_.distance_m();
    msg.energy_wh = tracker_.energy_wh();
    msg.duration_s = tracker_.active() ? (now().seconds() - 0.0) : 0.0;
    msg.avg_speed_mps = tracker_.avg_speed_mps(msg.duration_s);
    msg.timestamp = now();
    summary_pub_->publish(msg);
  }

  std::string log_file_ = "/var/lib/golfcart/trips.json";
  std::string course_name_ = "";
  std::string tee_name_ = "";
  std::vector<std::string> tee_ids_;
  std::vector<std::string> tee_names_;
  TripTracker tracker_;

  bool battery_valid_ = false;
  float voltage_v_ = 0.0f;
  float current_a_ = 0.0f;

  bool last_odom_valid_ = false;
  double last_x_ = 0.0;
  double last_y_ = 0.0;
  rclcpp::Time last_odom_time_;

  rclcpp::Subscription<golfcart_msgs::msg::HoleSession>::SharedPtr hole_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::CourseMap>::SharedPtr course_map_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::BatteryState>::SharedPtr battery_sub_;
  rclcpp::Service<golfcart_msgs::srv::EndRound>::SharedPtr end_srv_;
  rclcpp::Publisher<golfcart_msgs::msg::TripSummary>::SharedPtr summary_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::TripLoggerNode>());
  rclcpp::shutdown();
  return 0;
}