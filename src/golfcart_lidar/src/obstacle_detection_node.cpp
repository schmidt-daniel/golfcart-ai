#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

#include "golfcart_msgs/msg/obstacle.hpp"
#include "golfcart_msgs/msg/obstacle_state.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

// Obstacle Detection node.
// Consumes /scan (LaserScan) and publishes /obstacles and /obstacles/state.
// Detects obstacles inside a configurable stopping zone in front of the trolley.
class ObstacleDetectionNode : public rclcpp::Node
{
public:
  ObstacleDetectionNode()
  : Node("obstacle_detection_node")
  {
    stopping_distance_ = declare_parameter<double>("stopping_distance_m", 1.5);
    min_range_ = declare_parameter<double>("min_range_m", 0.1);
    max_range_ = declare_parameter<double>("max_range_m", 12.0);
    // Stopping zone angular half-width (radians) in front of the trolley.
    zone_half_angle_ = declare_parameter<double>("zone_half_angle_rad", 0.6);

    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
      "scan", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::LaserScan::SharedPtr msg) {
        process_scan(msg);
      });

    obstacles_pub_ = create_publisher<golfcart_msgs::msg::Obstacle>(
      "obstacles", rclcpp::SensorDataQoS());
    state_pub_ = create_publisher<golfcart_msgs::msg::ObstacleState>(
      "obstacles/state", rclcpp::SensorDataQoS());
  }

private:
  void process_scan(const sensor_msgs::msg::LaserScan::SharedPtr msg)
  {
    bool obstacle_in_zone = false;
    double nearest = std::numeric_limits<double>::infinity();
    double nearest_angle = 0.0;

    for (size_t i = 0; i < msg->ranges.size(); ++i) {
      const double range = msg->ranges[i];
      if (!std::isfinite(range) || range < min_range_ || range > max_range_) {
        continue;
      }
      const double angle = msg->angle_min + i * msg->angle_increment;

      // Track nearest obstacle overall.
      if (range < nearest) {
        nearest = range;
        nearest_angle = angle;
      }

      // Check if inside the stopping zone (in front of the trolley).
      double d = std::abs(angle);
      d = std::min(d, 2.0 * M_PI - d);
      if (d <= zone_half_angle_ && range <= stopping_distance_) {
        obstacle_in_zone = true;
      }
    }

    golfcart_msgs::msg::ObstacleState state;
    state.obstacle_in_zone = obstacle_in_zone;
    state.nearest_distance_m = std::isfinite(nearest) ? nearest : 0.0f;
    state.nearest_angle_rad = nearest_angle;
    state.valid = true;
    state.timestamp = now();
    state_pub_->publish(state);
  }

  double stopping_distance_ = 1.5;
  double min_range_ = 0.1;
  double max_range_ = 12.0;
  double zone_half_angle_ = 0.6;

  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::Obstacle>::SharedPtr obstacles_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::ObstacleState>::SharedPtr state_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::ObstacleDetectionNode>());
  rclcpp::shutdown();
  return 0;
}