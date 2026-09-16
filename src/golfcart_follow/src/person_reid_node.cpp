#include <memory>

#include "golfcart_follow/reid_math.hpp"
#include "golfcart_msgs/msg/follow_status.hpp"
#include "golfcart_msgs/msg/person_target.hpp"
#include "golfcart_msgs/msg/re_id_status.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Person Re-ID node.
// Re-acquires the follow-me operator after a target loss. Watches /follow/status;
// when follow is active and the state becomes TARGET_LOST, it enters SEARCHING.
// It then watches /person/target for a person reappearing near the last-known
// position (within a spatial gate + time window). On re-acquire, it publishes a
// fresh PersonTarget so the follow controller resumes.
//
// This is a continuity-based re-acquire (works with LiDAR), not appearance
// recognition (which would need the camera).
class PersonReIdNode : public rclcpp::Node
{
public:
  PersonReIdNode()
  : Node("person_reid_node")
  {
    search_timeout_s_ = declare_parameter<double>("search_timeout_s", 30.0);
    max_gap_m_ = declare_parameter<double>("max_gap_m", 3.0);

    tracker_ = ReIdTracker(search_timeout_s_, max_gap_m_);

    follow_sub_ = create_subscription<golfcart_msgs::msg::FollowStatus>(
      "follow/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::FollowStatus::SharedPtr msg) {
        on_follow_status(msg);
      });

    target_sub_ = create_subscription<golfcart_msgs::msg::PersonTarget>(
      "person/target", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::PersonTarget::SharedPtr msg) {
        on_target(msg);
      });

    target_pub_ = create_publisher<golfcart_msgs::msg::PersonTarget>(
      "person/target", rclcpp::SensorDataQoS());
    status_pub_ = create_publisher<golfcart_msgs::msg::ReIdStatus>(
      "reid/status", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(0.5);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_status(); });
  }

private:
  void on_follow_status(const golfcart_msgs::msg::FollowStatus::SharedPtr msg)
  {
    if (msg->state == "TARGET_LOST" && msg->active) {
      // Begin searching from the last-known target position.
      tracker_.begin_search(now().seconds(), last_x_, last_y_);
    } else if (msg->state == "FOLLOWING" || msg->state == "HOLDING") {
      // Follow is healthy; not searching.
      tracker_.reset();
    }
  }

  void on_target(const golfcart_msgs::msg::PersonTarget::SharedPtr msg)
  {
    if (!msg->valid) {
      return;
    }
    // Track the last-known position (for the search origin).
    last_x_ = msg->distance_m;
    last_y_ = msg->lateral_offset_m;

    if (!tracker_.searching()) {
      return;
    }

    // Candidate person: re-acquire if within the spatial gate + time window.
    const ReIdState st = tracker_.update(now().seconds(), msg->distance_m, msg->lateral_offset_m);
    if (st == REID_REACQUIRED) {
      // Re-publish the target so the follow controller resumes.
      golfcart_msgs::msg::PersonTarget out;
      out.distance_m = msg->distance_m;
      out.lateral_offset_m = msg->lateral_offset_m;
      out.relative_velocity_mps = msg->relative_velocity_mps;
      out.confidence = msg->confidence;
      out.source = "reid";
      out.valid = true;
      out.timestamp = now();
      target_pub_->publish(out);
      RCLCPP_INFO(get_logger(), "Operator re-acquired");
    }
  }

  void publish_status()
  {
    golfcart_msgs::msg::ReIdStatus msg;
    msg.searching = tracker_.searching();
    msg.reacquired = tracker_.reacquired();
    switch (tracker_.state()) {
      case REID_IDLE: msg.state = "IDLE"; break;
      case REID_SEARCHING: msg.state = "SEARCHING"; break;
      case REID_REACQUIRED: msg.state = "REACQUIRED"; break;
      case REID_TIMEOUT: msg.state = "TIMEOUT"; break;
      default: msg.state = "IDLE"; break;
    }
    msg.timestamp = now();
    status_pub_->publish(msg);
  }

  double search_timeout_s_ = 30.0;
  double max_gap_m_ = 3.0;
  double last_x_ = 0.0;
  double last_y_ = 0.0;
  ReIdTracker tracker_;

  rclcpp::Subscription<golfcart_msgs::msg::FollowStatus>::SharedPtr follow_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::PersonTarget>::SharedPtr target_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::PersonTarget>::SharedPtr target_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::ReIdStatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::PersonReIdNode>());
  rclcpp::shutdown();
  return 0;
}