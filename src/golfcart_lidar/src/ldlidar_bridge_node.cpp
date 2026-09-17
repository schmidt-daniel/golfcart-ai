#include <cmath>
#include <limits>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

class LdLidarBridgeNode : public rclcpp::Node
{
public:
  LdLidarBridgeNode()
  : Node("ldlidar_bridge_node")
  {
    const std::string input_topic = declare_parameter<std::string>(
      "input_topic", "ldlidar/scan");
    const std::string output_topic = declare_parameter<std::string>(
      "output_topic", "scan");
    const std::string output_frame = declare_parameter<std::string>(
      "output_frame", "lidar_link");
    blind_center_rad_ = declare_parameter<double>(
      "blind_spot_center_rad", M_PI);
    blind_half_rad_ = declare_parameter<double>(
      "blind_spot_half_angle_rad", 0.0);

    scan_pub_ = create_publisher<sensor_msgs::msg::LaserScan>(
      output_topic, rclcpp::SensorDataQoS());
    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
      input_topic, rclcpp::SensorDataQoS(),
      [this, output_frame](const sensor_msgs::msg::LaserScan::SharedPtr msg) {
        auto output = *msg;
        output.header.frame_id = output_frame;
        for (size_t index = 0; index < output.ranges.size(); ++index) {
          const double angle = output.angle_min + index * output.angle_increment;
          if (in_blind_spot(angle)) {
            output.ranges[index] = std::numeric_limits<float>::infinity();
          }
        }
        scan_pub_->publish(output);
      });
  }

private:
  bool in_blind_spot(double angle) const
  {
    if (blind_half_rad_ <= 0.0) {
      return false;
    }
    double distance = std::abs(angle - blind_center_rad_);
    const double full_turn = 2.0 * M_PI;
    distance = std::min(distance, full_turn - distance);
    return distance <= blind_half_rad_;
  }

  double blind_center_rad_ = M_PI;
  double blind_half_rad_ = 0.0;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr scan_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::LdLidarBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
