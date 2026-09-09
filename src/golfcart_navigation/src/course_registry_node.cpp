#include <algorithm>
#include <memory>
#include <string>

#include "golfcart_msgs/msg/course_info.hpp"
#include "golfcart_msgs/msg/course_list.hpp"
#include "golfcart_msgs/msg/course_map.hpp"
#include "golfcart_msgs/msg/course_selected.hpp"
#include "golfcart_msgs/srv/course_select.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "rclcpp/rclcpp.hpp"

#include "course_zip.hpp"

namespace golfcart
{

// Course registry node.
// Scans a directory of course Zips (exported by the map editor) and:
//   - publishes a CourseList on /course/list (transient-local)
//   - serves /course/select to load a chosen course and publish its CourseMap
//     on /course/map (transient-local)
class CourseRegistryNode : public rclcpp::Node
{
public:
  CourseRegistryNode()
  : Node("course_registry_node")
  {
    courses_dir_ = declare_parameter<std::string>("courses_dir", "courses");

    list_pub_ = create_publisher<golfcart_msgs::msg::CourseList>(
      "course/list",
      rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal));
    map_pub_ = create_publisher<golfcart_msgs::msg::CourseMap>(
      "course/map",
      rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal));
    selected_pub_ = create_publisher<golfcart_msgs::msg::CourseSelected>(
      "course/selected",
      rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal));
    costmap_pub_ = create_publisher<nav_msgs::msg::OccupancyGrid>(
      "map",
      rclcpp::QoS(rclcpp::KeepLast(1)).durability(rclcpp::DurabilityPolicy::TransientLocal));

    select_srv_ = create_service<golfcart_msgs::srv::CourseSelect>(
      "course/select",
      [this](const std::shared_ptr<golfcart_msgs::srv::CourseSelect::Request> req,
             std::shared_ptr<golfcart_msgs::srv::CourseSelect::Response> resp) {
        handle_select(req, resp);
      });

    scan_courses();
    publish_list();
  }

private:
  std::string courses_dir_;
  rclcpp::Publisher<golfcart_msgs::msg::CourseList>::SharedPtr list_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::CourseMap>::SharedPtr map_pub_;
  rclcpp::Publisher<golfcart_msgs::msg::CourseSelected>::SharedPtr selected_pub_;
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr costmap_pub_;
  rclcpp::Service<golfcart_msgs::srv::CourseSelect>::SharedPtr select_srv_;

  // course_id -> zip path
  std::vector<std::pair<std::string, std::string>> courses_;

  void scan_courses()
  {
    courses_.clear();
    std::vector<std::string> zips;
    // List *.zip files in the courses dir.
    std::string cmd = "ls " + courses_dir_ + "/*.zip 2>/dev/null";
    FILE * pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
      return;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), pipe)) {
      std::string line(buf);
      line.erase(line.find_last_not_of(" \t\r\n") + 1);
      if (!line.empty()) {
        zips.push_back(line);
      }
    }
    pclose(pipe);
    std::sort(zips.begin(), zips.end());

    for (const std::string & zp : zips) {
      ParsedCourse parsed;
      if (!parse_course_zip(zp, parsed)) {
        RCLCPP_WARN(get_logger(), "Skipping unreadable course zip '%s'", zp.c_str());
        continue;
      }
      const std::string id = parsed.map.course_id.empty()
                               ? parsed.map.course_name : parsed.map.course_id;
      courses_.push_back({id, zp});
      RCLCPP_INFO(get_logger(), "Found course '%s' (%s)", parsed.map.course_name.c_str(), zp.c_str());
    }
  }

  void publish_list()
  {
    golfcart_msgs::msg::CourseList msg;
    for (const auto & c : courses_) {
      golfcart_msgs::msg::CourseInfo info;
      info.id = c.first;
      // Re-parse to get the name + hole count.
      ParsedCourse parsed;
      if (parse_course_zip(c.second, parsed)) {
        info.name = parsed.map.course_name;
        info.hole_count = static_cast<uint32_t>(parsed.holes.size());
      } else {
        info.name = c.first;
        info.hole_count = 0;
      }
      msg.courses.push_back(info);
    }
    msg.timestamp = now();
    list_pub_->publish(msg);
    RCLCPP_INFO(get_logger(), "Published %zu courses on /course/list", msg.courses.size());
  }

  // Publish the costmap for the first hole that has one on /map.
  void publish_costmap(const std::string & zip_path)
  {
    ZipReader zip;
    if (!zip.open(zip_path)) {
      return;
    }
    for (const ZipReader::Entry & e : zip.entries()) {
      if (e.name.rfind("holes/costmap", 0) != 0 || e.name.size() < 5 ||
          e.name.substr(e.name.size() - 4) != ".pgm") {
        continue;
      }
      const std::string pgm = zip.read(e.name);
      const std::string yaml_name = e.name.substr(0, e.name.size() - 4) + ".yaml";
      const std::string yaml = zip.read(yaml_name);
      if (pgm.empty()) {
        continue;
      }
      nav_msgs::msg::OccupancyGrid grid;
      if (parse_costmap(pgm, yaml, grid)) {
        grid.header.stamp = now();
        costmap_pub_->publish(grid);
        RCLCPP_INFO(get_logger(), "Published costmap '%s' on /map", e.name.c_str());
        return;
      }
    }
  }

  void handle_select(const std::shared_ptr<golfcart_msgs::srv::CourseSelect::Request> req,
                     std::shared_ptr<golfcart_msgs::srv::CourseSelect::Response> resp)
  {
    for (const auto & c : courses_) {
      if (c.first == req->course_id) {
        ParsedCourse parsed;
        if (!parse_course_zip(c.second, parsed)) {
          resp->success = false;
          resp->message = "Failed to parse course zip";
          return;
        }
        parsed.map.timestamp = now();
        map_pub_->publish(parsed.map);
        // Tell the session node which zip to parse for full hole data.
        golfcart_msgs::msg::CourseSelected sel;
        sel.course_id = req->course_id;
        sel.zip_path = c.second;
        sel.timestamp = now();
        selected_pub_->publish(sel);
        // Publish the costmap for the first hole on /map (Nav2 static_layer).
        publish_costmap(c.second);
        resp->success = true;
        resp->message = "Loaded course '" + parsed.map.course_name + "'";
        RCLCPP_INFO(get_logger(), "Selected course '%s'", parsed.map.course_name.c_str());
        return;
      }
    }
    resp->success = false;
    resp->message = "Unknown course id: " + req->course_id;
  }
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::CourseRegistryNode>());
  rclcpp::shutdown();
  return 0;
}