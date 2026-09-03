#include <memory>
#include <mutex>

#include "geometry_msgs/msg/transform_stamped.hpp"
#include "golfcart_control/differential_drive.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "tf2/LinearMath/Quaternion.hpp"
#include "tf2_ros/transform_broadcaster.hpp"

namespace golfcart
{

// Wheel Odometry node.
// Subscribes /motor/state (encoder feedback), computes differential-drive
// odometry, and publishes nav_msgs/Odometry on /wheel/odometry + TF2 odom->base_link.
class WheelOdometryNode : public rclcpp::Node
{
public:
  WheelOdometryNode()
  : Node("wheel_odometry_node")
  {
    drive_.wheel_separation_m = declare_parameter<double>("wheel_separation_m", 0.5);
    const double rate_hz = declare_parameter<double>("odom_rate_hz", 50.0);
    const std::string odom_frame = declare_parameter<std::string>("odom_frame", "odom");
    const std::string base_frame = declare_parameter<std::string>("base_frame", "base_link");

    state_sub_ = create_subscription<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorState::SharedPtr msg) {
        handle_state(msg);
      });

    odom_pub_ = create_publisher<nav_msgs::msg::Odometry>(
      "wheel/odometry", rclcpp::SensorDataQoS());
    tf_broadcaster_ = std::make_shared<tf2_ros::TransformBroadcaster>(this);

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() {
        publish_odom();
      });
  }

private:
  void handle_state(const golfcart_msgs::msg::MotorState::SharedPtr msg)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Integrate wheel velocities into the odometry pose.

    // v_left/v_right are wheel velocities (m/s). Differential-drive kinematics:
    //   v      = (v_left + v_right) / 2
    //   omega  = (v_right - v_left) / wheel_separation
    const double v_left = msg->left_velocity;
    const double v_right = msg->right_velocity;
    const double v = (v_left + v_right) / 2.0;
    const double omega = (v_right - v_left) / drive_.wheel_separation_m;

    const double dt = (msg->timestamp.sec + msg->timestamp.nanosec * 1e-9) - last_time_;
    if (dt > 0.0 && dt < 1.0) {
      x_ += v * std::cos(theta_) * dt;
      y_ += v * std::sin(theta_) * dt;
      theta_ += omega * dt;
    }
    last_time_ = msg->timestamp.sec + msg->timestamp.nanosec * 1e-9;
    last_v_ = v;
    last_omega_ = omega;
  }

  void publish_odom()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    nav_msgs::msg::Odometry odom;
    odom.header.stamp = now();
    odom.header.frame_id = odom_frame_;
    odom.child_frame_id = base_frame_;
    odom.pose.pose.position.x = x_;
    odom.pose.pose.position.y = y_;
    odom.pose.pose.position.z = 0.0;
    tf2::Quaternion q;
    q.setRPY(0.0, 0.0, theta_);
    odom.pose.pose.orientation.x = q.x();
    odom.pose.pose.orientation.y = q.y();
    odom.pose.pose.orientation.z = q.z();
    odom.pose.pose.orientation.w = q.w();
    odom.twist.twist.linear.x = last_v_;
    odom.twist.twist.angular.z = last_omega_;
    odom_pub_->publish(odom);

    // Publish TF2 odom -> base_link.
    geometry_msgs::msg::TransformStamped t;
    t.header.stamp = now();
    t.header.frame_id = odom_frame_;
    t.child_frame_id = base_frame_;
    t.transform.translation.x = x_;
    t.transform.translation.y = y_;
    t.transform.translation.z = 0.0;
    t.transform.rotation.x = q.x();
    t.transform.rotation.y = q.y();
    t.transform.rotation.z = q.z();
    t.transform.rotation.w = q.w();
    tf_broadcaster_->sendTransform(t);
  }

  DifferentialDrive drive_;
  std::mutex mutex_;
  double x_ = 0.0, y_ = 0.0, theta_ = 0.0;
  double last_v_ = 0.0, last_omega_ = 0.0;
  double last_time_ = 0.0;
  std::string odom_frame_, base_frame_;
  rclcpp::Subscription<golfcart_msgs::msg::MotorState>::SharedPtr state_sub_;
  rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::shared_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::WheelOdometryNode>());
  rclcpp::shutdown();
  return 0;
}