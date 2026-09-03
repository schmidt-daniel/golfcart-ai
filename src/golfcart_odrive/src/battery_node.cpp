#include <memory>
#include <string>

#include "golfcart_msgs/msg/battery_state.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Battery monitor node.
// Reads battery voltage/current from an INA219 over I2C and publishes
// BatteryState on /battery/state.
//
// NOTE: This uses the Linux I2C device interface (/dev/i2c-N). The INA219
// registers are accessed directly. Requires the I2C device to be accessible
// (enable I2C on the RPi and grant permissions).
class BatteryNode : public rclcpp::Node
{
public:
  BatteryNode()
  : Node("battery_node")
  {
    i2c_device_ = declare_parameter<std::string>("i2c_device", "/dev/i2c-1");
    i2c_addr_ = declare_parameter<int>("i2c_address", 0x40);
    low_voltage_ = declare_parameter<double>("low_voltage_v", 30.0);
    critical_voltage_ = declare_parameter<double>("critical_voltage_v", 28.0);
    full_voltage_ = declare_parameter<double>("full_voltage_v", 42.0);
    // Voltage divider scale factor. The INA219 max bus voltage is 26 V, but the
    // 36 V battery reaches ~42 V. A divider (e.g. R1=R2=100k, divide by 2) keeps
    // VBUS <= 26 V; the measured voltage is multiplied by this factor.
    voltage_scale_ = declare_parameter<double>("voltage_scale", 2.0);

    pub_ = create_publisher<golfcart_msgs::msg::BatteryState>(
      "battery/state", rclcpp::SensorDataQoS());

    timer_ = create_wall_timer(
      std::chrono::milliseconds(1000),
      [this]() { publish_state(); });
  }

private:
  // Read a 16-bit register from the INA219 via the Linux I2C interface.
  // Returns false on failure.
  bool read_register(uint8_t reg, uint16_t & value)
  {
    // TODO: Implement I2C read via /dev/i2c-N (ioctl I2C_SLAVE + read/write).
    // For now, return false so the node reports invalid battery state.
    (void)reg;
    (void)value;
    return false;
  }

  void publish_state()
  {
    golfcart_msgs::msg::BatteryState msg;
    msg.valid = false;
    msg.voltage_v = 0.0f;
    msg.current_a = 0.0f;
    msg.charge_percent = 0.0f;
    msg.timestamp = now();

    uint16_t bus_reg = 0;
    uint16_t shunt_reg = 0;
    if (read_register(0x02, bus_reg) && read_register(0x01, shunt_reg)) {
      // INA219: bus voltage = (bus_reg >> 3) * 0.004 V
      //          current = shunt_reg * calibration-dependent LSB
      // Apply the voltage divider scale factor (see voltage_scale_).
      const double voltage = static_cast<double>(bus_reg >> 3) * 0.004 * voltage_scale_;
      msg.voltage_v = static_cast<float>(voltage);
      msg.valid = true;

      // Simple linear charge estimate between critical and full voltage.
      if (voltage <= critical_voltage_) {
        msg.charge_percent = 0.0f;
      } else if (voltage >= full_voltage_) {
        msg.charge_percent = 100.0f;
      } else {
        msg.charge_percent = static_cast<float>(
          (voltage - critical_voltage_) / (full_voltage_ - critical_voltage_) * 100.0);
      }
    }

    pub_->publish(msg);
  }

  std::string i2c_device_;
  int i2c_addr_ = 0x40;
  double low_voltage_ = 30.0;
  double critical_voltage_ = 28.0;
  double full_voltage_ = 42.0;
  double voltage_scale_ = 2.0;

  rclcpp::Publisher<golfcart_msgs::msg::BatteryState>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::BatteryNode>());
  rclcpp::shutdown();
  return 0;
}