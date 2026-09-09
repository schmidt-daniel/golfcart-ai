#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>
#include <zlib.h>

#include "golfcart_msgs/msg/course_feature.hpp"
#include "golfcart_msgs/msg/course_map.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"
#include "rclcpp/rclcpp.hpp"

namespace golfcart
{

// Course loader node.
// Loads a course exported by the map editor (a Zip bundle) and publishes it as
// a CourseMap message so the HMI, web app, and future semantic layers can
// consume the authored course (origin, features, forbidden zones, costmaps).
//
// The Zip bundle layout (from tools/map_editor/exporter.py):
//   course.yaml
//   holes/holeN.yaml
//   holes/costmapN.pgm        (optional)
//   holes/costmapN.yaml       (optional)
//   holes/geojson/holeN.geojson
//
// This node reads course.yaml + holes/holeN.yaml + optional costmaps and
// publishes a single CourseMap on /course/map (transient-local, so late
// subscribers get the latest map).
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

    if (!load_course(course_zip_)) {
      RCLCPP_ERROR(get_logger(), "Failed to load course from '%s'.", course_zip_.c_str());
      return;
    }

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

  // ------------------------------------------------------------------
  // Zip reading (minimal, no external zip lib): we only need to read
  // specific members. Use the `unzip -p` approach via popen is fragile;
  // instead we parse the Zip central directory ourselves (stored/deflate
  // via zlib). For simplicity and robustness we require the bundle to be
  // produced by Python's zipfile (deflate), which we can inflate with zlib.
  // ------------------------------------------------------------------

  bool load_course(const std::string & zip_path)
  {
    // Read the whole zip into memory.
    std::vector<uint8_t> data;
    {
      std::ifstream f(zip_path, std::ios::binary);
      if (!f) {
        RCLCPP_ERROR(get_logger(), "Cannot open '%s'", zip_path.c_str());
        return false;
      }
      f.seekg(0, std::ios::end);
      std::streamsize sz = f.tellg();
      f.seekg(0, std::ios::beg);
      data.resize(static_cast<size_t>(sz));
      if (sz > 0) {
        f.read(reinterpret_cast<char *>(data.data()), sz);
      }
    }

    // Locate End Of Central Directory (EOCD) signature 0x06054b50.
    const size_t eocd = find_signature(data, 0x06054b50);
    if (eocd == SIZE_MAX) {
      RCLCPP_ERROR(get_logger(), "Not a valid zip (no EOCD).");
      return false;
    }
    // EOCD: entries(2) at +10, cd_offset(4) at +16.
    const uint16_t n_entries = read_u16(data, eocd + 10);
    const uint32_t cd_offset = read_u32(data, eocd + 16);

    // Parse central directory entries.
    std::vector<ZipEntry> entries;
    size_t pos = cd_offset;
    for (uint16_t i = 0; i < n_entries; ++i) {
      if (read_u32(data, pos) != 0x02014b50) {
        RCLCPP_ERROR(get_logger(), "Bad central directory signature.");
        return false;
      }
      const uint16_t method = read_u16(data, pos + 10);
      const uint32_t comp_size = read_u32(data, pos + 20);
      const uint32_t uncomp_size = read_u32(data, pos + 24);
      const uint16_t name_len = read_u16(data, pos + 28);
      const uint16_t extra_len = read_u16(data, pos + 30);
      const uint16_t comment_len = read_u16(data, pos + 32);
      const uint32_t local_offset = read_u32(data, pos + 42);
      std::string name(data.begin() + pos + 46, data.begin() + pos + 46 + name_len);
      entries.push_back({name, method, comp_size, uncomp_size, local_offset});
      pos += 46 + name_len + extra_len + comment_len;
    }

    // Read the members we care about.
    std::string course_yaml = read_member(data, entries, "course.yaml");
    if (course_yaml.empty()) {
      RCLCPP_ERROR(get_logger(), "Zip has no course.yaml.");
      return false;
    }

    if (!parse_course_yaml(course_yaml)) {
      return false;
    }

    // Load each holeN.yaml + optional costmapN.pgm/.yaml.
    for (const ZipEntry & e : entries) {
      if (e.name.rfind("holes/hole", 0) != 0) {
        continue;
      }
      if (e.name.size() < 12 || e.name.substr(e.name.size() - 5) != ".yaml") {
        continue;
      }
      // holes/holeN.yaml -> N  ("holes/hole" is 10 chars)
      const std::string num = e.name.substr(10, e.name.size() - 10 - 5);
      int hole_num = 0;
      try {
        hole_num = std::stoi(num);
      } catch (...) {
        continue;
      }
      if (hole_num <= 0) {
        continue;
      }
      const std::string hole_yaml = read_member(data, entries, e.name);
      if (hole_yaml.empty()) {
        continue;
      }
      parse_hole_yaml(hole_yaml, hole_num);
    }

    RCLCPP_INFO(get_logger(), "Loaded course '%s' (%zu holes, %zu forbidden zones).",
                course_.course_name.c_str(), course_.features.size(),
                course_.forbidden_zones.size());
    return true;
  }

  bool parse_course_yaml(const std::string & text)
  {
    YAML::Node root;
    try {
      root = YAML::Load(text);
    } catch (const std::exception & e) {
      RCLCPP_ERROR(get_logger(), "course.yaml parse error: %s", e.what());
      return false;
    }
    // New format: {schema_version, course: {name, id, origin: {...}}, holes: [...]}
    const YAML::Node & course = root["course"];
    if (course) {
      course_.course_name = course["name"] ? course["name"].as<std::string>() : "";
      course_.course_id = course["id"] ? course["id"].as<std::string>() : "";
      const YAML::Node & origin = course["origin"];
      if (origin) {
        course_.origin_latitude_deg = origin["latitude_deg"] ? origin["latitude_deg"].as<double>() : 0.0;
        course_.origin_longitude_deg = origin["longitude_deg"] ? origin["longitude_deg"].as<double>() : 0.0;
        course_.origin_rotation_rad = origin["rotation_rad"] ? origin["rotation_rad"].as<double>() : 0.0;
      }
    } else {
      // Legacy flat format.
      course_.course_name = root["course_name"] ? root["course_name"].as<std::string>() : "";
      course_.course_id = root["course_id"] ? root["course_id"].as<std::string>() : "";
      course_.origin_latitude_deg = root["origin_latitude_deg"] ? root["origin_latitude_deg"].as<double>() : 0.0;
      course_.origin_longitude_deg = root["origin_longitude_deg"] ? root["origin_longitude_deg"].as<double>() : 0.0;
      course_.origin_rotation_rad = root["origin_rotation_rad"] ? root["origin_rotation_rad"].as<double>() : 0.0;
    }
    course_.timestamp = now();
    return true;
  }

  void parse_hole_yaml(const std::string & text, int hole_num)
  {
    YAML::Node root;
    try {
      root = YAML::Load(text);
    } catch (...) {
      return;
    }

    // Forbidden zones: greens, tees, water, rough, bunkers, forbidden zones.
    // New format uses `features` (schema-native: type on the object); legacy
    // used `shapes` (GeoJSON Feature wrappers with type in properties).
    const YAML::Node & feats = root["features"] ? root["features"] : root["shapes"];
    if (feats && feats.IsSequence()) {
      for (const YAML::Node & s : feats) {
        // Resolve type/label: schema-native (s["type"]) or legacy (s["properties"]["type"]).
        std::string type;
        std::string label;
        bool forbidden = false;
        YAML::Node geom;
        if (s["type"] && s["geometry"]) {
          // Schema-native.
          type = s["type"].as<std::string>();
          label = s["label"] ? s["label"].as<std::string>() : "";
          geom = s["geometry"];
          forbidden = is_forbidden_type(type);
        } else if (s["properties"] && s["geometry"]) {
          // Legacy GeoJSON Feature.
          type = s["properties"]["type"] ? s["properties"]["type"].as<std::string>() : "";
          label = s["properties"]["label"] ? s["properties"]["label"].as<std::string>() : "";
          forbidden = s["properties"]["forbidden"] ? s["properties"]["forbidden"].as<bool>() : false;
          geom = s["geometry"];
        } else {
          continue;
        }
        if (!forbidden) {
          continue;
        }
        // geometry.coordinates: Polygon -> [[[lon,lat],...]], Point -> [lon,lat].
        const std::string gtype = geom["type"] ? geom["type"].as<std::string>() : "";
        geometry_msgs::msg::Polygon poly;
        if (gtype == "Polygon") {
          const YAML::Node & ring = geom["coordinates"][0];
          if (ring.IsSequence()) {
            for (const YAML::Node & pt : ring) {
              if (pt.IsSequence() && pt.size() >= 2) {
                const double lon = pt[0].as<double>();
                const double lat = pt[1].as<double>();
                geometry_msgs::msg::Point32 p;
                p.x = lat;
                p.y = lon;
                poly.points.push_back(p);
              }
            }
          }
        } else if (gtype == "Point") {
          const YAML::Node & pt = geom["coordinates"];
          if (pt.IsSequence() && pt.size() >= 2) {
            geometry_msgs::msg::Point32 p;
            p.x = pt[1].as<double>();
            p.y = pt[0].as<double>();
            poly.points.push_back(p);
          }
        }
        if (poly.points.size() >= 3) {
          course_.forbidden_zones.push_back(poly);
        }
        // Also add a CourseFeature for the shape.
        golfcart_msgs::msg::CourseFeature feat;
        feat.type = type;
        feat.label = label;
        feat.hole_number = static_cast<uint32_t>(hole_num);
        if (poly.points.size() >= 1) {
          feat.x = poly.points[0].x;
          feat.y = poly.points[0].y;
        }
        feat.timestamp = now();
        course_.features.push_back(feat);
      }
    }
  }

  static bool is_forbidden_type(const std::string & type)
  {
    return type == "GREEN" || type == "TEE_BOX" || type == "WATER_HAZARD" ||
           type == "ROUGH" || type == "BUNKER" || type == "FORBIDDEN_ZONE";
  }

  void publish_course()
  {
    map_pub_->publish(course_);
  }

  // ------------------------------------------------------------------
  // Zip helpers
  // ------------------------------------------------------------------

  struct ZipEntry
  {
    std::string name;
    uint16_t method;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_offset;
  };

  static size_t find_signature(const std::vector<uint8_t> & d, uint32_t sig)
  {
    // The EOCD record is always at the very end of the file (its comment can
    // be up to 65535 bytes). Search backwards from the end to avoid false
    // positives inside compressed data.
    if (d.size() < 4) {
      return SIZE_MAX;
    }
    const size_t max_comment = 65535 + 22;  // EOCD record is 22 bytes + comment.
    const size_t start = (d.size() > max_comment) ? (d.size() - max_comment) : 0;
    for (size_t i = d.size() - 4; i >= start && i != SIZE_MAX; --i) {
      if (read_u32(d, i) == sig) {
        return i;
      }
    }
    return SIZE_MAX;
  }

  static uint16_t read_u16(const std::vector<uint8_t> & d, size_t off)
  {
    if (off + 2 > d.size()) {
      return 0;
    }
    return static_cast<uint16_t>(d[off]) |
           (static_cast<uint16_t>(d[off + 1]) << 8);
  }

  static uint32_t read_u32(const std::vector<uint8_t> & d, size_t off)
  {
    if (off + 4 > d.size()) {
      return 0;
    }
    return static_cast<uint32_t>(d[off]) |
           (static_cast<uint32_t>(d[off + 1]) << 8) |
           (static_cast<uint32_t>(d[off + 2]) << 16) |
           (static_cast<uint32_t>(d[off + 3]) << 24);
  }

  std::string read_member(const std::vector<uint8_t> & data,
                          const std::vector<ZipEntry> & entries,
                          const std::string & name)
  {
    for (const ZipEntry & e : entries) {
      if (e.name != name) {
        continue;
      }
      // Local file header at local_offset: sig(4) + ... + name_len(2)@+26,
      // extra_len(2)@+28, data starts at local_offset + 30 + name_len + extra_len.
      const uint16_t lname_len = read_u16(data, e.local_offset + 26);
      const uint16_t lextra_len = read_u16(data, e.local_offset + 28);
      const size_t data_off = e.local_offset + 30 + lname_len + lextra_len;
      if (data_off + e.comp_size > data.size()) {
        return "";
      }
      const std::vector<uint8_t> comp(data.begin() + data_off,
                                      data.begin() + data_off + e.comp_size);
      if (e.method == 0) {
        // Stored.
        return std::string(comp.begin(), comp.end());
      }
      if (e.method == 8) {
        // Raw deflate (no zlib header) as written by Python's zipfile.
        std::vector<uint8_t> out(e.uncomp_size);
        z_stream strm;
        std::memset(&strm, 0, sizeof(strm));
        if (inflateInit2(&strm, -15) != Z_OK) {
          return "";
        }
        strm.next_in = const_cast<Bytef *>(comp.data());
        strm.avail_in = static_cast<uInt>(comp.size());
        strm.next_out = out.data();
        strm.avail_out = static_cast<uInt>(out.size());
        const int ret = inflate(&strm, Z_FINISH);
        inflateEnd(&strm);
        if (ret != Z_STREAM_END) {
          return "";
        }
        return std::string(out.begin(), out.begin() + strm.total_out);
      }
      return "";
    }
    return "";
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