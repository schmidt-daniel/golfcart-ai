#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "golfcart_lidar/terrain_math.hpp"
#include "golfcart_msgs/msg/terrain_hazard.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

class TerrainHazardNode : public rclcpp::Node
{
public:
  TerrainHazardNode()
  : Node("terrain_hazard_node")
  {
    const std::string input_topic = declare_parameter<std::string>(
      "input_topic", "scan_tilted");
    forward_half_angle_rad_ = declare_parameter<double>(
      "forward_half_angle_rad", 0.61);
    min_range_m_ = declare_parameter<double>("min_range_m", 0.1);
    max_range_m_ = declare_parameter<double>("max_range_m", 12.0);
    mount_height_m_ = declare_parameter<double>("mount_height_m", 0.65);
    tilt_rad_ = declare_parameter<double>("tilt_rad", 0.436);
    drop_off_residual_m_ = declare_parameter<double>("drop_off_residual_m", 0.5);
    obstacle_residual_m_ = declare_parameter<double>("obstacle_residual_m", 0.5);
    min_valid_fraction_ = declare_parameter<double>("min_valid_fraction", 0.5);
    hazard_fraction_ = declare_parameter<double>("hazard_fraction", 0.25);
    roughness_threshold_m_ = declare_parameter<double>("roughness_threshold_m", 0.35);
    confirm_scans_ = declare_parameter<int>("confirm_scans", 3);
    clear_scans_ = declare_parameter<int>("clear_scans", 3);

    hazard_pub_ = create_publisher<golfcart_msgs::msg::TerrainHazard>(
      "terrain/hazard", rclcpp::SensorDataQoS());
    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
      input_topic, rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::LaserScan::SharedPtr msg) {
        process_scan(msg);
      });
  }

private:
  void process_scan(const sensor_msgs::msg::LaserScan::SharedPtr scan)
  {
    std::vector<double> ranges;
    std::vector<double> angles;
    ranges.reserve(scan->ranges.size());
    angles.reserve(scan->ranges.size());

    for (size_t index = 0; index < scan->ranges.size(); ++index) {
      const double angle = scan->angle_min + index * scan->angle_increment;
      double wrapped = std::abs(angle);
      wrapped = std::min(wrapped, 2.0 * M_PI - wrapped);
      if (wrapped > forward_half_angle_rad_) {
        continue;
      }
      const double range = scan->ranges[index];
      if (!std::isfinite(range) || range < min_range_m_ || range > max_range_m_) {
        continue;
      }
      ranges.push_back(range);
      angles.push_back(angle);
    }

    const TerrainAssessment assessment = assess_terrain(
      ranges, angles, mount_height_m_, tilt_rad_, drop_off_residual_m_,
      obstacle_residual_m_, min_valid_fraction_, hazard_fraction_,
      roughness_threshold_m_);

    if (assessment.candidate) {
      ++hazard_streak_;
      clear_streak_ = 0;
      if (hazard_streak_ >= std::max(1, confirm_scans_)) {
        active_type_ = assessment.type;
        active_confidence_ = assessment.confidence;
      }
    } else {
      hazard_streak_ = 0;
      ++clear_streak_;
      if (clear_streak_ >= std::max(1, clear_scans_)) {
        active_type_ = golfcart_msgs::msg::TerrainHazard::NONE;
        active_confidence_ = 0.0;
      }
    }

    golfcart_msgs::msg::TerrainHazard msg;
    msg.hazard_type = active_type_;
    msg.hazard = active_type_ != golfcart_msgs::msg::TerrainHazard::NONE;
    msg.valid = true;
    msg.confidence = static_cast<float>(
      msg.hazard ? std::min(1.0, active_confidence_) : 0.0);
    msg.coverage = static_cast<float>(assessment.coverage);
    msg.nearest_range_m = static_cast<float>(assessment.nearest_range_m);
    msg.timestamp = now();
    hazard_pub_->publish(msg);
  }

  double forward_half_angle_rad_ = 0.61;
  double min_range_m_ = 0.1;
  double max_range_m_ = 12.0;
  double mount_height_m_ = 0.65;
  double tilt_rad_ = 0.436;
  double drop_off_residual_m_ = 0.5;
  double obstacle_residual_m_ = 0.5;
  double min_valid_fraction_ = 0.5;
  double hazard_fraction_ = 0.25;
  double roughness_threshold_m_ = 0.35;
  int confirm_scans_ = 3;
  int clear_scans_ = 3;
  int hazard_streak_ = 0;
  int clear_streak_ = 0;
  uint8_t active_type_ = golfcart_msgs::msg::TerrainHazard::NONE;
  double active_confidence_ = 0.0;

  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::TerrainHazard>::SharedPtr hazard_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::TerrainHazardNode>());
  rclcpp::shutdown();
  return 0;
}
