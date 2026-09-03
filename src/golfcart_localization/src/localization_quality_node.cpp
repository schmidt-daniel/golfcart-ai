#include <algorithm>
#include <memory>

#include "golfcart_msgs/msg/navigation_status.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Localization Quality Gate node.
// Monitors the fused pose uncertainty from the EKF (on /odometry/filtered).
// If the position covariance exceeds a threshold (e.g. GPS lost too long, or
// odom/IMU drift too high), it publishes a NavigationStatus indicating the
// localization is degraded, so navigation can pause and ask the operator.
//
// Publishes:
//   /localization/quality  (golfcart_msgs/NavigationStatus)
//     state = "OK" or "DEGRADED"
class LocalizationQualityNode : public rclcpp::Node
{
public:
  LocalizationQualityNode()
  : Node("localization_quality_node")
  {
    // Position covariance threshold (m^2) on the x/y diagonal.
    max_position_covariance_ = declare_parameter<double>("max_position_covariance", 1.0);

    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) {
        handle_odom(msg);
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::NavigationStatus>(
      "localization/quality", rclcpp::SensorDataQoS());
  }

private:
  void handle_odom(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    // Covariance is a 36-element row-major 6x6 matrix. Position x/y are
    // indices 0 and 7 (row 0 col 0, row 1 col 1).
    const double cov_xx = msg->pose.covariance[0];
    const double cov_yy = msg->pose.covariance[7];
    const double pos_cov = std::max(cov_xx, cov_yy);

    golfcart_msgs::msg::NavigationStatus status;
    status.timestamp = now();
    if (pos_cov > max_position_covariance_) {
      status.state = "DEGRADED";
      status.error = "Localization uncertainty too high (GPS lost or drift)";
    } else {
      status.state = "OK";
      status.error = "";
    }
    status.progress = 0.0f;
    status_pub_->publish(status);
  }

  double max_position_covariance_ = 1.0;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::NavigationStatus>::SharedPtr status_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::LocalizationQualityNode>());
  rclcpp::shutdown();
  return 0;
}