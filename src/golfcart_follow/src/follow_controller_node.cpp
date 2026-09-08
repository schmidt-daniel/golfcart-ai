#include <cmath>
#include <memory>

#include "golfcart_msgs/msg/follow_status.hpp"
#include "golfcart_msgs/msg/motion_request.hpp"
#include "golfcart_msgs/msg/obstacle.hpp"
#include "golfcart_msgs/msg/person_target.hpp"
#include "golfcart_msgs/srv/follow_trigger.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Follow Controller node.
// Consumes /person/target (PersonTarget) and /obstacles/awareness (Obstacle),
// publishes MotionRequest (priority 0, source follow_me) on /motion/request.
//
// Follow-behind control law: maintain a set following distance. Obstacles exert
// a repulsive force (potential-field) on the desired heading for soft steering.
// Holds position when the person stops or leaves the forward sector; stops and
// requires re-triggering after resume_timeout_s.
class FollowControllerNode : public rclcpp::Node
{
public:
  FollowControllerNode()
  : Node("follow_controller_node")
  {
    // ---- Parameters ----
    follow_distance_m_ = declare_parameter<double>("follow_distance_m", 1.5);
    max_speed_mps_ = declare_parameter<double>("max_speed_mps", 0.8);
    kp_ = declare_parameter<double>("kp", 0.5);
    kt_ = declare_parameter<double>("kt", 0.8);  // turn gain
    confidence_threshold_ = declare_parameter<double>("confidence_threshold", 0.4);
    resume_timeout_s_ = declare_parameter<double>("resume_timeout_s", 20.0);
    obstacle_repulsion_gain_ = declare_parameter<double>("obstacle_repulsion_gain", 0.5);
    obstacle_repulsion_radius_m_ = declare_parameter<double>("obstacle_repulsion_radius_m", 1.0);
    target_timeout_s_ = declare_parameter<double>("target_timeout_s", 0.5);

    // ---- Subscriptions ----
    target_sub_ = create_subscription<golfcart_msgs::msg::PersonTarget>(
      "person/target", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::PersonTarget::SharedPtr msg) {
        on_target(msg);
      });
    obstacle_sub_ = create_subscription<golfcart_msgs::msg::Obstacle>(
      "obstacles/awareness", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::Obstacle::SharedPtr msg) {
        on_obstacle(msg);
      });

    // ---- Publishers ----
    motion_pub_ = create_publisher<golfcart_msgs::msg::MotionRequest>(
      "motion/request", rclcpp::SensorDataQoS());
    status_pub_ = create_publisher<golfcart_msgs::msg::FollowStatus>(
      "follow/status", rclcpp::SensorDataQoS());

    // ---- Service ----
    follow_srv_ = create_service<golfcart_msgs::srv::FollowTrigger>(
      "follow",
      [this](const std::shared_ptr<golfcart_msgs::srv::FollowTrigger::Request> req,
             std::shared_ptr<golfcart_msgs::srv::FollowTrigger::Response> resp) {
        handle_follow(req, resp);
      });

    // ---- Timers ----
    control_timer_ = create_wall_timer(
      std::chrono::milliseconds(100),  // 10 Hz
      [this]() { control_cycle(); });
    status_timer_ = create_wall_timer(
      std::chrono::milliseconds(500),
      [this]() { publish_status(); });
  }

private:
  void on_target(const golfcart_msgs::msg::PersonTarget::SharedPtr msg)
  {
    last_target_ = *msg;
    last_target_time_ = now();
  }

  void on_obstacle(const golfcart_msgs::msg::Obstacle::SharedPtr msg)
  {
    last_obstacle_ = *msg;
  }

  void handle_follow(
    const std::shared_ptr<golfcart_msgs::srv::FollowTrigger::Request> req,
    std::shared_ptr<golfcart_msgs::srv::FollowTrigger::Response> resp)
  {
    if (req->cancel) {
      stop_following("STOPPED");
      resp->success = true;
      resp->message = "Follow stopped";
      return;
    }
    active_ = true;
    state_ = "FOLLOWING";
    resp->success = true;
    resp->message = "Follow started";
  }

  void control_cycle()
  {
    if (!active_) {
      return;
    }

    // Target stale or invalid -> stop.
    if (!last_target_.valid ||
        (now() - last_target_time_).seconds() > target_timeout_s_) {
      stop_following("TARGET_LOST");
      return;
    }

    // Confidence below threshold -> stop.
    if (last_target_.confidence < confidence_threshold_) {
      stop_following("TARGET_LOST");
      return;
    }

    // Hold-on-stop: if the person is stopped (low speed), hold position.
    // If they resume walking away within resume_timeout_s, resume.
    const double person_speed = std::abs(last_target_.relative_velocity_mps);
    if (person_speed < 0.05) {
      if (state_ != "HOLDING") {
        state_ = "HOLDING";
        hold_start_time_ = now();
        publish_zero_motion();
      }
      // If the person resumes walking away within the timeout, resume.
      if ((now() - hold_start_time_).seconds() > resume_timeout_s_) {
        stop_following("STOPPED");
        return;
      }
      // Stay holding (don't drive toward a stopped person).
      publish_zero_motion();
      return;
    }

    // Person is moving away -> follow.
    state_ = "FOLLOWING";

    // Distance error (P-controller).
    const double err = last_target_.distance_m - follow_distance_m_;
    double linear = kp_ * err;
    linear = std::clamp(linear, -max_speed_mps_, max_speed_mps_);

    // Lateral centering (turn to keep the person centered).
    double angular = kt_ * last_target_.lateral_offset_m;

    // Potential-field obstacle repulsion (soft steering).
    if (last_obstacle_.valid) {
      const double d = last_obstacle_.distance_m;
      if (d < obstacle_repulsion_radius_m_ && d > 0.01) {
        // Steer away from the obstacle (opposite its angle).
        const double repulse = obstacle_repulsion_gain_ * (1.0 - d / obstacle_repulsion_radius_m_);
        angular += repulse * std::copysign(1.0, -last_obstacle_.angle_rad);
      }
    }

    publish_motion(linear, angular);
  }

  void publish_motion(double linear, double angular)
  {
    golfcart_msgs::msg::MotionRequest msg;
    msg.linear_velocity_mps = static_cast<float>(linear);
    msg.angular_velocity_radps = static_cast<float>(angular);
    msg.source = "follow_me";
    msg.priority = 0;  // autonomous
    msg.timestamp = now();
    motion_pub_->publish(msg);
  }

  void publish_zero_motion()
  {
    publish_motion(0.0, 0.0);
  }

  void stop_following(const std::string & reason)
  {
    active_ = false;
    state_ = reason;
    publish_zero_motion();
  }

  void publish_status()
  {
    golfcart_msgs::msg::FollowStatus msg;
    msg.active = active_;
    msg.state = state_;
    msg.distance_m = last_target_.valid ? last_target_.distance_m : 0.0;
    msg.lateral_offset_m = last_target_.valid ? last_target_.lateral_offset_m : 0.0;
    msg.confidence = last_target_.confidence;
    msg.timestamp = now();
    status_pub_->publish(msg);
  }

  // ---- Params ----
  double follow_distance_m_, max_speed_mps_, kp_, kt_;
  double confidence_threshold_, resume_timeout_s_;
  double obstacle_repulsion_gain_, obstacle_repulsion_radius_m_, target_timeout_s_;

  // ---- State ----
  bool active_ = false;
  std::string state_ = "IDLE";
  rclcpp::Time hold_start_time_;
  golfcart_msgs::msg::PersonTarget last_target_;
  rclcpp::Time last_target_time_;
  golfcart_msgs::msg::Obstacle last_obstacle_;

  // ---- ROS handles ----
  rclcpp::Subscription<golfcart_msgs::msg::PersonTarget>::SharedPtr target_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::Obstacle>::SharedPtr obstacle_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::MotionRequest>::SharedPtr motion_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::FollowStatus>::SharedPtr status_pub_;
  rclcpp::Service<golfcart_msgs::srv::FollowTrigger>::SharedPtr follow_srv_;
  rclcpp::TimerBase::SharedPtr control_timer_;
  rclcpp::TimerBase::SharedPtr status_timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::FollowControllerNode>());
  rclcpp::shutdown();
  return 0;
}