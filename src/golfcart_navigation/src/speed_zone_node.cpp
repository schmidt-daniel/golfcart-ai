#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "golfcart_msgs/msg/course_map.hpp"
#include "golfcart_msgs/msg/course_selected.hpp"
#include "golfcart_msgs/msg/speed_zone_status.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

#include "course_zip.hpp"
#include "speed_zone_math.hpp"

namespace golfcart
{

// Speed zone node + course-aware speed governor.
// Monitors the trolley's fused pose against the active course's speed-limit
// zones (SPEED_ZONE features) AND its forbidden zones (greens, tees, water
// hazards, rough, bunkers) and publishes the active speed limit on
// /speed_zone/status for the Safety Controller.
//
// Semantics:
//   - Inside a SPEED_ZONE -> limit = that zone's max_speed_mps (most restrictive wins).
//   - Within slow_radius_m of a forbidden zone -> limit = feature_limit_mps.
//   - Outside all -> limit = -1 (no zone limit; use configured max).
//   - Stale/invalid pose -> conservative low limit (never speed up on unknown data).
class SpeedZoneNode : public rclcpp::Node
{
public:
  SpeedZoneNode()
  : Node("speed_zone_node")
  {
    pose_timeout_s_ = declare_parameter<double>("pose_timeout_s", 2.0);
    stale_limit_mps_ = declare_parameter<double>("stale_limit_mps", 0.3);
    slow_radius_m_ = declare_parameter<double>("slow_radius_m", 3.0);
    feature_limit_mps_ = declare_parameter<double>("feature_limit_mps", 0.5);

    selected_sub_ = create_subscription<golfcart_msgs::msg::CourseSelected>(
      "course/selected", rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal),
      [this](const golfcart_msgs::msg::CourseSelected::SharedPtr msg) { on_selected(msg); });
    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) { on_odom(msg); });

    status_pub_ = create_publisher<golfcart_msgs::msg::SpeedZoneStatus>(
      "speed_zone/status", rclcpp::SensorDataQoS());
  }

private:
  double pose_timeout_s_;
  double stale_limit_mps_;
  double slow_radius_m_;
  double feature_limit_mps_;
  rclcpp::Subscription<golfcart_msgs::msg::CourseSelected>::SharedPtr selected_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::SpeedZoneStatus>::SharedPtr status_pub_;

  // Active speed zones (map-frame polygons) + parallel limits/labels.
  std::vector<std::vector<MapPoint>> zones_;
  std::vector<double> limits_;
  std::vector<std::string> labels_;
  bool have_zones_ = false;

  // Forbidden zones (greens, tees, water, rough, bunker) + parallel types/labels.
  std::vector<std::vector<MapPoint>> forbidden_;
  std::vector<std::string> forbidden_types_;
  std::vector<std::string> forbidden_labels_;
  bool have_forbidden_ = false;

  // Trolley pose (map frame).
  bool have_odom_ = false;
  double pos_x_ = 0.0, pos_y_ = 0.0;
  rclcpp::Time last_odom_time_;

  void on_selected(const golfcart_msgs::msg::CourseSelected::SharedPtr msg)
  {
    load_zones(msg->zip_path);
  }

  // Load the speed zones from the course zip (via parse_course_zip -> CourseMap).
  void load_zones(const std::string & zip_path)
  {
    ParsedCourse parsed;
    if (!parse_course_zip(zip_path, parsed)) {
      RCLCPP_ERROR(get_logger(), "Cannot parse course zip '%s'", zip_path.c_str());
      have_zones_ = false;
      return;
    }
    zones_.clear();
    limits_.clear();
    labels_.clear();
    forbidden_.clear();
    forbidden_types_.clear();
    forbidden_labels_.clear();
    const golfcart_msgs::msg::CourseMap & map = parsed.map;
    for (size_t i = 0; i < map.speed_zones.size(); ++i) {
      std::vector<MapPoint> poly;
      for (const geometry_msgs::msg::Point32 & p : map.speed_zones[i].points) {
        poly.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
      }
      if (poly.size() < 3) {
        continue;
      }
      zones_.push_back(poly);
      limits_.push_back(i < map.speed_zone_limits_mps.size()
                          ? map.speed_zone_limits_mps[i] : -1.0);
      labels_.push_back(i < map.speed_zone_labels.size()
                          ? map.speed_zone_labels[i] : "");
    }
    have_zones_ = !zones_.empty();

    // Forbidden zones (typed polygons) for proximity-based slowing.
    for (size_t i = 0; i < map.forbidden_zones.size(); ++i) {
      std::vector<MapPoint> poly;
      for (const geometry_msgs::msg::Point32 & p : map.forbidden_zones[i].points) {
        poly.push_back({static_cast<double>(p.x), static_cast<double>(p.y)});
      }
      if (poly.size() < 3) {
        continue;
      }
      forbidden_.push_back(poly);
      forbidden_types_.push_back(i < map.forbidden_zone_types.size()
                                   ? map.forbidden_zone_types[i] : "FORBIDDEN_ZONE");
      forbidden_labels_.push_back(i < map.forbidden_zone_labels.size()
                                    ? map.forbidden_zone_labels[i] : "");
    }
    have_forbidden_ = !forbidden_.empty();
    RCLCPP_INFO(get_logger(), "Loaded %zu speed zones, %zu forbidden zones",
                zones_.size(), forbidden_.size());
  }

  void on_odom(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    have_odom_ = true;
    last_odom_time_ = now();
    pos_x_ = msg->pose.pose.position.x;
    pos_y_ = msg->pose.pose.position.y;
    publish_status();
  }

  void publish_status()
  {
    golfcart_msgs::msg::SpeedZoneStatus msg;
    msg.timestamp = now();
    msg.valid = false;
    msg.limit_mps = -1.0;
    msg.zone = "";
    msg.pose_age_s = 0.0;

    if (!have_zones_ || !have_odom_) {
      status_pub_->publish(msg);
      return;
    }

    const double age = (now() - last_odom_time_).seconds();
    msg.pose_age_s = age;

    // Stale pose -> conservative low limit (never speed up on unknown data).
    if (age > pose_timeout_s_) {
      msg.valid = true;
      msg.limit_mps = stale_limit_mps_;
      msg.zone = "stale";
      status_pub_->publish(msg);
      return;
    }

    // Find the most-restrictive containing zone (min limit).
    const MapPoint p{pos_x_, pos_y_};
    double best = -1.0;
    std::string best_label = "";
    for (size_t i = 0; i < zones_.size(); ++i) {
      if (!point_in_polygon(p, zones_[i])) {
        continue;
      }
      const double lim = limits_[i];
      if (lim < 0.0) {
        continue;  // zone with no limit: ignore
      }
      if (best < 0.0 || lim < best) {
        best = lim;
        best_label = labels_[i];
      }
    }

    // Course-aware governor: slow near forbidden zones (greens, tees, water,
    // rough, bunkers) within slow_radius_m. Most-restrictive limit wins.
    if (have_forbidden_) {
      for (size_t i = 0; i < forbidden_.size(); ++i) {
        if (distance_to_polygon(p, forbidden_[i]) > slow_radius_m_) {
          continue;
        }
        const double lim = feature_limit_mps_;
        if (best < 0.0 || lim < best) {
          best = lim;
          best_label = "near " + (forbidden_labels_[i].empty()
                                    ? forbidden_types_[i] : forbidden_labels_[i]);
        }
      }
    }

    msg.valid = true;
    msg.limit_mps = best;
    msg.zone = best_label;
    status_pub_->publish(msg);
  }
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SpeedZoneNode>());
  rclcpp::shutdown();
  return 0;
}