#include <memory>
#include <string>

#include "golfcart_gps/gps_sensor.hpp"
#include "golfcart_gps/gps_sensor_impl.hpp"
#include "golfcart_gps/mock_gps_sensor.hpp"
#include "golfcart_msgs/msg/gps_fix.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// GPS node: reads the GPS and publishes GpsFix on /gps/fix.
class GpsNode : public rclcpp::Node
{
public:
  GpsNode()
  : Node("gps_node")
  {
    const std::string impl = declare_parameter<std::string>("implementation", "mock");
    const std::string device = declare_parameter<std::string>("device", "/dev/ttyUSB0");

    if (impl == "mock") {
      mock_ = std::make_shared<MockGpsSensor>();
      sensor_ = mock_;
    } else if (impl == "real") {
      sensor_ = std::make_shared<GpsSensorImpl>(device);
    } else {
      RCLCPP_FATAL(get_logger(), "Unknown implementation '%s'", impl.c_str());
      throw std::runtime_error("Unknown GPS implementation");
    }

    pub_ = create_publisher<golfcart_msgs::msg::GpsFix>(
      "gps/fix", rclcpp::SensorDataQoS());

    timer_ = create_wall_timer(
      std::chrono::milliseconds(1000),  // 1 Hz
      [this]() { publish_fix(); });
  }

private:
  void publish_fix()
  {
    const GpsSample s = sensor_->read();
    golfcart_msgs::msg::GpsFix msg;
    msg.latitude_deg = s.latitude_deg;
    msg.longitude_deg = s.longitude_deg;
    msg.altitude_m = s.altitude_m;
    msg.speed_mps = s.speed_mps;
    msg.heading_rad = s.heading_rad;
    msg.valid = s.valid;
    msg.timestamp = now();
    pub_->publish(msg);
  }

  std::shared_ptr<GpsSensor> sensor_;
  std::shared_ptr<MockGpsSensor> mock_;
  rclcpp::Publisher<golfcart_msgs::msg::GpsFix>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::GpsNode>());
  rclcpp::shutdown();
  return 0;
}