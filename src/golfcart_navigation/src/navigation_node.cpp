#include <memory>

#include "golfcart_msgs/msg/goal_pose.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "golfcart_msgs/msg/navigation_status.hpp"
#include "golfcart_msgs/srv/set_goal.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "nav2_msgs/action/navigate_to_pose.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "std_msgs/msg/string.hpp"
#include "tf2/LinearMath/Quaternion.hpp"

namespace golfcart
{

// Navigation bridge node.
// Connects Nav2 output to the Safety Controller input, and provides a goal
// interface (/set_goal service) that forwards to Nav2's navigate_to_pose action.
//
// Navigation is the lowest-priority motion source (priority 0). If a manual
// request arrives (higher priority), navigation pauses.
class NavigationNode : public rclcpp::Node
{
public:
  using NavigateToPose = nav2_msgs::action::NavigateToPose;
  using GoalHandle = rclcpp_action::ClientGoalHandle<NavigateToPose>;

  NavigationNode()
  : Node("navigation_node")
  {
    // Subscribe to Nav2 cmd_vel and republish as MotionRequest (priority 0).
    cmd_vel_sub_ = create_subscription<geometry_msgs::msg::Twist>(
      "cmd_vel", rclcpp::SensorDataQoS(),
      [this](const geometry_msgs::msg::Twist::SharedPtr msg) {
        publish_motion_request(msg->linear.x, msg->angular.z);
      });

    // Subscribe to safety/state to know when navigation is allowed.
    safety_state_sub_ = create_subscription<std_msgs::msg::String>(
      "safety/state", rclcpp::SensorDataQoS(),
      [this](const std_msgs::msg::String::SharedPtr msg) {
        safety_state_ = msg->data;
        check_pause();
      });

    // MotionRequest publisher (priority 0 = autonomous/navigation).
    motion_pub_ = create_publisher<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS());

    // NavigationStatus publisher.
    status_pub_ = create_publisher<golfcart_msgs::msg::NavigationStatus>(
      "navigation/status", rclcpp::SensorDataQoS());

    // /set_goal service (takes a GoalPose > triggers NavigateToPose action).
    set_goal_srv_ = create_service<golfcart_msgs::srv::SetGoal>(
      "set_goal",
      [this](const std::shared_ptr<golfcart_msgs::srv::SetGoal::Request> req,
             std::shared_ptr<golfcart_msgs::srv::SetGoal::Response> resp) {
        send_goal(req, resp);
      });

    // NAV2 navigate_to_pose action client.
    navigate_client_ = rclcpp_action::create_client<NavigateToPose>(this, "navigate_to_pose");
  }

private:
  void publish_motion_request(double linear, double angular)
  {
    if (!manual_override()) {
      golfcart_msgs::msg::MotionRequest msg;
      msg.linear_velocity_mps = linear;
      msg.angular_velocity_radps = angular;
      msg.source = "navigation";
      msg.priority = 0;  // autonomous/navigation - lowest
      msg.timestamp = now();
      motion_pub_->publish(msg);
    }
  }

  bool manual_override()
  {
    // If safety is not READY/MOVING (e.g. manual has control), pause.
    return safety_state_ != "READY" && safety_state_ != "MOVING";
  }

  void check_pause()
  {
    if (manual_override()) {
      publish_status("PAUSED");
    } else if (active_goal_) {
      publish_status("DRIVING");
    } else {
      publish_status("IDLE");
    }
  }

  void publish_status(const std::string & state)
  {
    golfcart_msgs::msg::NavigationStatus msg;
    msg.state = state;
    msg.error = "";
    msg.progress = 0.0f;
    msg.timestamp = get_clock()->now();
    status_pub_->publish(msg);
  }

  void send_goal(const std::shared_ptr<golfcart_msgs::srv::SetGoal::Request> req,
                 const std::shared_ptr<golfcart_msgs::srv::SetGoal::Response> resp)
  {
    if (!navigate_client_->wait_for_action_server(std::chrono::seconds(2))) {
      resp->success = false;
      resp->message = "Nav2 action server not available";
      return;
    }

    active_goal_ = true;
    auto goal = NavigateToPose::Goal();
    goal.pose.header.frame_id = req->goal.frame_id.empty() ? "map" : req->goal.frame_id;
    goal.pose.header.stamp = now();
    goal.pose.pose.position.x = req->goal.x;
    goal.pose.pose.position.y = req->goal.y;
    goal.pose.pose.position.z = 0.0;
    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, req->goal.theta);
    goal.pose.pose.orientation.x = q.x();
    goal.pose.pose.orientation.y = q.y();
    goal.pose.pose.orientation.z = q.z();
    goal.pose.pose.orientation.w = q.w();

    auto send_goal_options = rclcpp_action::Client<NavigateToPose>::SendGoalOptions();
    send_goal_options.goal_response_callback =
      [this, resp](const GoalHandle::SharedPtr & handle) {
        if (!handle) {
          resp->success = false;
          resp->message = "Goal rejected by server";
          active_goal_ = false;
          return;
        }
        resp->success = true;
        resp->message = "Goal accepted";
      };
    send_goal_options.result_callback =
      [this](const GoalHandle::WrappedResult & result) {
        active_goal_ = false;
        if (result.code == rclcpp_action::ResultCode::SUCCEEDED) {
          publish_status("ARRIVED");
        } else {
          publish_status("STOPPED");
        }
      };

    navigate_client_->async_send_goal(goal, send_goal_options);
  }

  rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr safety_state_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr motion_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::NavigationStatus>::SharedPtr status_pub_;
  rclcpp::Service<golfcart_msgs::srv::SetGoal>::SharedPtr set_goal_srv_;
  std::shared_ptr<rclcpp_action::Client<NavigateToPose>> navigate_client_;
  std::string safety_state_ = "SAFE_STOPPED";
  bool active_goal_ = false;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::NavigationNode>());
  rclcpp::shutdown();
  return 0;
}