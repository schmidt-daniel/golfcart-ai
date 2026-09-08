#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "golfcart_msgs/msg/geofence_status.hpp"
#include "golfcart_msgs/msg/gps_fix.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "golfcart_msgs/srv/geofence_trigger.hpp"
#include "rclcpp/rclcpp.hpp"

#include "geofence_math.hpp"

namespace golfcart
{

// Geofence node.
// Monitors the trolley's GPS position against the outer course boundary
// polygon (loaded from a per-hole YAML config) and:
//   - publishes /geofence/status (ARMED / NEAR / CROSSED / OUT_OF_FIX / DISARMED)
//   - when autonomous driving crosses the boundary, publishes a priority-3 zero
//     MotionRequest so the Safety Controller stops the trolley
//   - in manual mode, only notifies (no forced stop)
// Always armed on startup; an operator may temporarily disarm via /geofence.
class GeofenceNode : public rclcpp::Node
{
public:
  GeofenceNode()
  : Node("geofence_node")
  {
    // ---- Parameters ----
    config_file_ = declare_parameter<std::string>("config_file", "");
    warn_distance_m_ = declare_parameter<double>("warn_distance_m", 5.0);
    stop_margin_m_ = declare_parameter<double>("stop_margin_m", 2.0);
    gps_timeout_s_ = declare_parameter<double>("gps_timeout_s", 3.0);
    autonomous_only_ = declare_parameter<bool>("autonomous_only", true);

    // ---- Load boundary config ----
    if (!load_config()) {
      RCLCPP_ERROR(get_logger(), "Failed to load geofence config '%s'; "
                   "geofence will stay DISARMED.", config_file_.c_str());
      armed_ = false;
    }

    // ---- Subscriptions ----
    gps_sub_ = create_subscription<golfcart_msgs::msg::GpsFix>(
      "gps/fix", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::GpsFix::SharedPtr msg) { on_gps(msg); });

    // Track the active motion source to decide autonomous vs manual.
    motion_sub_ = create_subscription<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotionRequest::SharedPtr msg) {
        last_motion_time_ = now();
        autonomous_active_ = (msg->priority == 0);
      });

    // ---- Publishers ----
    status_pub_ = create_publisher<golfcart_msgs::msg::GeofenceStatus>(
      "geofence/status", rclcpp::SensorDataQoS());
    motion_pub_ = create_publisher<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS());

    // ---- Services ----
    geofence_srv_ = create_service<golfcart_msgs::srv::GeofenceTrigger>(
      "geofence",
      [this](const std::shared_ptr<golfcart_msgs::srv::GeofenceTrigger::Request> req,
             std::shared_ptr<golfcart_msgs::srv::GeofenceTrigger::Response> resp) {
        handle_trigger(req, resp);
      });

    // ---- Timers ----
    status_timer_ = create_wall_timer(
      std::chrono::milliseconds(200),
      [this]() { publish_status(); });
  }

private:
  bool load_config()
  {
    if (config_file_.empty()) {
      return false;
    }
    YAML::Node root;
    try {
      root = YAML::LoadFile(config_file_);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "YAML load error: %s", e.what());
      return false;
    }
    if (!root["boundary"] || !root["boundary"].IsSequence() ||
        root["boundary"].size() < 3) {
      RCLCPP_ERROR(get_logger(), "Config must have a 'boundary' sequence of >=3 points");
      return false;
    }
    origin_lat_ = root["origin_latitude_deg"] ? root["origin_latitude_deg"].as<double>() : 0.0;
    origin_lon_ = root["origin_longitude_deg"] ? root["origin_longitude_deg"].as<double>() : 0.0;
    source_ = root["hole_number"] ? ("hole" + root["hole_number"].as<std::string>())
                                  : "course";

    boundary_.clear();
    for (const auto & pt : root["boundary"]) {
      const double lat = pt["lat"].as<double>();
      const double lon = pt["lon"].as<double>();
      boundary_.push_back(latlon_to_enu(lat, lon, origin_lat_, origin_lon_));
    }
    RCLCPP_INFO(get_logger(), "Loaded geofence '%s' with %zu boundary vertices",
                source_.c_str(), boundary_.size());
    return true;
  }

  void on_gps(const golfcart_msgs::msg::GpsFix::SharedPtr msg)
  {
    last_fix_time_ = now();
    if (!msg->valid) {
      has_fix_ = false;
      return;
    }
    has_fix_ = true;
    latest_lat_ = msg->latitude_deg;
    latest_lon_ = msg->longitude_deg;
    // Convert to ENU for the polygon check.
    pos_ = latlon_to_enu(latest_lat_, latest_lon_, origin_lat_, origin_lon_);
  }

  void handle_trigger(
    const std::shared_ptr<golfcart_msgs::srv::GeofenceTrigger::Request> req,
    std::shared_ptr<golfcart_msgs::srv::GeofenceTrigger::Response> resp)
  {
    if (req->cancel) {
      armed_ = false;
      resp->success = true;
      resp->message = "Geofence disarmed";
    } else {
      armed_ = true;
      resp->success = true;
      resp->message = "Geofence armed";
    }
  }

  void publish_status()
  {
    golfcart_msgs::msg::GeofenceStatus msg;
    msg.enabled = armed_;
    msg.source = source_;
    msg.timestamp = now();

    if (!armed_) {
      msg.state = "DISARMED";
      msg.inside = true;
      msg.distance_to_boundary_m = 0.0;
      msg.latest_lat = latest_lat_;
      msg.latest_lon = latest_lon_;
      msg.last_fix_age_s = fix_age();
      status_pub_->publish(msg);
      return;
    }

    // Stale / no fix -> OUT_OF_FIX (autonomous: safe stop).
    if (!has_fix_ || fix_age() > gps_timeout_s_) {
      msg.state = "OUT_OF_FIX";
      msg.inside = false;
      msg.distance_to_boundary_m = 0.0;
      msg.latest_lat = latest_lat_;
      msg.latest_lon = latest_lon_;
      msg.last_fix_age_s = fix_age();
      status_pub_->publish(msg);
      if (autonomous_only_ && autonomous_active_) {
        publish_stop();
      }
      return;
    }

    const bool inside = point_in_polygon(pos_, boundary_);
    const double dist = distance_to_boundary(pos_, boundary_);
    msg.inside = inside;
    msg.distance_to_boundary_m = inside ? dist : -dist;
    msg.latest_lat = latest_lat_;
    msg.latest_lon = latest_lon_;
    msg.last_fix_age_s = fix_age();

    if (!inside) {
      // Crossed the boundary.
      msg.state = "CROSSED";
      status_pub_->publish(msg);
      if (autonomous_only_ && autonomous_active_) {
        publish_stop();
      }
      return;
    }

    if (dist <= warn_distance_m_) {
      msg.state = "NEAR";
      status_pub_->publish(msg);
      return;
    }

    msg.state = "ARMED";
    status_pub_->publish(msg);
  }

  void publish_stop()
  {
    golfcart_msgs::msg::MotionRequest req;
    req.linear_velocity_mps = 0.0;
    req.angular_velocity_radps = 0.0;
    req.source = "geofence";
    req.priority = 3;  // safety override
    req.timestamp = now();
    motion_pub_->publish(req);
  }

  double fix_age() const
  {
    if (!has_fix_) {
      return std::numeric_limits<double>::infinity();
    }
    return (now() - last_fix_time_).seconds();
  }

  // ---- Params ----
  std::string config_file_;
  double warn_distance_m_;
  double stop_margin_m_;
  double gps_timeout_s_;
  bool autonomous_only_;

  // ---- Config ----
  double origin_lat_ = 0.0, origin_lon_ = 0.0;
  std::string source_;
  std::vector<Point2> boundary_;

  // ---- State ----
  bool armed_ = true;  // always armed on startup
  bool has_fix_ = false;
  bool autonomous_active_ = false;
  double latest_lat_ = 0.0, latest_lon_ = 0.0;
  Point2 pos_{0.0, 0.0};
  rclcpp::Time last_fix_time_;
  rclcpp::Time last_motion_time_;

  // ---- ROS handles ----
  rclcpp::Subscription<golfcart_msgs::msg::GpsFix>::SharedPtr gps_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::MotionRequest>::SharedPtr motion_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::GeofenceStatus>::SharedPtr status_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr motion_pub_;
  rclcpp::Service<golfcart_msgs::srv::GeofenceTrigger>::SharedPtr geofence_srv_;
  rclcpp::TimerBase::SharedPtr status_timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::GeofenceNode>());
  rclcpp::shutdown();
  return 0;
}