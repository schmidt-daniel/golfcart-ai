#include <cmath>
#include <memory>

#include "golfcart_msgs/msg/goal_pose.hpp"
#include "golfcart_msgs/srv/set_goal.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Georeference node.
// Converts a web lat/lon target into a map-frame goal, using the map origin
// (lat/lon + rotation) from GPS anchoring. Provides a /set_goal_geo service
// that takes lat/lon and returns a map-frame GoalPose.
//
// The web app calls /set_goal_geo with lat/lon; this node converts to map
// coordinates and forwards to the navigation_node's /set_goal service.
class GeoreferenceNode : public rclcpp::Node
{
public:
  GeoreferenceNode()
  : Node("georeference_node")
  {
    // Map origin (datum). Set from the per-course map.
    origin_lat_ = declare_parameter<double>("origin_latitude_deg", 0.0);
    origin_lon_ = declare_parameter<double>("origin_longitude_deg", 0.0);
    origin_rotation_ = declare_parameter<double>("origin_rotation_rad", 0.0);

    // Client to the navigation_node's /set_goal service.
    set_goal_client_ = create_client<golfcart_msgs::srv::SetGoal>("set_goal");

    // Service: /set_goal_geo (lat/lon -> map-frame goal).
    geo_srv_ = create_service<golfcart_msgs::srv::SetGoal>(
      "set_goal_geo",
      [this](const std::shared_ptr<golfcart_msgs::srv::SetGoal::Request> req,
             std::shared_ptr<golfcart_msgs::srv::SetGoal::Response> resp) {
        handle_geo_goal(req, resp);
      });
  }

private:
  // Convert lat/lon to map-frame x/y using the map origin + rotation.
  // Simple equirectangular approximation (valid for short distances).
  void latlon_to_map(double lat, double lon, double & x, double & y)
  {
    constexpr double R = 6371000.0;  // Earth radius (m)
    const double dlat = (lat - origin_lat_) * M_PI / 180.0;
    const double dlon = (lon - origin_lon_) * M_PI / 180.0;
    const double cos_lat = std::cos(origin_lat_ * M_PI / 180.0);

    // Local ENU offsets (x = east, y = north).
    const double east = R * dlon * cos_lat;
    const double north = R * dlat;

    // Rotate into the map frame (map origin rotation).
    const double c = std::cos(origin_rotation_);
    const double s = std::sin(origin_rotation_);
    x = east * c - north * s;
    y = east * s + north * c;
  }

  void handle_geo_goal(const std::shared_ptr<golfcart_msgs::srv::SetGoal::Request> req,
                       std::shared_ptr<golfcart_msgs::srv::SetGoal::Response> resp)
  {
    // The request's goal.x/y are interpreted as lat/lon here.
    const double lat = req->goal.x;
    const double lon = req->goal.y;

    double x, y;
    latlon_to_map(lat, lon, x, y);

    // Forward to the navigation_node's /set_goal service.
    if (!set_goal_client_->wait_for_service(std::chrono::seconds(2))) {
      resp->success = false;
      resp->message = "set_goal service not available";
      return;
    }
    auto fwd = std::make_shared<golfcart_msgs::srv::SetGoal::Request>();
    fwd->goal.x = x;
    fwd->goal.y = y;
    fwd->goal.theta = req->goal.theta;
    fwd->goal.frame_id = "map";
    fwd->goal.source = "web_geo";
    auto future = set_goal_client_->async_send_request(fwd);
    // Wait briefly for the response.
    if (future.wait_for(std::chrono::seconds(3)) == std::future_status::ready) {
      auto result = future.get();
      resp->success = result->success;
      resp->message = result->message;
    } else {
      resp->success = false;
      resp->message = "set_goal timed out";
    }
  }

  double origin_lat_, origin_lon_, origin_rotation_;
  rclcpp::Client<golfcart_msgs::srv::SetGoal>::SharedPtr set_goal_client_;
  rclcpp::Service<golfcart_msgs::srv::SetGoal>::SharedPtr geo_srv_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::GeoreferenceNode>());
  rclcpp::shutdown();
  return 0;
}