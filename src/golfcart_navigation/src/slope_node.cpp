#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "golfcart_msgs/msg/course_selected.hpp"
#include "golfcart_msgs/msg/slope_status.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "rclcpp/rclcpp.hpp"

#include "course_zip.hpp"

namespace golfcart
{

// Slope node.
// Computes the trolley-relative roll/pitch from the terrain gradient and the
// trolley's heading (yaw), and publishes the predicted slope at the trolley's
// current position on /slope/status for the safety controller.
//
// Roll and pitch are trolley-relative: they depend on the heading. The editor
// exports the ground-fixed gradient (dz/dx, dz/dy) as float PGM files; this
// node projects it onto the fused-pose yaw:
//     pitch = gradient . forward
//     roll  = gradient . lateral
class SlopeNode : public rclcpp::Node
{
public:
  SlopeNode()
  : Node("slope_node")
  {
    selected_sub_ = create_subscription<golfcart_msgs::msg::CourseSelected>(
      "course/selected", rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal),
      [this](const golfcart_msgs::msg::CourseSelected::SharedPtr msg) { on_selected(msg); });
    odom_sub_ = create_subscription<nav_msgs::msg::Odometry>(
      "odometry/filtered", rclcpp::SensorDataQoS(),
      [this](const nav_msgs::msg::Odometry::SharedPtr msg) { on_odom(msg); });

    slope_pub_ = create_publisher<golfcart_msgs::msg::SlopeStatus>(
      "slope/status", rclcpp::SensorDataQoS());
  }

private:
  rclcpp::Subscription<golfcart_msgs::msg::CourseSelected>::SharedPtr selected_sub_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::SlopeStatus>::SharedPtr slope_pub_;

  // Gradient grid (ground-fixed), row-major [row][col], row 0 = north.
  std::vector<float> gradx_;
  std::vector<float> grady_;
  int grid_rows_ = 0, grid_cols_ = 0;
  double resolution_ = 0.0;
  double origin_x_ = 0.0, origin_y_ = 0.0;
  bool have_gradient_ = false;

  // Trolley pose (map frame).
  bool have_odom_ = false;
  double pos_x_ = 0.0, pos_y_ = 0.0, yaw_ = 0.0;

  void on_selected(const golfcart_msgs::msg::CourseSelected::SharedPtr msg)
  {
    load_gradient(msg->zip_path);
  }

  // Load the gradient PGM files (hole1_slope_gradx.pgm / _grady.pgm) from the
  // course zip. We look for the first hole's gradient.
  void load_gradient(const std::string & zip_path)
  {
    ZipReader zip;
    if (!zip.open(zip_path)) {
      RCLCPP_ERROR(get_logger(), "Cannot open course zip '%s'", zip_path.c_str());
      return;
    }
    // Find the first holeN_slope_gradx.pgm.
    std::string gradx_name, grady_name, grad_yaml;
    for (const ZipReader::Entry & e : zip.entries()) {
      if (e.name.rfind("holes/", 0) == 0 && e.name.find("_gradx.pgm") != std::string::npos) {
        gradx_name = e.name;
        grady_name = e.name.substr(0, e.name.size() - 9) + "grady.pgm";
        grad_yaml = e.name.substr(0, e.name.size() - 9) + "grad.yaml";
        break;
      }
    }
    if (gradx_name.empty()) {
      RCLCPP_WARN(get_logger(), "No gradient costmap in course zip '%s'", zip_path.c_str());
      return;
    }
    const std::string gx = zip.read(gradx_name);
    const std::string gy = zip.read(grady_name);
    const std::string gyaml = zip.read(grad_yaml);
    if (gx.empty() || gy.empty()) {
      RCLCPP_WARN(get_logger(), "Gradient files missing in course zip");
      return;
    }
    if (!parse_gradient_pgm(gx, gradx_) || !parse_gradient_pgm(gy, grady_)) {
      RCLCPP_ERROR(get_logger(), "Failed to parse gradient PGM");
      return;
    }
    // Parse the gradient YAML for resolution/origin.
    if (!gyaml.empty()) {
      YAML::Node root;
      try {
        root = YAML::Load(gyaml);
        resolution_ = root["resolution"] ? root["resolution"].as<double>() : 0.0;
        const YAML::Node & origin = root["origin"];
        if (origin && origin.IsSequence() && origin.size() >= 2) {
          origin_x_ = origin[0].as<double>();
          origin_y_ = origin[1].as<double>();
        }
      } catch (...) {
      }
    }
    have_gradient_ = true;
    RCLCPP_INFO(get_logger(), "Loaded gradient %dx%d @ %.1f m", grid_rows_, grid_cols_, resolution_);
  }

  // Parse a float32 PGM (P5, maxval 65535) into a flat row-major array.
  bool parse_gradient_pgm(const std::string & data, std::vector<float> & out)
  {
    // Header: "P5\n<cols> <rows>\n65535\n" then raw float32.
    size_t pos = 0;
    auto next_line = [&]() {
      size_t nl = data.find('\n', pos);
      if (nl == std::string::npos) {
        return std::string();
      }
      std::string line = data.substr(pos, nl - pos);
      pos = nl + 1;
      return line;
    };
    if (next_line() != "P5") {
      return false;
    }
    std::string dims = next_line();
    std::istringstream iss(dims);
    int cols, rows;
    if (!(iss >> cols >> rows)) {
      return false;
    }
    if (next_line() != "65535") {
      return false;
    }
    const size_t n = static_cast<size_t>(rows) * static_cast<size_t>(cols);
    if (data.size() - pos < n * sizeof(float)) {
      return false;
    }
    out.resize(n);
    std::memcpy(out.data(), data.data() + pos, n * sizeof(float));
    grid_rows_ = rows;
    grid_cols_ = cols;
    return true;
  }

  void on_odom(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    have_odom_ = true;
    pos_x_ = msg->pose.pose.position.x;
    pos_y_ = msg->pose.pose.position.y;
    // Yaw from the quaternion.
    const auto & q = msg->pose.pose.orientation;
    const double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
    const double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
    yaw_ = std::atan2(siny_cosp, cosy_cosp);
    publish_slope();
  }

  void publish_slope()
  {
    golfcart_msgs::msg::SlopeStatus msg;
    msg.valid = false;
    msg.timestamp = now();
    if (!have_gradient_ || !have_odom_ || grid_rows_ == 0 || grid_cols_ == 0) {
      slope_pub_->publish(msg);
      return;
    }
    // Map-frame position -> grid cell (row 0 = north, col 0 = west).
    const double gx = pos_x_ - origin_x_;
    const double gy = pos_y_ - origin_y_;
    int col = static_cast<int>(std::floor(gx / resolution_));
    int row = static_cast<int>(std::floor(gy / resolution_));
    // Clamp to grid.
    col = std::max(0, std::min(grid_cols_ - 1, col));
    row = std::max(0, std::min(grid_rows_ - 1, row));
    const size_t idx = static_cast<size_t>(row) * grid_cols_ + col;

    const double dzdx = gradx_[idx];
    const double dzdy = grady_[idx];

    // Project onto the trolley heading (ROS yaw: 0 = +x/east, CCW positive).
    const double cy = std::cos(yaw_);
    const double sy = std::sin(yaw_);
    const double pitch = dzdx * cy + dzdy * sy;   // slope along forward
    const double roll = -dzdx * sy + dzdy * cy;   // slope along lateral

    msg.roll_rad = static_cast<float>(std::atan(roll));
    msg.pitch_rad = static_cast<float>(std::atan(pitch));
    msg.slope_deg = static_cast<float>(std::atan(std::hypot(dzdx, dzdy)) * 180.0 / M_PI);
    msg.valid = true;
    slope_pub_->publish(msg);
  }
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::SlopeNode>());
  rclcpp::shutdown();
  return 0;
}