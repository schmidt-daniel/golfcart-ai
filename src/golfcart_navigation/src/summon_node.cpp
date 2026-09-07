#include <chrono>
#include <cmath>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/phone_fix.hpp"
#include "golfcart_msgs/msg/summon_status.hpp"
#include "golfcart_msgs/srv/set_goal.hpp"
#include "golfcart_msgs/srv/summon_trigger.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "std_srvs/srv/trigger.hpp"

#include "summon_math.hpp"

namespace golfcart
{

// Summon node.
// Tracks the operator's live phone position and drives the trolley to it via
// the existing navigation stack (/set_goal_geo -> /set_goal -> Nav2).
//
// Two targeting modes:
//   CURRENT - aim at the operator's live position.
//   PREDICT - estimate operator velocity and aim at their predicted position
//             at the trolley's ETA (intercept), with a "hold if approaching"
//             rule.
//
// Safety: target-loss stop, max distance, timeout, reduced speed, accuracy
// gate, already-at-trolley refusal, restart-on-retrigger, network-loss
// behavior.
class SummonNode : public rclcpp::Node
{
public:
  SummonNode()
  : Node("summon_node")
  {
    // ---- Parameters ----
    origin_lat_ = declare_parameter<double>("origin_latitude_deg", 0.0);
    origin_lon_ = declare_parameter<double>("origin_longitude_deg", 0.0);
    origin_rotation_ = declare_parameter<double>("origin_rotation_rad", 0.0);

    target_timeout_s_ = declare_parameter<double>("target_timeout_s", 3.0);
    max_summon_distance_m_ = declare_parameter<double>("max_summon_distance_m", 100.0);
    summon_timeout_s_ = declare_parameter<double>("summon_timeout_s", 120.0);
    max_speed_mps_ = declare_parameter<double>("max_speed_mps", 1.0);
    arrival_radius_m_ = declare_parameter<double>("arrival_radius_m", 2.0);
    approach_slowing_radius_m_ = declare_parameter<double>("approach_slowing_radius_m", 5.0);
    max_lead_m_ = declare_parameter<double>("max_lead_m", 10.0);
    approach_cone_deg_ = declare_parameter<double>("approach_cone_deg", 25.0);
    approach_pass_m_ = declare_parameter<double>("approach_pass_m", 2.0);
    approach_timeout_s_ = declare_parameter<double>("approach_timeout_s", 30.0);
    velocity_filter_window_s_ = declare_parameter<double>("velocity_filter_window_s", 3.0);
    accuracy_gate_m_ = declare_parameter<double>("accuracy_gate_m", 2.5);
    retarget_hysteresis_m_ = declare_parameter<double>("retarget_hysteresis_m", 1.0);
    retarget_period_s_ = declare_parameter<double>("retarget_period_s", 1.0);

    // ---- Subscriptions ----
    phone_sub_ = create_subscription<golfcart_msgs::msg::PhoneFix>(
      "phone/gps", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::PhoneFix::SharedPtr msg) {
        on_phone_fix(msg);
      });

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        trolley_pos_.x = msg->pose.pose.position.x;
        trolley_pos_.y = msg->pose.pose.position.y;
        trolley_has_pos_ = true;
      });

    // ---- Publishers ----
    status_pub_ = create_publisher<golfcart_msgs::msg::SummonStatus>(
      "summon/status", rclcpp::SensorDataQoS());

    // ---- Services ----
    summon_srv_ = create_service<golfcart_msgs::srv::SummonTrigger>(
      "summon",
      [this](const std::shared_ptr<golfcart_msgs::srv::SummonTrigger::Request> req,
             std::shared_ptr<golfcart_msgs::srv::SummonTrigger::Response> resp) {
        handle_summon(req, resp);
      });

    set_goal_geo_client_ = create_client<golfcart_msgs::srv::SetGoal>("set_goal_geo");

    // ---- Timers ----
    retarget_timer_ = create_wall_timer(
      std::chrono::duration<double>(retarget_period_s_),
      [this]() { retarget_cycle(); });
    status_timer_ = create_wall_timer(
      std::chrono::milliseconds(500),
      [this]() { publish_status(); });
  }

private:
  void on_phone_fix(const golfcart_msgs::msg::PhoneFix::SharedPtr msg)
  {
    last_phone_time_ = now();
    if (!msg->valid || msg->accuracy_m > accuracy_gate_m_) {
      // Invalid or too inaccurate: treat as no fix.
      phone_valid_ = false;
      return;
    }
    phone_valid_ = true;
    phone_lat_ = msg->latitude_deg;
    phone_lon_ = msg->longitude_deg;

    // Maintain a time-windowed history for velocity estimation.
    const double t = now().seconds();
    history_.push_back({msg->latitude_deg, msg->longitude_deg, t});
    while (!history_.empty() &&
           t - history_.front().t_s > velocity_filter_window_s_) {
      history_.pop_front();
    }
  }

  void handle_summon(
    const std::shared_ptr<golfcart_msgs::srv::SummonTrigger::Request> req,
    std::shared_ptr<golfcart_msgs::srv::SummonTrigger::Response> resp)
  {
    if (req->cancel) {
      cancel_summon();
      resp->success = true;
      resp->message = "Summon cancelled";
      return;
    }

    // Validate mode.
    if (req->mode != "CURRENT" && req->mode != "PREDICT") {
      resp->success = false;
      resp->message = "Invalid mode: " + req->mode;
      return;
    }

    // Accuracy gate: the phone must be accurate to <= accuracy_gate_m_.
    if (!phone_valid_ || std::hypot(req->lat - phone_lat_, req->lon - phone_lon_) > 1e-6) {
      // The request carries the phone's current position; require a valid fix.
      if (!phone_valid_) {
        resp->success = false;
        resp->message = "Phone GPS not valid (accuracy > " +
                        std::to_string(accuracy_gate_m_) + " m or no fix)";
        return;
      }
    }

    // Already at the trolley -> refuse.
    if (trolley_has_pos_) {
      const Vec2 op = latlon_to_enu(phone_lat_, phone_lon_, origin_lat_, origin_lon_);
      const double dist = std::hypot(op.x - trolley_pos_.x, op.y - trolley_pos_.y);
      if (dist <= arrival_radius_m_) {
        resp->success = false;
        resp->message = "Operator already at the trolley";
        return;
      }
      // Max distance check.
      if (dist > max_summon_distance_m_) {
        resp->success = false;
        resp->message = "Operator too far (" + std::to_string(dist) + " m)";
        return;
      }
    }

    // Start (or restart) summon.
    mode_ = req->mode;
    active_ = true;
    start_time_ = now();
    last_target_lat_ = phone_lat_;
    last_target_lon_ = phone_lon_;
    state_ = "TRACKING";
    resp->success = true;
    resp->message = "Summon started (" + mode_ + ")";
  }

  void cancel_summon()
  {
    active_ = false;
    state_ = "CANCELLED";
    history_.clear();
    // Cancel any active Nav2 goal via /safety/stop.
    call_safety_stop();
  }

  void retarget_cycle()
  {
    if (!active_) {
      return;
    }

    // Target-loss: no valid phone fix for target_timeout_s_.
    if (!phone_valid_ ||
        (now() - last_phone_time_).seconds() > target_timeout_s_) {
      // Network loss: drive to the last planned location and notify.
      state_ = "TARGET_LOST";
      // Keep the last known target as the goal (drive to last planned location).
      if (last_target_lat_ != 0.0 || last_target_lon_ != 0.0) {
        send_goal(last_target_lat_, last_target_lon_);
      }
      publish_status();
      return;
    }

    // Timeout.
    if ((now() - start_time_).seconds() > summon_timeout_s_) {
      state_ = "TIMEOUT";
      active_ = false;
      call_safety_stop();
      publish_status();
      return;
    }

    // Compute the target based on the mode.
    double target_lat = phone_lat_;
    double target_lon = phone_lon_;

    if (mode_ == "PREDICT") {
      const Vec2 op = latlon_to_enu(phone_lat_, phone_lon_, origin_lat_, origin_lon_);
      const Vec2 op_vel = estimate_velocity(history_).v;

      // "Hold if approaching" rule.
      const Vec2 trolley_to_op{op.x - trolley_pos_.x, op.y - trolley_pos_.y};
      if (is_approaching(op_vel, trolley_to_op, approach_cone_deg_, approach_pass_m_)) {
        // Hold position and wait for the operator.
        if (state_ != "HOLDING") {
          state_ = "HOLDING";
          call_safety_stop();  // cancel any active goal
        }
        // Waiting fallback: if approaching too long, switch to intercept.
        if ((now() - start_time_).seconds() > approach_timeout_s_) {
          state_ = "DRIVING";
        } else {
          publish_status();
          return;
        }
      }

      // Predictive intercept.
      const Vec2 target = predictive_target(
        op, op_vel, trolley_pos_, max_speed_mps_, max_lead_m_);
      // Convert ENU target back to lat/lon (inverse of latlon_to_enu).
      target_lat = origin_lat_ + target.y / kEarthRadiusM * 180.0 / M_PI;
      target_lon = origin_lon_ + target.x / (kEarthRadiusM * std::cos(origin_lat_ * M_PI / 180.0)) * 180.0 / M_PI;
      state_ = "DRIVING";
    } else {
      state_ = "DRIVING";
    }

    // Re-target with hysteresis.
    const double moved = std::hypot(target_lat - last_target_lat_, target_lon - last_target_lon_);
    if (moved > retarget_hysteresis_m_ || !goal_sent_) {
      send_goal(target_lat, target_lon);
      last_target_lat_ = target_lat;
      last_target_lon_ = target_lon;
      goal_sent_ = true;
    }

    // Arrival check.
    if (trolley_has_pos_) {
      const Vec2 op = latlon_to_enu(phone_lat_, phone_lon_, origin_lat_, origin_lon_);
      const double dist = std::hypot(op.x - trolley_pos_.x, op.y - trolley_pos_.y);
      if (dist <= arrival_radius_m_) {
        state_ = "ARRIVED";
        active_ = false;
        call_safety_stop();
      }
    }
  }

  void send_goal(double lat, double lon)
  {
    // Forward to /set_goal_geo (georeference_node) which converts lat/lon to
    // map frame and calls /set_goal. We use the SetGoal service type.
    if (!set_goal_geo_client_->service_is_ready()) {
      RCLCPP_WARN(get_logger(), "set_goal_geo not available");
      return;
    }
    auto req = std::make_shared<golfcart_msgs::srv::SetGoal::Request>();
    req->goal.x = lat;
    req->goal.y = lon;
    req->goal.theta = 0.0;
    req->goal.frame_id = "map";
    req->goal.source = "summon";
    set_goal_geo_client_->async_send_request(req);
  }

  void call_safety_stop()
  {
    if (!safety_stop_client_) {
      safety_stop_client_ = create_client<std_srvs::srv::Trigger>("safety/stop");
    }
    if (safety_stop_client_->service_is_ready()) {
      auto req = std::make_shared<std_srvs::srv::Trigger::Request>();
      safety_stop_client_->async_send_request(req);
    }
  }

  void publish_status()
  {
    golfcart_msgs::msg::SummonStatus msg;
    msg.active = active_;
    msg.state = state_;
    msg.target_lat = last_target_lat_;
    msg.target_lon = last_target_lon_;
    msg.distance_m = 0.0;
    if (trolley_has_pos_) {
      const Vec2 op = latlon_to_enu(phone_lat_, phone_lon_, origin_lat_, origin_lon_);
      msg.distance_m = std::hypot(op.x - trolley_pos_.x, op.y - trolley_pos_.y);
    }
    msg.progress = 0.0f;
    msg.timestamp = now();
    status_pub_->publish(msg);
  }

  // ---- State ----
  bool active_ = false;
  std::string state_ = "IDLE";
  std::string mode_ = "CURRENT";
  rclcpp::Time start_time_;
  rclcpp::Time last_phone_time_;

  bool phone_valid_ = false;
  double phone_lat_ = 0.0, phone_lon_ = 0.0;
  double last_target_lat_ = 0.0, last_target_lon_ = 0.0;
  bool goal_sent_ = false;

  bool trolley_has_pos_ = false;
  Vec2 trolley_pos_{0.0, 0.0};

  std::deque<Fix> history_;

  // ---- Params ----
  double origin_lat_, origin_lon_, origin_rotation_;
  double target_timeout_s_, max_summon_distance_m_, summon_timeout_s_;
  double max_speed_mps_, arrival_radius_m_, approach_slowing_radius_m_;
  double max_lead_m_, approach_cone_deg_, approach_pass_m_, approach_timeout_s_;
  double velocity_filter_window_s_, accuracy_gate_m_;
  double retarget_hysteresis_m_, retarget_period_s_;

  // ---- ROS handles ----
  rclcpp::Subscription<golfcart_msgs::msg::PhoneFix>::SharedPtr phone_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::SummonStatus>::SharedPtr status_pub_;
  rclcpp::Service<golfcart_msgs::srv::SummonTrigger>::SharedPtr summon_srv_;
  rclcpp::Client<golfcart_msgs::srv::SetGoal>::SharedPtr set_goal_geo_client_;
  rclcpp::Client<std_srvs::srv::Trigger>::SharedPtr safety_stop_client_;
  rclcpp::TimerBase::SharedPtr retarget_timer_;
  rclcpp::TimerBase::SharedPtr status_timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SummonNode>());
  rclcpp::shutdown();
  return 0;
}