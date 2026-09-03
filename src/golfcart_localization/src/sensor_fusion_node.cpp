#include <memory>

#include "golfcart_msgs/msg/gps_fix.hpp"
#include "golfcart_msgs/msg/imu_data.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "tf2/LinearMath/Quaternion.hpp"

namespace golfcart
{

// Sensor Fusion bridge node.
// Converts the custom IMU (ImuData) and GPS (GpsFix) messages into the standard
// sensor_msgs/Imu and sensor_msgs/NavSatFix messages expected by the
// robot_localization EKF. The EKF fuses these with /wheel/odometry.
//
// Publishes:
//   /imu/data_raw   (sensor_msgs/Imu)      <- from /imu/data
//   /gps/fix_std    (sensor_msgs/NavSatFix) <- from /gps/fix
//
// The robot_localization ekf_node subscribes to /wheel/odometry, /imu/data_raw,
// and /gps/fix_std and publishes the fused pose.
class SensorFusionNode : public rclcpp::Node
{
public:
  SensorFusionNode()
  : Node("sensor_fusion_node")
  {
    imu_sub_ = create_subscription<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::ImuData::SharedPtr msg) {
        handle_imu(msg);
      });

    gps_sub_ = create_subscription<golfcart_msgs::msg::GpsFix>(
      "gps/fix", rclcpp::SensorDataQoS(),
      [this](const golfcart_msgs::msg::GpsFix::SharedPtr msg) {
        handle_gps(msg);
      });

    imu_pub_ = create_publisher<sensor_msgs::msg::Imu>(
      "imu/data_raw", rclcpp::SensorDataQoS());
    gps_pub_ = create_publisher<sensor_msgs::msg::NavSatFix>(
      "gps/fix_std", rclcpp::SensorDataQoS());
  }

private:
  void handle_imu(const golfcart_msgs::msg::ImuData::SharedPtr msg)
  {
    if (!msg->valid) {
      return;
    }
    sensor_msgs::msg::Imu out;
    out.header.stamp = msg->timestamp;
    out.header.frame_id = "imu_link";
    // The custom ImuData only carries roll/pitch inclination. Set orientation
    // from roll/pitch (yaw unknown here; the EKF fuses yaw from odometry).
    // For a flat trolley, roll/pitch map directly to the orientation quaternion.
    // Yaw is left at 0 (the EKF uses odometry for yaw).
    tf2::Quaternion q;
    q.setRPY(msg->roll_rad, msg->pitch_rad, 0.0);
    out.orientation.x = q.x();
    out.orientation.y = q.y();
    out.orientation.z = q.z();
    out.orientation.w = q.w();
    imu_pub_->publish(out);
  }

  void handle_gps(const golfcart_msgs::msg::GpsFix::SharedPtr msg)
  {
    if (!msg->valid) {
      return;
    }
    sensor_msgs::msg::NavSatFix out;
    out.header.stamp = msg->timestamp;
    out.header.frame_id = "gps_link";
    out.latitude = msg->latitude_deg;
    out.longitude = msg->longitude_deg;
    out.altitude = msg->altitude_m;
    out.status.status = sensor_msgs::msg::NavSatStatus::STATUS_FIX;
    out.status.service = sensor_msgs::msg::NavSatStatus::SERVICE_GPS;
    gps_pub_->publish(out);
  }

  rclcpp::Subscription<golfcart_msgs::msg::ImuData>::SharedPtr imu_sub_;
  rclcpp::Subscription<golfcart_msgs::msg::GpsFix>::SharedPtr gps_sub_;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;
  rclcpp::Publisher<sensor_msgs::msg::NavSatFix>::SharedPtr gps_pub_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SensorFusionNode>());
  rclcpp::shutdown();
  return 0;
}