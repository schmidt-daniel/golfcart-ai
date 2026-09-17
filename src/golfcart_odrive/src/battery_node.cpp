#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>

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
    shunt_resistance_ohm_ = declare_parameter<double>("shunt_resistance_ohm", 0.01);
    max_current_a_ = declare_parameter<double>("max_current_a", 40.0);

    pub_ = create_publisher<golfcart_msgs::msg::BatteryState>(
      "battery/state", rclcpp::SensorDataQoS());

    timer_ = create_wall_timer(
      std::chrono::milliseconds(1000),
      [this]() { publish_state(); });
  }

  ~BatteryNode() override
  {
    close_device();
  }

private:
  bool initialize_device()
  {
    close_device();
    fd_ = open(i2c_device_.c_str(), O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
      return false;
    }
    if (ioctl(fd_, I2C_SLAVE, i2c_addr_) < 0) {
      close_device();
      return false;
    }

    // 32 V bus range, 320 mV shunt range, 12-bit conversions, continuous mode.
    if (!write_register(0x00, 0x399F)) {
      close_device();
      return false;
    }

    if (shunt_resistance_ohm_ <= 0.0 || max_current_a_ <= 0.0) {
      close_device();
      return false;
    }
    current_lsb_a_ = max_current_a_ / 32767.0;
    const double calibration = 0.04096 / (current_lsb_a_ * shunt_resistance_ohm_);
    if (calibration < 1.0 || calibration > 65535.0) {
      close_device();
      return false;
    }
    return write_register(0x05, static_cast<uint16_t>(calibration));
  }

  void close_device()
  {
    if (fd_ >= 0) {
      close(fd_);
      fd_ = -1;
    }
  }

  bool write_register(uint8_t reg, uint16_t value)
  {
    const uint8_t data[3] = {
      reg,
      static_cast<uint8_t>((value >> 8) & 0xFF),
      static_cast<uint8_t>(value & 0xFF)};
    return write(fd_, data, sizeof(data)) == static_cast<ssize_t>(sizeof(data));
  }

  bool read_register(uint8_t reg, uint16_t & value)
  {
    if (write(fd_, &reg, sizeof(reg)) != static_cast<ssize_t>(sizeof(reg))) {
      return false;
    }
    uint8_t data[2] = {};
    if (read(fd_, data, sizeof(data)) != static_cast<ssize_t>(sizeof(data))) {
      return false;
    }
    value = static_cast<uint16_t>(data[0] << 8 | data[1]);
    return true;
  }

  void publish_state()
  {
    golfcart_msgs::msg::BatteryState msg;
    msg.valid = false;
    msg.voltage_v = 0.0f;
    msg.current_a = 0.0f;
    msg.charge_percent = 0.0f;
    msg.timestamp = now();

    if (fd_ < 0 && !initialize_device()) {
      RCLCPP_WARN_THROTTLE(
        get_logger(), *get_clock(), 5000,
        "Unable to initialize INA219 at %s address 0x%02x: %s",
        i2c_device_.c_str(), i2c_addr_, std::strerror(errno));
      pub_->publish(msg);
      return;
    }

    uint16_t bus_reg = 0;
    uint16_t current_reg = 0;
    if (read_register(0x02, bus_reg) && read_register(0x04, current_reg)) {
      if ((bus_reg & 0x0002U) != 0U) {
        RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000, "INA219 bus conversion overflow");
        close_device();
        pub_->publish(msg);
        return;
      }
      // INA219: bus voltage = (bus_reg >> 3) * 0.004 V
      //          current = signed current register * configured current LSB
      // Apply the voltage divider scale factor (see voltage_scale_).
      const double voltage = static_cast<double>(bus_reg >> 3) * 0.004 * voltage_scale_;
      const auto signed_current_reg = static_cast<int16_t>(current_reg);
      const double current = static_cast<double>(signed_current_reg) * current_lsb_a_;
      msg.voltage_v = static_cast<float>(voltage);
      msg.current_a = static_cast<float>(current);
      msg.valid = true;

      // Simple linear charge estimate between critical and full voltage.
      if (voltage <= critical_voltage_) {
        msg.charge_percent = 0.0f;
      } else if (voltage >= full_voltage_) {
        msg.charge_percent = 100.0f;
      } else {
        msg.charge_percent = static_cast<float>(
          std::clamp(
            (voltage - critical_voltage_) / (full_voltage_ - critical_voltage_) * 100.0,
            0.0, 100.0));
      }
    } else {
      close_device();
    }

    pub_->publish(msg);
  }

  std::string i2c_device_;
  int i2c_addr_ = 0x40;
  double low_voltage_ = 30.0;
  double critical_voltage_ = 28.0;
  double full_voltage_ = 42.0;
  double voltage_scale_ = 2.0;
  double shunt_resistance_ohm_ = 0.01;
  double max_current_a_ = 40.0;
  double current_lsb_a_ = 0.0;
  int fd_ = -1;

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