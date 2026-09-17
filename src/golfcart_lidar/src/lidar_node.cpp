#include <limits>
#include <memory>
#include <string>

#include "golfcart_lidar/lidar_sensor.hpp"
#include "golfcart_lidar/mock_lidar_sensor.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

// LiDAR node: reads the LiDAR and publishes a standard LaserScan on /scan.
class LidarNode : public rclcpp::Node
{
public:
  LidarNode()
  : Node("lidar_node")
  {
    const std::string impl = declare_parameter<std::string>("implementation", "mock");
    // Blind spot: an angular window to ignore (e.g. the bag behind the
    // trolley). Center + half-width in radians; 0 = disabled.
    blind_center_rad_ = declare_parameter<double>("blind_spot_center_rad", M_PI);
    blind_half_rad_ = declare_parameter<double>("blind_spot_half_angle_rad", 0.0);

    if (impl == "mock") {
      mock_ = std::make_shared<MockLidarSensor>();
      sensor_ = mock_;
    } else {
      RCLCPP_FATAL(
        get_logger(),
        "Unknown implementation '%s'; use ldlidar_ros2_node for hardware",
        impl.c_str());
      throw std::runtime_error("Unknown LiDAR implementation");
    }

    pub_ = create_publisher<sensor_msgs::msg::LaserScan>(
      "scan", rclcpp::SensorDataQoS());

    timer_ = create_wall_timer(
      std::chrono::milliseconds(100),  // 10 Hz
      [this]() { publish_scan(); });
  }

private:
  // True if the given angle (rad) falls inside the blind-spot window.
  bool in_blind_spot(double angle) const
  {
    if (blind_half_rad_ <= 0.0) {
      return false;
    }
    double d = std::abs(angle - blind_center_rad_);
    d = std::min(d, 2.0 * M_PI - d);  // wrap-around
    return d <= blind_half_rad_;
  }

  void publish_scan()
  {
    const LidarScan scan = sensor_->read();
    if (scan.points.empty()) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000, "LiDAR scan contains no points");
      return;
    }
    sensor_msgs::msg::LaserScan msg;
    msg.header.stamp = now();
    msg.header.frame_id = "lidar_link";
    msg.angle_min = 0.0;
    msg.angle_max = 2.0 * M_PI;
    msg.angle_increment = 2.0 * M_PI / scan.points.size();
    msg.range_min = 0.1;
    msg.range_max = 12.0;
    msg.ranges.reserve(scan.points.size());
    for (const auto & p : scan.points) {
      // Mask the blind spot (bag behind the trolley) to infinity.
      if (in_blind_spot(p.angle_rad)) {
        msg.ranges.push_back(std::numeric_limits<float>::infinity());
      } else {
        msg.ranges.push_back(p.valid ? static_cast<float>(p.range_m) : std::numeric_limits<float>::infinity());
      }
    }
    pub_->publish(msg);
  }

  double blind_center_rad_;
  double blind_half_rad_;
  std::shared_ptr<LidarSensor> sensor_;
  std::shared_ptr<MockLidarSensor> mock_;
  rclcpp::Publisher<sensor_msgs::msg::LaserScan>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::LidarNode>());
  rclcpp::shutdown();
  return 0;
}