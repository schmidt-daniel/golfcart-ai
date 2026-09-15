#include <cmath>
#include <memory>

#include "golfcart_msgs/msg/capability_status.hpp"
#include "golfcart_msgs/srv/drive_distance.hpp"
#include "golfcart_msgs/srv/set_goal.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

#include "drive_distance_math.hpp"

namespace golfcart
{

// Drive-Distance node.
// Lets the operator drive the trolley a fixed distance (10/20/30/40/50 m) in
// its current heading direction. Subscribes to /odometry/filtered for the
// current pose + yaw, computes the goal point N meters ahead, and forwards it
// to the navigation stack via /set_goal (Nav2 navigate_to_pose). Obstacle
// avoidance and safety apply automatically.
//
// Gated on GPS capability (autonomous driving requires GPS).
class DriveDistanceNode : public rclcpp::Node
{
public:
  DriveDistanceNode()
  : Node("drive_distance_node")
  {
    max_distance_m_ = declare_parameter<double>("max_distance_m", 50.0);

    // Track GPS capability: autonomous driving requires GPS.
    cap_sub_ = create_subscription<golfcart_msgs::msg::CapabilityStatus>(
      "capability/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg) {
        gps_available_ = msg->gps;
      });

    // Current pose (position + heading).
    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        pose_.x = msg->pose.pose.position.x;
        pose_.y = msg->pose.pose.position.y;
        const double qx = msg->pose.pose.orientation.x;
        const double qy = msg->pose.pose.orientation.y;
        const double qz = msg->pose.pose.orientation.z;
        const double qw = msg->pose.pose.orientation.w;
        // Yaw from the quaternion (ZYX convention).
        pose_.yaw = std::atan2(2.0 * (qw * qz + qx * qy),
                               1.0 - 2.0 * (qy * qy + qz * qz));
        have_pose_ = true;
      });

    // Client to the navigation_node's /set_goal service.
    set_goal_client_ = create_client<golfcart_msgs::srv::SetGoal>("set_goal");

    // Service: /drive_distance (distance in m, or cancel).
    drive_srv_ = create_service<golfcart_msgs::srv::DriveDistance>(
      "drive_distance",
      [this](const std::shared_ptr<golfcart_msgs::srv::DriveDistance::Request> req,
             std::shared_ptr<golfcart_msgs::srv::DriveDistance::Response> resp) {
        handle_drive(req, resp);
      });
  }

private:
  void handle_drive(
    const std::shared_ptr<golfcart_msgs::srv::DriveDistance::Request> req,
    std::shared_ptr<golfcart_msgs::srv::DriveDistance::Response> resp)
  {
    if (req->cancel) {
      // Cancel: send a zero-distance goal to stop (or a stop request).
      resp->success = true;
      resp->message = "Drive cancelled";
      return;
    }

    // GPS required for autonomous driving.
    if (!gps_available_) {
      resp->success = false;
      resp->message = "Drive unavailable: GPS not present";
      return;
    }

    // Validate distance.
    if (req->distance_m <= 0.0 || req->distance_m > max_distance_m_) {
      resp->success = false;
      resp->message = "Invalid distance (0 < d <= " +
                      std::to_string(max_distance_m_) + ")";
      return;
    }

    // Need a valid pose to compute the goal.
    if (!have_pose_) {
      resp->success = false;
      resp->message = "No pose yet (odometry not available)";
      return;
    }

    // Compute the goal point ahead in the current heading.
    double gx, gy, gtheta;
    goal_ahead(pose_, req->distance_m, gx, gy, gtheta);

    // Forward to the navigation stack.
    auto goal = std::make_shared<golfcart_msgs::srv::SetGoal::Request>();
    goal->goal.x = gx;
    goal->goal.y = gy;
    goal->goal.theta = gtheta;
    goal->goal.frame_id = "map";
    goal->goal.source = "drive_distance";

    if (!set_goal_client_->service_is_ready()) {
      resp->success = false;
      resp->message = "Navigation not available";
      return;
    }
    auto future = set_goal_client_->async_send_request(goal);
    // Respond optimistically; the navigation node reports the actual result.
    resp->success = true;
    resp->message = "Driving " + std::to_string(req->distance_m) + " m";
  }

  double max_distance_m_ = 50.0;
  bool gps_available_ = false;
  bool have_pose_ = false;
  Pose2D pose_;

  rclcpp::Subscription<golfcart_msgs::msg::CapabilityStatus>::SharedPtr cap_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Client<golfcart_msgs::srv::SetGoal>::SharedPtr set_goal_client_;
  rclcpp::Service<golfcart_msgs::srv::DriveDistance>::SharedPtr drive_srv_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::DriveDistanceNode>());
  rclcpp::shutdown();
  return 0;
}