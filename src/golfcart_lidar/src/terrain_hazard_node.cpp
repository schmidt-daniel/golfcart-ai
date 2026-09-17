#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

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
    drop_off_range_m_ = declare_parameter<double>("drop_off_range_m", 6.0);
    near_hazard_range_m_ = declare_parameter<double>("near_hazard_range_m", 0.35);
    min_valid_fraction_ = declare_parameter<double>("min_valid_fraction", 0.5);
    hazard_fraction_ = declare_parameter<double>("hazard_fraction", 0.25);
    roughness_threshold_m_ = declare_parameter<double>("roughness_threshold_m", 0.35);
    range_jump_threshold_m_ = declare_parameter<double>("range_jump_threshold_m", 1.0);
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
  struct Assessment
  {
    uint8_t type = golfcart_msgs::msg::TerrainHazard::NONE;
    double confidence = 0.0;
    double coverage = 0.0;
    double nearest_range_m = 0.0;
    bool candidate = false;
  };

  Assessment assess_scan(const sensor_msgs::msg::LaserScan & scan) const
  {
    Assessment assessment;
    std::vector<double> ranges;
    size_t considered = 0;
    size_t far_count = 0;
    size_t near_count = 0;
    size_t jump_count = 0;
    double previous_range = 0.0;
    bool have_previous = false;

    for (size_t index = 0; index < scan.ranges.size(); ++index) {
      const double angle = scan.angle_min + index * scan.angle_increment;
      double wrapped = std::abs(angle);
      wrapped = std::min(wrapped, 2.0 * M_PI - wrapped);
      if (wrapped > forward_half_angle_rad_) {
        continue;
      }
      ++considered;
      const double range = scan.ranges[index];
      if (!std::isfinite(range) || range < min_range_m_ || range > max_range_m_) {
        continue;
      }

      ranges.push_back(range);
      assessment.nearest_range_m =
        ranges.size() == 1 ? range : std::min(assessment.nearest_range_m, range);
      if (range >= drop_off_range_m_) {
        ++far_count;
      }
      if (range <= near_hazard_range_m_) {
        ++near_count;
      }
      if (have_previous && std::abs(range - previous_range) >= range_jump_threshold_m_) {
        ++jump_count;
      }
      previous_range = range;
      have_previous = true;
    }

    if (considered == 0) {
      return assessment;
    }
    assessment.coverage = static_cast<double>(ranges.size()) / considered;
    const double far_fraction = static_cast<double>(far_count) / considered;
    const double near_fraction = static_cast<double>(near_count) / considered;
    const double jump_fraction =
      ranges.size() > 1 ? static_cast<double>(jump_count) / (ranges.size() - 1) : 0.0;

    if (assessment.coverage < min_valid_fraction_) {
      assessment.type = golfcart_msgs::msg::TerrainHazard::NO_RETURN;
      assessment.confidence = 1.0 - assessment.coverage;
      assessment.candidate = true;
      return assessment;
    }
    if (far_fraction >= hazard_fraction_) {
      assessment.type = golfcart_msgs::msg::TerrainHazard::DROP_OFF;
      assessment.confidence = far_fraction;
      assessment.candidate = true;
      return assessment;
    }
    if (near_fraction >= hazard_fraction_) {
      assessment.type = golfcart_msgs::msg::TerrainHazard::OBSTRUCTION;
      assessment.confidence = near_fraction;
      assessment.candidate = true;
      return assessment;
    }

    const double mean = std::accumulate(ranges.begin(), ranges.end(), 0.0) / ranges.size();
    double variance = 0.0;
    for (const double range : ranges) {
      const double delta = range - mean;
      variance += delta * delta;
    }
    const double roughness = std::sqrt(variance / ranges.size());
    if (roughness >= roughness_threshold_m_ || jump_fraction >= hazard_fraction_) {
      assessment.type = golfcart_msgs::msg::TerrainHazard::ROUGH_GROUND;
      assessment.confidence = std::min(
        1.0, std::max(roughness / roughness_threshold_m_, jump_fraction / hazard_fraction_));
      assessment.candidate = true;
    }
    return assessment;
  }

  void process_scan(const sensor_msgs::msg::LaserScan::SharedPtr scan)
  {
    const Assessment assessment = assess_scan(*scan);
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
  double drop_off_range_m_ = 6.0;
  double near_hazard_range_m_ = 0.35;
  double min_valid_fraction_ = 0.5;
  double hazard_fraction_ = 0.25;
  double roughness_threshold_m_ = 0.35;
  double range_jump_threshold_m_ = 1.0;
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
