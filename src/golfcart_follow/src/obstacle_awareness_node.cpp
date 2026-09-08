#include <cmath>
#include <limits>
#include <memory>

#include "golfcart_msgs/msg/obstacle.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

// Obstacle Awareness node.
// Consumes /scan (LaserScan) and publishes /obstacles/awareness (Obstacle) —
// a SOFT obstacle view for the follow controller to steer around (potential
// field). This is distinct from the safety /obstacles/state (hard stop via the
// Safety Controller). Safety stops; behavior steers.
class ObstacleAwarenessNode : public rclcpp::Node
{
public:
  ObstacleAwarenessNode()
  : Node("obstacle_awareness_node")
  {
    min_range_ = declare_parameter<double>("min_range_m", 0.1);
    max_range_ = declare_parameter<double>("max_range_m", 3.0);
    sector_half_angle_ = declare_parameter<double>("sector_half_angle_rad", 1.0472);  // ±60°

    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
      "scan", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::LaserScan::SharedPtr msg) {
        process_scan(msg);
      });
    awareness_pub_ = create_publisher<golfcart_msgs::msg::Obstacle>(
      "obstacles/awareness", rclcpp::SensorDataQoS());
  }

private:
  void process_scan(const sensor_msgs::msg::LaserScan::SharedPtr msg)
  {
    // Find the nearest obstacle in the forward sector (soft awareness).
    double nearest = std::numeric_limits<double>::infinity();
    double nearest_angle = 0.0;
    bool found = false;

    for (size_t i = 0; i < msg->ranges.size(); ++i) {
      const double range = msg->ranges[i];
      if (!std::isfinite(range) || range < min_range_ || range > max_range_) {
        continue;
      }
      const double angle = msg->angle_min + i * msg->angle_increment;
      double d = std::abs(angle);
      d = std::min(d, 2.0 * M_PI - d);
      if (d > sector_half_angle_) {
        continue;
      }
      if (range < nearest) {
        nearest = range;
        nearest_angle = angle;
        found = true;
      }
    }

    golfcart_msgs::msg::Obstacle obs;
    obs.valid = found;
    obs.distance_m = found ? static_cast<float>(nearest) : 0.0f;
    obs.angle_rad = found ? static_cast<float>(nearest_angle) : 0.0f;
    obs.width_m = 0.0f;
    obs.confidence = found ? 1.0f : 0.0f;
    obs.source = "lidar";
    obs.timestamp = now();
    awareness_pub_->publish(obs);
  }

  double min_range_, max_range_, sector_half_angle_;

  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::Obstacle>::SharedPtr awareness_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::ObstacleAwarenessNode>());
  rclcpp::shutdown();
  return 0;
}