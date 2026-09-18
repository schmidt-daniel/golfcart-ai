#include <memory>

#include "golfcart_msgs/msg/battery_state.hpp"
#include "golfcart_msgs/msg/capability_status.hpp"
#include "golfcart_msgs/msg/gps_fix.hpp"
#include "golfcart_msgs/msg/imu_data.hpp"
#include "golfcart_msgs/msg/motor_state.hpp"
#include "golfcart_msgs/msg/pose_keypoints.hpp"
#include "golfcart_system/capability_math.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

// Capability node: tracks which hardware is present and publishes a
// CapabilityStatus on /capability/status.
//
// Each sensor is tracked via heartbeat + valid flag (see CapabilityTracker).
// Config overrides in golfcart.yaml can force a capability present/absent for
// testing without hardware.
//
// Downstream nodes (mode, summon, follow, safety) subscribe to /capability/status
// and gate features that require specific sensors. Manual driving always works.
class CapabilityNode : public rclcpp::Node
{
public:
  CapabilityNode()
  : Node("capability_node")
  {
    const double timeout_s = declare_parameter<double>("heartbeat_timeout_s", 2.0);
    const double rate_hz = declare_parameter<double>("publish_rate_hz", 5.0);

    // Config overrides: -1 = auto-detect, 0 = force absent, 1 = force present.
    override_lidar_h_ = declare_parameter<int>("override_lidar_horizontal", -1);
    override_lidar_t_ = declare_parameter<int>("override_lidar_tilted", -1);
    override_gps_ = declare_parameter<int>("override_gps", -1);
    override_imu_ = declare_parameter<int>("override_imu", -1);
    override_battery_ = declare_parameter<int>("override_battery", -1);
    override_camera_ = declare_parameter<int>("override_camera", -1);
    override_coral_ = declare_parameter<int>("override_coral", -1);
    override_odrive_ = declare_parameter<int>("override_odrive", -1);

    lidar_h_ = CapabilityTracker(timeout_s);
    lidar_t_ = CapabilityTracker(timeout_s);
    gps_ = CapabilityTracker(timeout_s);
    imu_ = CapabilityTracker(timeout_s);
    battery_ = CapabilityTracker(timeout_s);
    camera_ = CapabilityTracker(timeout_s);
    coral_ = CapabilityTracker(timeout_s);
    odrive_ = CapabilityTracker(timeout_s);

    // Subscribe to sensor topics for heartbeat + valid detection.
    gps_sub_ = create_subscription<golfcart_msgs::msg::GpsFix>(
      "gps/fix", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::GpsFix::SharedPtr msg) {
        gps_.update(now_sec(), msg->valid);
      });

    imu_sub_ = create_subscription<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ImuData::SharedPtr msg) {
        imu_.update(now_sec(), msg->valid);
      });

    battery_sub_ = create_subscription<golfcart_msgs::msg::BatteryState>(
      "battery/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::BatteryState::SharedPtr msg) {
        battery_.update(now_sec(), msg->valid);
      });

    lidar_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
      "scan", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::LaserScan::SharedPtr) {
        // A LaserScan has no explicit valid flag; presence = heartbeat.
        lidar_h_.update(now_sec(), true);
      });

    odrive_sub_ = create_subscription<golfcart_msgs::msg::MotorState>(
      "motor/state", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::MotorState::SharedPtr) {
        // MotorState has no explicit valid flag; presence = heartbeat.
        odrive_.update(now_sec(), true);
      });

    // Camera: presence of /camera/image frames = heartbeat (no valid flag).
    camera_sub_ = create_subscription<sensor_msgs::msg::Image>(
      "camera/image", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::Image::SharedPtr) {
        camera_.update(now_sec(), true);
      });

    // Coral: presence of /pose/keypoints frames = heartbeat (the Coral pose
    // node only publishes when the EdgeTPU + a model are working). The valid
    // flag reflects whether the last inference was confident enough.
    coral_sub_ = create_subscription<golfcart_msgs::msg::PoseKeypoints>(
      "pose/keypoints", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::PoseKeypoints::SharedPtr msg) {
        coral_.update(now_sec(), msg->valid);
      });

    status_pub_ = create_publisher<golfcart_msgs::msg::CapabilityStatus>(
      "capability/status", rclcpp::SensorDataQoS());

    const auto period = std::chrono::duration<double>(1.0 / rate_hz);
    timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(period),
      [this]() { publish_status(); });
  }

private:
  double now_sec() const
  {
    return now().seconds();
  }

  // Apply a config override (-1 auto, 0 absent, 1 present) to a tracker.
  bool apply_override(const CapabilityTracker & t, int override)
  {
    if (override == 0) {
      return false;
    }
    if (override == 1) {
      return true;
    }
    return t.present(now_sec());
  }

  void publish_status()
  {
    golfcart_msgs::msg::CapabilityStatus msg;
    msg.lidar_horizontal = apply_override(lidar_h_, override_lidar_h_);
    msg.lidar_tilted = apply_override(lidar_t_, override_lidar_t_);
    msg.gps = apply_override(gps_, override_gps_);
    msg.imu = apply_override(imu_, override_imu_);
    msg.battery = apply_override(battery_, override_battery_);
    msg.camera = apply_override(camera_, override_camera_);
    msg.coral = apply_override(coral_, override_coral_);
    msg.odrive = apply_override(odrive_, override_odrive_);
    msg.stamp = now();
    status_pub_->publish(msg);
  }

  CapabilityTracker lidar_h_;
  CapabilityTracker lidar_t_;
  CapabilityTracker gps_;
  CapabilityTracker imu_;
  CapabilityTracker battery_;
  CapabilityTracker camera_;
  CapabilityTracker coral_;
  CapabilityTracker odrive_;

  int override_lidar_h_ = -1;
  int override_lidar_t_ = -1;
  int override_gps_ = -1;
  int override_imu_ = -1;
  int override_battery_ = -1;
  int override_camera_ = -1;
  int override_coral_ = -1;
  int override_odrive_ = -1;

  rclcpp::Subscription<golfcart_msgs::msg::GpsFix>::SharedPtr gps_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::ImuData>::SharedPtr imu_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::BatteryState>::SharedPtr battery_sub_;
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr lidar_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::MotorState>::SharedPtr odrive_sub_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr camera_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::PoseKeypoints>::SharedPtr coral_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::CapabilityStatus>::SharedPtr status_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::CapabilityNode>());
  rclcpp::shutdown();
  return 0;
}