// Shared course Zip parsing for the golf cart.
//
// Reads a course exported by the map editor (a Zip bundle) and produces a
// CourseMap message. Used by both course_loader_node and course_registry_node.
//
// The Zip bundle layout (from tools/map_editor/exporter.py):
//   course.yaml
//   holes/holeN.yaml
//   holes/costmapN.pgm        (optional)
//   holes/costmapN.yaml       (optional)
//   holes/geojson/holeN.geojson
//
// The Zip parser is minimal (no external zip lib): it parses the central
// directory and inflates raw-deflate members via zlib (as written by Python's
// zipfile).

#ifndef GOLFCART_NAVIGATION__COURSE_ZIP_HPP_
#define GOLFCART_NAVIGATION__COURSE_ZIP_HPP_

#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>
#include <zlib.h>

#include "golfcart_msgs/msg/course_feature.hpp"
#include "golfcart_msgs/msg/course_map.hpp"
#include "geometry_msgs/msg/polygon.hpp"
#include "nav_msgs/msg/occupancy_grid.hpp"

namespace golfcart
{

// Per-hole data extracted from a hole YAML (for the session node).
struct HoleData
{
  int number = 0;
  std::string name;
  // tee_id -> distance (m)
  std::vector<std::pair<std::string, double>> distances;
  // Boundary polygon in map-frame x/y.
  std::vector<geometry_msgs::msg::Point32> boundary;
  // Features (tee boxes, greens, hazards) in map-frame x/y.
  std::vector<golfcart_msgs::msg::CourseFeature> features;
};

// A parsed course: the CourseMap message plus per-hole data.
struct ParsedCourse
{
  golfcart_msgs::msg::CourseMap map;
  std::vector<HoleData> holes;
};

// Forward declarations (defined below).
void latlon_to_map(double lat, double lon, const golfcart_msgs::msg::CourseMap & map,
                   double & x, double & y);
void parse_hole_yaml(const std::string & text, int hole_num,
                     golfcart_msgs::msg::CourseMap & map, HoleData & hd);
bool is_forbidden_type(const std::string & type);

// Minimal Zip reader.
class ZipReader
{
public:
  struct Entry
  {
    std::string name;
    uint16_t method;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_offset;
  };

  // Load a zip file into memory and parse its central directory.
  bool open(const std::string & path)
  {
    data_.clear();
    entries_.clear();
    std::ifstream f(path, std::ios::binary);
    if (!f) {
      return false;
    }
    f.seekg(0, std::ios::end);
    std::streamsize sz = f.tellg();
    f.seekg(0, std::ios::beg);
    data_.resize(static_cast<size_t>(sz));
    if (sz > 0) {
      f.read(reinterpret_cast<char *>(data_.data()), sz);
    }

    // Locate End Of Central Directory (EOCD) signature 0x06054b50.
    const size_t eocd = find_signature(0x06054b50);
    if (eocd == SIZE_MAX) {
      return false;
    }
    const uint16_t n_entries = read_u16(eocd + 10);
    const uint32_t cd_offset = read_u32(eocd + 16);

    size_t pos = cd_offset;
    for (uint16_t i = 0; i < n_entries; ++i) {
      if (read_u32(pos) != 0x02014b50) {
        return false;
      }
      const uint16_t method = read_u16(pos + 10);
      const uint32_t comp_size = read_u32(pos + 20);
      const uint32_t uncomp_size = read_u32(pos + 24);
      const uint16_t name_len = read_u16(pos + 28);
      const uint16_t extra_len = read_u16(pos + 30);
      const uint16_t comment_len = read_u16(pos + 32);
      const uint32_t local_offset = read_u32(pos + 42);
      std::string name(data_.begin() + pos + 46, data_.begin() + pos + 46 + name_len);
      entries_.push_back({name, method, comp_size, uncomp_size, local_offset});
      pos += 46 + name_len + extra_len + comment_len;
    }
    return true;
  }

  const std::vector<Entry> & entries() const { return entries_; }

  // Read a member by exact name; returns "" if not found.
  std::string read(const std::string & name)
  {
    for (const Entry & e : entries_) {
      if (e.name != name) {
        continue;
      }
      const uint16_t lname_len = read_u16(e.local_offset + 26);
      const uint16_t lextra_len = read_u16(e.local_offset + 28);
      const size_t data_off = e.local_offset + 30 + lname_len + lextra_len;
      if (data_off + e.comp_size > data_.size()) {
        return "";
      }
      const std::vector<uint8_t> comp(data_.begin() + data_off,
                                      data_.begin() + data_off + e.comp_size);
      if (e.method == 0) {
        return std::string(comp.begin(), comp.end());
      }
      if (e.method == 8) {
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

private:
  std::vector<uint8_t> data_;
  std::vector<Entry> entries_;

  size_t find_signature(uint32_t sig)
  {
    if (data_.size() < 4) {
      return SIZE_MAX;
    }
    const size_t max_comment = 65535 + 22;
    const size_t start = (data_.size() > max_comment) ? (data_.size() - max_comment) : 0;
    for (size_t i = data_.size() - 4; i >= start && i != SIZE_MAX; --i) {
      if (read_u32(i) == sig) {
        return i;
      }
    }
    return SIZE_MAX;
  }

  uint16_t read_u16(size_t off)
  {
    if (off + 2 > data_.size()) {
      return 0;
    }
    return static_cast<uint16_t>(data_[off]) |
           (static_cast<uint16_t>(data_[off + 1]) << 8);
  }

  uint32_t read_u32(size_t off)
  {
    if (off + 4 > data_.size()) {
      return 0;
    }
    return static_cast<uint32_t>(data_[off]) |
           (static_cast<uint32_t>(data_[off + 1]) << 8) |
           (static_cast<uint32_t>(data_[off + 2]) << 16) |
           (static_cast<uint32_t>(data_[off + 3]) << 24);
  }
};

// Parse a course Zip into a ParsedCourse. Returns false on failure.
bool parse_course_zip(const std::string & zip_path, ParsedCourse & out)
{
  ZipReader zip;
  if (!zip.open(zip_path)) {
    return false;
  }

  const std::string course_yaml = zip.read("course.yaml");
  if (course_yaml.empty()) {
    return false;
  }

  YAML::Node root;
  try {
    root = YAML::Load(course_yaml);
  } catch (...) {
    return false;
  }

  golfcart_msgs::msg::CourseMap & map = out.map;
  const YAML::Node & course = root["course"];
  if (course) {
    map.course_name = course["name"] ? course["name"].as<std::string>() : "";
    map.course_id = course["id"] ? course["id"].as<std::string>() : "";
    const YAML::Node & origin = course["origin"];
    if (origin) {
      map.origin_latitude_deg = origin["latitude_deg"] ? origin["latitude_deg"].as<double>() : 0.0;
      map.origin_longitude_deg = origin["longitude_deg"] ? origin["longitude_deg"].as<double>() : 0.0;
      map.origin_rotation_rad = origin["rotation_rad"] ? origin["rotation_rad"].as<double>() : 0.0;
    }
    // Tee taxonomy: {tee_id: {name, slope?, cr?}}.
    const YAML::Node & tees = course["tees"];
    if (tees) {
      for (const auto & kv : tees) {
        map.tee_ids.push_back(kv.first.as<std::string>());
        map.tee_names.push_back(kv.second["name"] ? kv.second["name"].as<std::string>()
                                                  : kv.first.as<std::string>());
      }
    }
  } else {
    // Legacy flat format.
    map.course_name = root["course_name"] ? root["course_name"].as<std::string>() : "";
    map.course_id = root["course_id"] ? root["course_id"].as<std::string>() : "";
    map.origin_latitude_deg = root["origin_latitude_deg"] ? root["origin_latitude_deg"].as<double>() : 0.0;
    map.origin_longitude_deg = root["origin_longitude_deg"] ? root["origin_longitude_deg"].as<double>() : 0.0;
    map.origin_rotation_rad = root["origin_rotation_rad"] ? root["origin_rotation_rad"].as<double>() : 0.0;
  }

  // Load each holeN.yaml.
  for (const ZipReader::Entry & e : zip.entries()) {
    if (e.name.rfind("holes/hole", 0) != 0) {
      continue;
    }
    if (e.name.size() < 12 || e.name.substr(e.name.size() - 5) != ".yaml") {
      continue;
    }
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
    const std::string hole_yaml = zip.read(e.name);
    if (hole_yaml.empty()) {
      continue;
    }
    HoleData hd;
    parse_hole_yaml(hole_yaml, hole_num, map, hd);
    out.holes.push_back(hd);
  }
  return true;
}

// Convert lat/lon to map-frame x/y using the course origin.
void latlon_to_map(double lat, double lon, const golfcart_msgs::msg::CourseMap & map,
                   double & x, double & y)
{
  constexpr double R = 6371000.0;
  const double dlat = (lat - map.origin_latitude_deg) * M_PI / 180.0;
  const double dlon = (lon - map.origin_longitude_deg) * M_PI / 180.0;
  const double cos_lat = std::cos(map.origin_latitude_deg * M_PI / 180.0);
  const double east = R * dlon * cos_lat;
  const double north = R * dlat;
  const double c = std::cos(map.origin_rotation_rad);
  const double s = std::sin(map.origin_rotation_rad);
  x = east * c - north * s;
  y = east * s + north * c;
}

// Parse a hole YAML into per-hole data (distances, boundary, features) and
// add its forbidden zones/features to the CourseMap.
void parse_hole_yaml(const std::string & text, int hole_num,
                     golfcart_msgs::msg::CourseMap & map, HoleData & hd)
{
  hd.number = hole_num;
  YAML::Node root;
  try {
    root = YAML::Load(text);
  } catch (...) {
    return;
  }

  hd.name = root["name"] ? root["name"].as<std::string>() : "";

  // Distances: {tee_id: meters}.
  const YAML::Node & dists = root["distances"];
  if (dists) {
    for (const auto & kv : dists) {
      hd.distances.push_back({kv.first.as<std::string>(), kv.second.as<double>()});
    }
  }

  // Boundary: [{lat, lon}, ...] -> map-frame x/y.
  const YAML::Node & boundary = root["boundary"];
  if (boundary && boundary.IsSequence()) {
    for (const YAML::Node & pt : boundary) {
      const double lat = pt["lat"].as<double>();
      const double lon = pt["lon"].as<double>();
      double mx, my;
      latlon_to_map(lat, lon, map, mx, my);
      geometry_msgs::msg::Point32 p;
      p.x = static_cast<float>(mx);
      p.y = static_cast<float>(my);
      hd.boundary.push_back(p);
    }
  }

  // Features.
  const YAML::Node & feats = root["features"] ? root["features"] : root["shapes"];
  if (feats && feats.IsSequence()) {
    for (const YAML::Node & s : feats) {
      std::string type;
      std::string label;
      std::string tee_id;
      YAML::Node geom;
      if (s["type"] && s["geometry"]) {
        type = s["type"].as<std::string>();
        label = s["label"] ? s["label"].as<std::string>() : "";
        tee_id = s["tee_id"] ? s["tee_id"].as<std::string>() : "";
        geom = s["geometry"];
      } else if (s["properties"] && s["geometry"]) {
        type = s["properties"]["type"] ? s["properties"]["type"].as<std::string>() : "";
        label = s["properties"]["label"] ? s["properties"]["label"].as<std::string>() : "";
        tee_id = s["properties"]["tee_id"] ? s["properties"]["tee_id"].as<std::string>() : "";
        geom = s["geometry"];
      } else {
        continue;
      }

      golfcart_msgs::msg::CourseFeature feat;
      feat.type = type;
      feat.label = label;
      feat.tee_id = tee_id;
      feat.hole_number = static_cast<uint32_t>(hole_num);

      const std::string gtype = geom["type"] ? geom["type"].as<std::string>() : "";
      geometry_msgs::msg::Polygon poly;
      if (gtype == "Polygon") {
        const YAML::Node & ring = geom["coordinates"][0];
        if (ring.IsSequence()) {
          for (const YAML::Node & pt : ring) {
            if (pt.IsSequence() && pt.size() >= 2) {
              const double lon = pt[0].as<double>();
              const double lat = pt[1].as<double>();
              double mx, my;
              latlon_to_map(lat, lon, map, mx, my);
              geometry_msgs::msg::Point32 p;
              p.x = static_cast<float>(mx);
              p.y = static_cast<float>(my);
              poly.points.push_back(p);
            }
          }
        }
      } else if (gtype == "Point") {
        const YAML::Node & pt = geom["coordinates"];
        if (pt.IsSequence() && pt.size() >= 2) {
          double mx, my;
          latlon_to_map(pt[1].as<double>(), pt[0].as<double>(), map, mx, my);
          geometry_msgs::msg::Point32 p;
          p.x = static_cast<float>(mx);
          p.y = static_cast<float>(my);
          poly.points.push_back(p);
        }
      }

      if (poly.points.size() >= 3) {
        map.forbidden_zones.push_back(poly);
      }
      if (poly.points.size() >= 1) {
        feat.x = poly.points[0].x;
        feat.y = poly.points[0].y;
      }
      map.features.push_back(feat);
      hd.features.push_back(feat);
    }
  }
}

// Parse a PGM (P5 binary) + YAML costmap pair into an OccupancyGrid.
// Returns false on failure. The YAML provides resolution + origin.
bool parse_costmap(const std::string & pgm_text, const std::string & yaml_text,
                   nav_msgs::msg::OccupancyGrid & grid)
{
  // Parse the PGM header: "P5\n<width> <height>\n<maxval>\n<data>".
  size_t pos = 0;
  auto skip_ws = [&]() {
    while (pos < pgm_text.size() && (pgm_text[pos] == ' ' || pgm_text[pos] == '\n' ||
                                     pgm_text[pos] == '\r' || pgm_text[pos] == '\t')) {
      ++pos;
    }
  };
  auto read_token = [&]() -> std::string {
    skip_ws();
    std::string tok;
    while (pos < pgm_text.size() && pgm_text[pos] != ' ' && pgm_text[pos] != '\n' &&
           pgm_text[pos] != '\r' && pgm_text[pos] != '\t') {
      tok += pgm_text[pos++];
    }
    return tok;
  };

  if (read_token() != "P5") {
    return false;
  }
  const int width = std::stoi(read_token());
  const int height = std::stoi(read_token());
  const int maxval = std::stoi(read_token());
  if (width <= 0 || height <= 0 || maxval != 255) {
    return false;
  }
  // Skip a single whitespace char before the binary data.
  skip_ws();

  // Parse the YAML for resolution + origin.
  double resolution = 0.05;
  double origin_x = 0.0, origin_y = 0.0;
  try {
    YAML::Node meta = YAML::Load(yaml_text);
    if (meta["resolution"]) {
      resolution = meta["resolution"].as<double>();
    }
    const YAML::Node & origin = meta["origin"];
    if (origin && origin.IsSequence() && origin.size() >= 2) {
      origin_x = origin[0].as<double>();
      origin_y = origin[1].as<double>();
    }
  } catch (...) {
    // Use defaults.
  }

  grid.info.resolution = resolution;
  grid.info.width = static_cast<uint32_t>(width);
  grid.info.height = static_cast<uint32_t>(height);
  grid.info.origin.position.x = origin_x;
  grid.info.origin.position.y = origin_y;
  grid.info.origin.orientation.w = 1.0;
  grid.header.frame_id = "map";

  // Read the binary data (width*height bytes).
  const size_t n = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (pos + n > pgm_text.size()) {
    return false;
  }
  grid.data.resize(n);
  for (size_t i = 0; i < n; ++i) {
    // PGM 0..255 -> occupancy -1..1 (0 = free, 254 -> 1, 255 unknown -> 0).
    const uint8_t v = static_cast<uint8_t>(pgm_text[pos + i]);
    if (v == 255) {
      grid.data[i] = 0.0;  // unknown
    } else if (v >= 254) {
      grid.data[i] = 1.0;  // lethal
    } else {
      grid.data[i] = static_cast<double>(v) / 254.0;
    }
  }
  return true;
}

bool is_forbidden_type(const std::string & type)
{
  return type == "GREEN" || type == "TEE_BOX" || type == "WATER_HAZARD" ||
         type == "ROUGH" || type == "BUNKER" || type == "FORBIDDEN_ZONE";
}

}  // namespace golfcart

#endif  // GOLFCART_NAVIGATION__COURSE_ZIP_HPP_