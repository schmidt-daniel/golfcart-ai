#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/course_map.hpp"
#include "golfcart_msgs/msg/course_selected.hpp"
#include "golfcart_msgs/msg/gps_fix.hpp"
#include "golfcart_msgs/msg/hole_session.hpp"
#include "golfcart_msgs/srv/hole_select.hpp"
#include "geometry_msgs/msg/point32.hpp"
#include "rclcpp/rclcpp.hpp"

#include "course_zip.hpp"

namespace golfcart
{

// Course session node.
// Tracks the active hole + selected tee for the loaded course.
//   - subscribes to /course/selected (CourseSelected: zip path) and /gps/fix
//   - auto-detects the hole by GPS proximity to tee boxes (with hysteresis:
//     stay on the current hole until the NEXT tee box is reached)
//   - serves /course/hole for manual hole/tee selection
//   - publishes /course/hole (HoleSession) with the hole layout, distances,
//     trolley position, and remaining distance to the hole
class CourseSessionNode : public rclcpp::Node
{
public:
  CourseSessionNode()
  : Node("course_session_node")
  {
    auto_radius_m_ = declare_parameter<double>("auto_radius_m", 30.0);

    selected_sub_ = create_subscription<golfcart_msgs::msg::CourseSelected>(
      "course/selected", rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal),
      [this](const golfcart_msgs::msg::CourseSelected::SharedPtr msg) { on_selected(msg); });
    gps_sub_ = create_subscription<golfcart_msgs::msg::GpsFix>(
      "gps/fix", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::GpsFix::SharedPtr msg) { on_gps(msg); });

    hole_pub_ = create_publisher<golfcart_msgs::msg::HoleSession>(
      "course/hole",
      rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal));

    hole_srv_ = create_service<golfcart_msgs::srv::HoleSelect>(
      "course/hole",
      [this](const std::shared_ptr<golfcart_msgs::srv::HoleSelect::Request> req,
             std::shared_ptr<golfcart_msgs::srv::HoleSelect::Response> resp) {
        handle_hole_select(req, resp);
      });

    timer_ = create_wall_timer(std::chrono::milliseconds(500),
                               [this]() { publish_session(); });
  }

private:
  double auto_radius_m_;
  rclcpp::Subscription<golfcart_msgs::msg::CourseSelected>::SharedPtr selected_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::GpsFix>::SharedPtr gps_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::HoleSession>::SharedPtr hole_pub_;
  rclcpp::Service<golfcart_msgs::srv::HoleSelect>::SharedPtr hole_srv_;
  rclcpp::TimerBase::SharedPtr timer_;

  golfcart_msgs::msg::CourseMap course_;
  std::vector<HoleData> holes_;
  bool have_course_ = false;
  bool have_gps_ = false;
  double gps_lat_ = 0.0, gps_lon_ = 0.0;

  int active_hole_ = 0;
  std::string active_tee_ = "";

  void on_selected(const golfcart_msgs::msg::CourseSelected::SharedPtr msg)
  {
    ParsedCourse parsed;
    if (!parse_course_zip(msg->zip_path, parsed)) {
      RCLCPP_ERROR(get_logger(), "Failed to parse course zip '%s'", msg->zip_path.c_str());
      return;
    }
    course_ = parsed.map;
    holes_ = parsed.holes;
    have_course_ = true;
    active_hole_ = 0;
    RCLCPP_INFO(get_logger(), "Course '%s' loaded (%zu holes).",
                course_.course_name.c_str(), holes_.size());
    publish_session();
  }

  void on_gps(const golfcart_msgs::msg::GpsFix::SharedPtr msg)
  {
    if (!msg->valid) {
      return;
    }
    have_gps_ = true;
    gps_lat_ = msg->latitude_deg;
    gps_lon_ = msg->longitude_deg;
    maybe_auto_detect();
  }

  double dist2(double ax, double ay, double bx, double by)
  {
    const double dx = ax - bx;
    const double dy = ay - by;
    return std::sqrt(dx * dx + dy * dy);
  }

  void maybe_auto_detect()
  {
    if (!have_course_ || !have_gps_) {
      return;
    }
    double tx, ty;
    latlon_to_map(gps_lat_, gps_lon_, tx, ty);

    // Find the nearest tee box across all holes.
    int best_hole = 0;
    double best_dist = 1e18;
    for (const HoleData & hd : holes_) {
      for (const golfcart_msgs::msg::CourseFeature & tee : hd.features) {
        if (tee.type != "TEE_BOX") {
          continue;
        }
        const double d = dist2(tx, ty, tee.x, tee.y);
        if (d < best_dist) {
          best_dist = d;
          best_hole = hd.number;
        }
      }
    }
    if (best_hole == 0) {
      return;
    }

    // Hysteresis: only switch if we're within auto_radius of the NEXT tee box.
    if (active_hole_ == 0 || best_hole != active_hole_) {
      if (best_dist <= auto_radius_m_) {
        active_hole_ = best_hole;
        RCLCPP_INFO(get_logger(), "Auto-detected hole %d (%.1f m to tee)",
                    active_hole_, best_dist);
        publish_session();
      }
    }
  }

  void handle_hole_select(const std::shared_ptr<golfcart_msgs::srv::HoleSelect::Request> req,
                          std::shared_ptr<golfcart_msgs::srv::HoleSelect::Response> resp)
  {
    if (!have_course_) {
      resp->success = false;
      resp->message = "No course loaded";
      return;
    }
    if (req->hole_number != 0) {
      bool found = false;
      for (const HoleData & hd : holes_) {
        if (hd.number == static_cast<int>(req->hole_number)) {
          found = true;
          break;
        }
      }
      if (!found) {
        resp->success = false;
        resp->message = "Unknown hole " + std::to_string(req->hole_number);
        return;
      }
      active_hole_ = static_cast<int>(req->hole_number);
    }
    if (!req->tee_id.empty()) {
      active_tee_ = req->tee_id;
    }
    resp->success = true;
    resp->message = "Hole " + std::to_string(active_hole_) + " tee " + active_tee_;
    publish_session();
  }

  void publish_session()
  {
    if (!have_course_ || active_hole_ == 0) {
      return;
    }
    golfcart_msgs::msg::HoleSession msg;
    msg.hole_number = static_cast<uint32_t>(active_hole_);
    msg.tee_id = active_tee_;
    msg.timestamp = now();

    const HoleData * hd = nullptr;
    for (const HoleData & h : holes_) {
      if (h.number == active_hole_) {
        hd = &h;
        break;
      }
    }
    if (!hd) {
      return;
    }
    msg.hole_name = hd->name;

    // Distance for the selected tee.
    msg.distance_m = 0.0;
    for (const auto & d : hd->distances) {
      if (d.first == active_tee_) {
        msg.distance_m = d.second;
        break;
      }
    }

    // Tee box (bottom) and green (top) positions.
    for (const golfcart_msgs::msg::CourseFeature & f : hd->features) {
      if (f.type == "TEE_BOX" && msg.tee_x == 0.0 && msg.tee_y == 0.0) {
        msg.tee_x = f.x;
        msg.tee_y = f.y;
      } else if (f.type == "GREEN" && msg.green_x == 0.0 && msg.green_y == 0.0) {
        msg.green_x = f.x;
        msg.green_y = f.y;
      }
    }

    // Boundary polygon.
    for (const geometry_msgs::msg::Point32 & p : hd->boundary) {
      msg.boundary.points.push_back(p);
    }
    msg.features = hd->features;

    // Trolley position + remaining distance to the green.
    if (have_gps_) {
      latlon_to_map(gps_lat_, gps_lon_, msg.trolley_x, msg.trolley_y);
      msg.remaining_m = dist2(msg.trolley_x, msg.trolley_y, msg.green_x, msg.green_y);
    } else {
      msg.remaining_m = 0.0;
    }

    hole_pub_->publish(msg);
  }

  void latlon_to_map(double lat, double lon, double & x, double & y)
  {
    constexpr double R = 6371000.0;
    const double dlat = (lat - course_.origin_latitude_deg) * M_PI / 180.0;
    const double dlon = (lon - course_.origin_longitude_deg) * M_PI / 180.0;
    const double cos_lat = std::cos(course_.origin_latitude_deg * M_PI / 180.0);
    const double east = R * dlon * cos_lat;
    const double north = R * dlat;
    const double c = std::cos(course_.origin_rotation_rad);
    const double s = std::sin(course_.origin_rotation_rad);
    x = east * c - north * s;
    y = east * s + north * c;
  }
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::CourseSessionNode>());
  rclcpp::shutdown();
  return 0;
}