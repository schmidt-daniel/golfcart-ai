#include <cmath>
#include <memory>
#include <string>

#include "golfcart_imu/imu_sensor.hpp"
#include "golfcart_imu/imu_sensor_impl.hpp"
#include "golfcart_imu/mock_imu_sensor.hpp"
#include "golfcart_msgs/msg/imu_data.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// IMU node: reads the IMU and publishes ImuData (roll/pitch) on /imu/data.
class ImuNode : public rclcpp::Node
{
public:
  ImuNode()
  : Node("imu_node")
  {
    const std::string impl = declare_parameter<std::string>("implementation", "mock");
    const std::string device = declare_parameter<std::string>("device", "/dev/i2c-1");

    if (impl == "mock") {
      mock_ = std::make_shared<MockImuSensor>();
      sensor_ = mock_;
    } else if (impl == "real") {
      sensor_ = std::make_shared<ImuSensorImpl>(device);
    } else {
      RCLCPP_FATAL(get_logger(), "Unknown implementation '%s'", impl.c_str());
      throw std::runtime_error("Unknown IMU implementation");
    }

    pub_ = create_publisher<golfcart_msgs::msg::ImuData>(
      "imu/data", rclcpp::SensorDataQoS());

    timer_ = create_wall_timer(
      std::chrono::milliseconds(20),  // 50 Hz
      [this]() { publish_sample(); });
  }

private:
  void publish_sample()
  {
    const ImuSample s = sensor_->read();
    golfcart_msgs::msg::ImuData msg;
    msg.valid = s.valid;
    msg.timestamp = now();

    if (s.valid) {
      // Compute roll/pitch from accelerometer (gravity direction).
      // roll = atan2(ay, az), pitch = atan2(-ax, sqrt(ay^2 + az^2))
      msg.roll_rad = std::atan2(s.accel_y, s.accel_z);
      msg.pitch_rad = std::atan2(
        -s.accel_x, std::sqrt(s.accel_y * s.accel_y + s.accel_z * s.accel_z));
    } else {
      msg.roll_rad = 0.0f;
      msg.pitch_rad = 0.0f;
    }

    pub_->publish(msg);
  }

  std::shared_ptr<ImuSensor> sensor_;
  std::shared_ptr<MockImuSensor> mock_;
  rclcpp::Publisher<golfcart_msgs::msg::ImuData>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::ImuNode>());
  rclcpp::shutdown();
  return 0;
}