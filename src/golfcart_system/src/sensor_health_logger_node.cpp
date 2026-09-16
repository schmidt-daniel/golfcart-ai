#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "golfcart_msgs/msg/capability_status.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Sensor Health Logger node.
// Subscribes to /capability/status and logs capability dropouts (present ->
// absent) to a JSON Lines file, so intermittent sensor failures can be
// diagnosed. Each entry records which sensor dropped, when, and the full
// capability snapshot.
class SensorHealthLoggerNode : public rclcpp::Node
{
public:
  SensorHealthLoggerNode()
  : Node("sensor_health_logger")
  {
    log_file_ = declare_parameter<std::string>("log_file", "/var/lib/golfcart/sensor_health.jsonl");

    cap_sub_ = create_subscription<golfcart_msgs::msg::CapabilityStatus>(
      "capability/status", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg) {
        on_capability(msg);
      });
  }

private:
  // Sensor names in the same order as the CapabilityStatus fields.
  static const std::vector<std::string> & sensor_names()
  {
    static const std::vector<std::string> names = {
      "lidar_horizontal", "lidar_tilted", "gps", "imu",
      "battery", "camera", "coral", "odrive"};
    return names;
  }

  void on_capability(const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg)
  {
    const std::vector<bool> present = {
      msg->lidar_horizontal, msg->lidar_tilted, msg->gps, msg->imu,
      msg->battery, msg->camera, msg->coral, msg->odrive};

    // Detect present -> absent transitions (dropouts).
    for (size_t i = 0; i < present.size(); ++i) {
      const bool was_present = prev_present_[i];
      if (was_present && !present[i]) {
        append_log(sensor_names()[i], "absent", msg);
      } else if (!was_present && present[i]) {
        append_log(sensor_names()[i], "present", msg);
      }
    }
    prev_present_ = present;
  }

  void append_log(const std::string & sensor, const std::string & event,
                  const golfcart_msgs::msg::CapabilityStatus::SharedPtr msg)
  {
    std::ofstream f(log_file_, std::ios::app);
    if (!f.is_open()) {
      RCLCPP_WARN(get_logger(), "Could not open sensor health log %s",
                  log_file_.c_str());
      return;
    }
    // JSON Lines: one event per line.
    f << "{\"sensor\":\"" << sensor << "\",\"event\":\"" << event
      << "\",\"ts\":" << now().seconds()
      << ",\"lidar_horizontal\":" << (msg->lidar_horizontal ? 1 : 0)
      << ",\"lidar_tilted\":" << (msg->lidar_tilted ? 1 : 0)
      << ",\"gps\":" << (msg->gps ? 1 : 0)
      << ",\"imu\":" << (msg->imu ? 1 : 0)
      << ",\"battery\":" << (msg->battery ? 1 : 0)
      << ",\"camera\":" << (msg->camera ? 1 : 0)
      << ",\"coral\":" << (msg->coral ? 1 : 0)
      << ",\"odrive\":" << (msg->odrive ? 1 : 0)
      << "}\n";
    f.close();
  }

  std::string log_file_ = "/var/lib/golfcart/sensor_health.jsonl";
  std::vector<bool> prev_present_ = std::vector<bool>(8, false);

  rclcpp::Subscription<golfcart_msgs::msg::CapabilityStatus>::SharedPtr cap_sub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SensorHealthLoggerNode>());
  rclcpp::shutdown();
  return 0;
}