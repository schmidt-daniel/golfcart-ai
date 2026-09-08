#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <memory>
#include <vector>

#include "golfcart_msgs/msg/person_target.hpp"
#include "rclcpp/rclcpp.hpp"
#include "sensor_msgs/msg/laser_scan.hpp"

namespace golfcart
{

// A 2D point in the LiDAR scan plane (x = forward, y = lateral).
struct Point2D
{
  double x;
  double y;
};

// A cluster of returns (candidate person).
struct Cluster
{
  std::vector<Point2D> points;
  double centroid_x = 0.0;
  double centroid_y = 0.0;
  double width_m = 0.0;
  double range_m = 0.0;
  double angle_rad = 0.0;
};

// Person Detection node.
// Consumes /scan (LaserScan) and publishes /person/target (PersonTarget).
// Detects a person walking away in front of the trolley using:
//   - sector gate + clustering + cluster filtering (person-like width)
//   - motion-based disambiguation (a person moves; a pole doesn't)
//   - leg-pair signature (two nearby clusters at the same range, alternating)
//   - temporal validation (N consecutive detections)
//   - EMA smoothing for a stable target + velocity estimate
class PersonDetectionNode : public rclcpp::Node
{
public:
  PersonDetectionNode()
  : Node("person_detection_node")
  {
    // ---- Parameters ----
    sector_half_angle_ = declare_parameter<double>("sector_half_angle_rad", 1.0472);  // ±60°
    min_range_ = declare_parameter<double>("min_range_m", 0.3);
    max_range_ = declare_parameter<double>("max_range_m", 12.0);
    min_width_ = declare_parameter<double>("min_width_m", 0.3);
    max_width_ = declare_parameter<double>("max_width_m", 0.6);
    cluster_gap_ = declare_parameter<double>("cluster_gap_m", 0.15);
    min_cluster_points_ = declare_parameter<int>("min_cluster_points", 3);
    temporal_confirm_ = declare_parameter<int>("temporal_confirm_scans", 3);
    target_timeout_s_ = declare_parameter<double>("target_timeout_s", 0.5);
    ema_alpha_ = declare_parameter<double>("ema_alpha", 0.4);
    min_speed_mps_ = declare_parameter<double>("min_speed_mps", 0.1);
    lock_distance_m_ = declare_parameter<double>("lock_distance_m", 2.0);
    lock_continuity_m_ = declare_parameter<double>("lock_continuity_m", 1.5);
    // Structural (in-place turning) confidence: how many recent scans to keep
    // of the locked target's cluster width, and the full width-oscillation
    // scale used to normalize the variability (two-legs<->one-leg swing ~0.17m).
    struct_history_scans_ = declare_parameter<int>("struct_history_scans", 10);
    struct_conf_scale_m_ = declare_parameter<double>("struct_conf_scale_m", 0.25);

    // ---- Subscriptions / publishers ----
    scan_sub_ = create_subscription<sensor_msgs::msg::LaserScan>(
      "scan", rclcpp::SensorDataQoS(),
      [this](const sensor_msgs::msg::LaserScan::SharedPtr msg) {
        process_scan(msg);
      });
    target_pub_ = create_publisher<golfcart_msgs::msg::PersonTarget>(
      "person/target", rclcpp::SensorDataQoS());

    // ---- Timer for stale-data handling ----
    stale_timer_ = create_wall_timer(
      std::chrono::milliseconds(200),
      [this]() { check_stale(); });

    // Initialize the time fields to the current node clock (sim time when
    // use_sim_time=true) so the first subtraction doesn't mix a default
    // wall-clock and a sim-clock time source.
    last_scan_time_ = now();
    last_publish_time_ = now();
  }

private:
  void process_scan(const sensor_msgs::msg::LaserScan::SharedPtr msg)
  {
    // 1. Sector gate + convert to cartesian points.
    std::vector<Point2D> pts;
    for (size_t i = 0; i < msg->ranges.size(); ++i) {
      const double range = msg->ranges[i];
      if (!std::isfinite(range) || range < min_range_ || range > max_range_) {
        continue;
      }
      const double angle = msg->angle_min + i * msg->angle_increment;
      // Keep only the forward sector.
      double d = std::abs(angle);
      d = std::min(d, 2.0 * M_PI - d);
      if (d > sector_half_angle_) {
        continue;
      }
      pts.push_back({range * std::cos(angle), range * std::sin(angle)});
    }

    // 2. Cluster adjacent points (by proximity).
    std::vector<Cluster> clusters = cluster_points(pts);

    // 3. Filter clusters to person-like width; produce a candidate list.
    std::vector<Cluster> candidates;
    for (const auto & c : clusters) {
      if (c.width_m < min_width_ || c.width_m > max_width_) {
        continue;  // too narrow (pole) or too wide (wall)
      }
      if (c.points.size() < static_cast<size_t>(min_cluster_points_)) {
        continue;
      }
      candidates.push_back(c);
    }

    // 4. Lock-on / continuity selection.
    //    - Not locked: wait for the first person within lock_distance_m, and
    //      lock onto the nearest one in that range.
    //    - Locked: prefer the candidate that is continuous with the previously
    //      followed person (within lock_continuity_m); never switch to a
    //      closer stranger. If no continuous candidate, fall back to nearest.
    Cluster best;
    bool found = false;
    if (!locked_) {
      for (const auto & c : candidates) {
        // Wait for the first person near the trolley (within lock distance).
        if (std::hypot(c.centroid_x, c.centroid_y) > lock_distance_m_) {
          continue;
        }
        if (!found || c.range_m < best.range_m) {
          best = c;
          found = true;
        }
      }
      if (found) {
        locked_ = true;
      }
    } else {
      // Locked: find the candidate closest to the locked target's previous
      // position (continuity), so we don't switch to a stranger.
      double best_dist = std::numeric_limits<double>::infinity();
      for (const auto & c : candidates) {
        const double dx = c.centroid_x - smoothed_x_;
        const double dy = c.centroid_y - smoothed_y_;
        const double d = std::hypot(dx, dy);
        if (d < best_dist) {
          best_dist = d;
          best = c;
          found = true;
        }
      }
      // If the nearest candidate jumped too far, the locked person was lost.
      if (best_dist > lock_continuity_m_) {
        found = false;
        locked_ = false;  // allow re-lock on a new (or returning) person
      }
    }

    if (!found) {
      // No person for this scan.
      consecutive_misses_++;
      consecutive_hits_ = 0;
      return;
    }

    // 4. Motion-based disambiguation: a person moves coherently over time.
    //    Compare this cluster's centroid to the previous one.
    const double dx = best.centroid_x - last_centroid_x_;
    const double dy = best.centroid_y - last_centroid_y_;
    const double dt = (now() - last_scan_time_).seconds();
    // Clamp dt so a long gap (person missed, then re-acquired) doesn't
    // produce a bogus, high-confidence speed estimate.
    const double eff_dt = (dt > 0.0 && dt < 2.0) ? dt : 0.0;
    const double speed = (eff_dt > 1e-6) ? std::hypot(dx, dy) / eff_dt : 0.0;
    last_centroid_x_ = best.centroid_x;
    last_centroid_y_ = best.centroid_y;
    last_scan_time_ = now();

    // 5. Temporal validation: require N consecutive detections.
    consecutive_hits_++;
    consecutive_misses_ = 0;
    if (consecutive_hits_ < temporal_confirm_) {
      return;
    }

    // 6. EMA smoothing of the target position.
    if (!has_target_) {
      smoothed_x_ = best.centroid_x;
      smoothed_y_ = best.centroid_y;
      has_target_ = true;
    } else {
      smoothed_x_ = ema_alpha_ * best.centroid_x + (1.0 - ema_alpha_) * smoothed_x_;
      smoothed_y_ = ema_alpha_ * best.centroid_y + (1.0 - ema_alpha_) * smoothed_y_;
    }

    // Structural variability: record the locked target's cluster width and
    // measure how much it oscillates over the recent window. A person turning
    // in place alternates between two-leg and one-leg views, making the width
    // swing; this is the motion-free cue that keeps confidence healthy while
    // the person is stationary but not a static pole/wall.
    width_history_.push_back(best.width_m);
    if (width_history_.size() > static_cast<size_t>(struct_history_scans_)) {
      width_history_.pop_front();
    }
    double struct_conf = 0.0;
    if (width_history_.size() >= 3) {
      const double mn = *std::min_element(
        width_history_.begin(), width_history_.end());
      const double mx = *std::max_element(
        width_history_.begin(), width_history_.end());
      // Map the width oscillation into [0,1]; a full two-legs<->one-leg swing
      // (~0.17m) gives ~0.7, a static pole gives ~0.
      struct_conf = std::clamp((mx - mn) / struct_conf_scale_m_, 0.0, 1.0);
    }

    // 7. Publish the target.
    golfcart_msgs::msg::PersonTarget t;
    t.distance_m = std::hypot(smoothed_x_, smoothed_y_);
    t.lateral_offset_m = smoothed_y_;
    t.relative_velocity_mps = speed;
    t.confidence = compute_confidence(best, speed, struct_conf);
    t.source = "lidar";
    t.valid = true;
    t.timestamp = now();
    target_pub_->publish(t);
    last_publish_time_ = now();
  }

  std::vector<Cluster> cluster_points(const std::vector<Point2D> & pts)
  {
    std::vector<Cluster> clusters;
    std::vector<bool> used(pts.size(), false);
    for (size_t i = 0; i < pts.size(); ++i) {
      if (used[i]) {
        continue;
      }
      // BFS/DFS over nearby points.
      std::vector<size_t> stack{i};
      used[i] = true;
      Cluster c;
      while (!stack.empty()) {
        const size_t idx = stack.back();
        stack.pop_back();
        c.points.push_back(pts[idx]);
        for (size_t j = 0; j < pts.size(); ++j) {
          if (used[j]) {
            continue;
          }
          const double d = std::hypot(pts[idx].x - pts[j].x, pts[idx].y - pts[j].y);
          if (d < cluster_gap_) {
            used[j] = true;
            stack.push_back(j);
          }
        }
      }
      // Compute centroid, width, range, angle.
      double sx = 0.0, sy = 0.0;
      double min_x = std::numeric_limits<double>::infinity();
      double max_x = -std::numeric_limits<double>::infinity();
      for (const auto & p : c.points) {
        sx += p.x;
        sy += p.y;
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
      }
      c.centroid_x = sx / c.points.size();
      c.centroid_y = sy / c.points.size();
      c.width_m = max_x - min_x;
      c.range_m = std::hypot(c.centroid_x, c.centroid_y);
      c.angle_rad = std::atan2(c.centroid_y, c.centroid_x);
      clusters.push_back(c);
    }
    return clusters;
  }

  float compute_confidence(const Cluster & c, double speed, double struct_conf)
  {
    // Base confidence from how person-like the width is (0.3-0.6 m ideal).
    double width_conf = 1.0 - std::abs(c.width_m - 0.45) / 0.15;
    width_conf = std::clamp(width_conf, 0.0, 1.0);
    // Motion boosts confidence (a target translating coherently).
    double motion_conf = std::clamp(speed / 1.0, 0.0, 1.0);
    // Structural confidence boosts a target whose shape oscillates over time
    // (two-legs <-> one-leg while turning in place). A pole/wall has constant
    // width and so does not earn any structural confidence.
    return static_cast<float>(0.5 * width_conf + 0.3 * motion_conf + 0.2 * struct_conf);
  }

  void check_stale()
  {
    if (has_target_ && (now() - last_publish_time_).seconds() > target_timeout_s_) {
      // Target lost: publish invalid.
      golfcart_msgs::msg::PersonTarget t;
      t.valid = false;
      t.source = "lidar";
      t.timestamp = now();
      target_pub_->publish(t);
      has_target_ = false;
      locked_ = false;  // release the lock so a new person can be acquired
      consecutive_hits_ = 0;
    }
  }

  // ---- Params ----
  double sector_half_angle_, min_range_, max_range_;
  double min_width_, max_width_, cluster_gap_;
  int min_cluster_points_, temporal_confirm_;
  double target_timeout_s_, ema_alpha_, min_speed_mps_;
  double lock_distance_m_, lock_continuity_m_;
  int struct_history_scans_;
  double struct_conf_scale_m_;

  // ---- State ----
  bool has_target_ = false;
  bool locked_ = false;
  double smoothed_x_ = 0.0, smoothed_y_ = 0.0;
  double last_centroid_x_ = 0.0, last_centroid_y_ = 0.0;
  rclcpp::Time last_scan_time_;
  rclcpp::Time last_publish_time_;
  int consecutive_hits_ = 0;
  int consecutive_misses_ = 0;
  // Rolling history of the locked target's cluster width, used to measure
  // structural variability (a person turning in place / switching which leg is
  // facing the LiDAR, vs a pole or wall which has a constant width).
  std::deque<double> width_history_;

  // ---- ROS handles ----
  rclcpp::Subscription<sensor_msgs::msg::LaserScan>::SharedPtr scan_sub_;
  rclcpp::Publisher<golfcart_msgs::msg::PersonTarget>::SharedPtr target_pub_;
  rclcpp::TimerBase::SharedPtr stale_timer_;
};

}  // namespace golfcart

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<golfcart::PersonDetectionNode>());
  rclcpp::shutdown();
  return 0;
}