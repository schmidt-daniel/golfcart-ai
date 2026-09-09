#include <memory>
#include <string>

#include "golfcart_msgs/msg/course_map.hpp"
#include "rclcpp/rclcpp.hpp"

#include "course_zip.hpp"

namespace golfcart
{

// Course loader node.
// Loads a course exported by the map editor (a Zip bundle) and publishes it as
// a CourseMap message so the HMI, web app, and future semantic layers can
// consume the authored course (origin, features, forbidden zones, costmaps).
class CourseLoaderNode : public rclcpp::Node
{
public:
  CourseLoaderNode()
  : Node("course_loader_node")
  {
    course_zip_ = declare_parameter<std::string>("course_zip", "");
    publish_period_s_ = declare_parameter<double>("publish_period_s", 5.0);

    map_pub_ = create_publisher<golfcart_msgs::msg::CourseMap>(
      "course/map",
      rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal));

    if (course_zip_.empty()) {
      RCLCPP_ERROR(get_logger(), "No 'course_zip' parameter; course_loader will not publish.");
      return;
    }

    ParsedCourse parsed;
    if (!parse_course_zip(course_zip_, parsed)) {
      RCLCPP_ERROR(get_logger(), "Failed to load course from '%s'.", course_zip_.c_str());
      return;
    }
    course_ = parsed.map;
    course_.timestamp = now();

    RCLCPP_INFO(get_logger(), "Loaded course '%s' (%zu holes, %zu forbidden zones).",
                course_.course_name.c_str(), course_.features.size(),
                course_.forbidden_zones.size());

    // Publish once immediately, then on a timer so late subscribers get it.
    publish_course();
    timer_ = create_wall_timer(
      std::chrono::duration<double>(publish_period_s_),
      [this]() { publish_course(); });
  }

private:
  std::string course_zip_;
  double publish_period_s_;
  rclcpp::Publisher<golfcart_msgs::msg::CourseMap>::SharedPtr map_pub_;
  rclcpp::TimerBase::SharedPtr timer_;
  golfcart_msgs::msg::CourseMap course_;

  void publish_course()
  {
    map_pub_->publish(course_);
  }
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::CourseLoaderNode>());
  rclcpp::shutdown();
  return 0;
}