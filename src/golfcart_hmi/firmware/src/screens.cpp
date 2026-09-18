// LVGL screen implementations for the handle-unit HMI.
// See screens.h for the public API.

#include "screens.h"
#include "handle_protocol.h"
#include "sensors.h"

// ---------------------------------------------------------------------------
// LVGL includes (TFT_eSPI backend for the ST7796).
// ---------------------------------------------------------------------------
#include <lvgl.h>
#include <TFT_eSPI.h>
#include <qrcode.h>  // ricmoo/QRCode: pure-C QR matrix generation

// ---------------------------------------------------------------------------
// Display + LVGL setup
// ---------------------------------------------------------------------------
static TFT_eSPI tft = TFT_eSPI();
static lv_disp_drv_t disp_drv;
static lv_disp_t *disp;

// Elecrow 14-pin header pin 8 (LED) = backlight. PWM-driven for dimming.
// GPIO 9 is free (firmware uses GPIO 4-8 for joystick + HX711).
#define BACKLIGHT_PIN 9

// ---------------------------------------------------------------------------
// Shared state cache (populated by the Pi via STATE_UPDATE).
// ---------------------------------------------------------------------------
typedef struct {
  int16_t battery_pct;
  int16_t speed_mps;          // x100
  uint8_t safety_state;
  uint8_t mode;
  int32_t gps_lat;            // x1e7
  int32_t gps_lon;            // x1e7
  uint16_t gps_speed;         // x100
  uint8_t gps_sats;
  int16_t imu_roll;           // x1000 rad
  int16_t imu_pitch;          // x1000 rad
  uint8_t obstacle;
  uint16_t obstacle_nearest;  // x100 m
  uint8_t geofence;
  int16_t speed_zone_limit;   // x100, -1 = none
  int16_t slope_deg;          // x100
  uint8_t nav_status;
  uint8_t hole_number;
  uint16_t hole_distance;
  uint16_t hole_remaining;
  int16_t push_force;         // x100 N
  uint8_t assist_level;
  uint8_t assist_enabled;
  uint8_t hill_assist_enabled;
  uint8_t steering_assist_enabled;
  uint16_t time_hhmm;
  uint16_t range_m;           // estimated remaining range (m)
  uint16_t return_m;          // estimated return distance (m)
  uint8_t range_state;        // 0=OK, 1=CAUTION, 2=CRITICAL
  int16_t holes_remaining;    // estimated holes left, -1 = unknown
  uint8_t slip;               // 0=no slip, 1=wheels slipping
  uint16_t capability;        // bitmask of present sensors (see CAP_*)
  uint8_t segmentation;       // 0=off, 1=active
  uint16_t map_x;             // trolley map x (cm)
  uint16_t map_y;             // trolley map y (cm)
  int16_t map_heading;        // trolley heading (deg x10)
  uint8_t map_available;      // 1 when a course map is loaded
  uint8_t alert;              // active alert code (ALERT_*)
  uint8_t round_active;       // 1 while a round is in progress
  uint16_t round_distance;    // round distance (m)
  uint16_t round_energy;      // round energy (Wh x10)
  uint16_t round_duration;    // round duration (s)
  uint16_t round_avg_speed;   // round avg speed (cm/s)
  uint16_t speed_limit;       // selected max speed (cm/s)
} HandleState;

static HandleState g_state;
static uint8_t g_current_screen = SCR_COURSE;

// ---------------------------------------------------------------------------
// WiFi credentials (from DL_CONFIG) for the WiFi screen + QR code.
// ---------------------------------------------------------------------------
static char g_wifi_ssid[64] = "golfcart-xxxx";
static char g_wifi_pass[64] = "12345678";

// ---------------------------------------------------------------------------
// Cached course-map bitmap (from DL_MAP_FRAME). Rendered on both the map
// (SCR_MAP) and hole (SCR_HOLE) screens. 148x190 RGB565 = ~56 KB.
// ---------------------------------------------------------------------------
#define MAP_CACHE_MAX_W 160
#define MAP_CACHE_MAX_H 200
static uint8_t g_map_cache[MAP_CACHE_MAX_W * MAP_CACHE_MAX_H * 2];
static uint16_t g_map_w = 0;
static uint16_t g_map_h = 0;

// ---------------------------------------------------------------------------
// LVGL display flush callback (TFT_eSPI).
// ---------------------------------------------------------------------------
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area,
                       lv_color_t *color_p)
{
  uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
  uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)&color_p->ch, w * h, true);
  tft.endWrite();
  lv_disp_flush_ready(drv);
}

// ---------------------------------------------------------------------------
// Palette (matches docs/hmi-spec.md §9.1).
// ---------------------------------------------------------------------------
#define C_BG        lv_color_hex(0x0F172A)
#define C_SURFACE   lv_color_hex(0x1E293B)
#define C_SURFACE2  lv_color_hex(0x334155)
#define C_TEXT      lv_color_hex(0xF8FAFC)
#define C_TEXT_DIM  lv_color_hex(0x94A3B8)
#define C_ACCENT    lv_color_hex(0x38BDF8)
#define C_OK        lv_color_hex(0x34D399)
#define C_WARN      lv_color_hex(0xFBBF24)
#define C_DANGER    lv_color_hex(0xF87171)
#define C_INFO      lv_color_hex(0x818CF8)

// ---------------------------------------------------------------------------
// Widget helpers
// ---------------------------------------------------------------------------
static lv_obj_t *scr;  // current screen container

// Forward declarations (defined below; make_screen calls them).
static void clear_hits(void);
static void clear_labels(void);

static lv_obj_t *make_screen(void)
{
  lv_obj_t *s = lv_scr_act();
  lv_obj_clean(s);
  lv_obj_set_style_bg_color(s, C_BG, 0);
  clear_hits();
  clear_labels();
  return s;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            int w, int h, lv_color_t color)
{
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_size(l, w, h);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_color(l, color, 0);
  lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
  return l;
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y,
                             int w, int h, lv_color_t color)
{
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_size(b, w, h);
  lv_obj_set_style_bg_color(b, color, 0);
  lv_obj_set_style_radius(b, 8, 0);
  lv_obj_set_style_text_color(b, C_TEXT, 0);
  lv_obj_set_style_text_font(b, &lv_font_montserrat_16, 0);
  // LVGL 8.x buttons have no set_text; add a centered label child.
  lv_obj_t *l = lv_label_create(b);
  lv_label_set_text(l, text);
  lv_obj_center(l);
  return b;
}

static lv_obj_t *make_header(const char *title)
{
  lv_obj_t *h = lv_label_create(scr);
  lv_obj_set_pos(h, 0, 0);
  lv_obj_set_size(h, 320, 32);
  lv_obj_set_style_bg_color(h, C_SURFACE, 0);
  lv_label_set_text(h, title);
  lv_obj_set_style_text_color(h, C_TEXT, 0);
  lv_obj_set_style_text_font(h, &lv_font_montserrat_16, 0);
  return h;
}

// ---------------------------------------------------------------------------
// Hit-region table (tap -> menu item id).
// Each screen registers (x0, y0, x1, y1, item_id). A tap inside a region
// returns the item id; the Pi maps it to an action.
// ---------------------------------------------------------------------------
#define MAX_HIT_REGIONS 16
typedef struct {
  int x0, y0, x1, y1;
  int item;
} HitRegion;

static HitRegion g_hits[MAX_HIT_REGIONS];
static int g_hit_count = 0;

static void clear_hits(void)
{
  g_hit_count = 0;
}

static void add_hit(int x0, int y0, int x1, int y1, int item)
{
  if (g_hit_count < MAX_HIT_REGIONS) {
    g_hits[g_hit_count].x0 = x0;
    g_hits[g_hit_count].y0 = y0;
    g_hits[g_hit_count].x1 = x1;
    g_hits[g_hit_count].y1 = y1;
    g_hits[g_hit_count].item = item;
    ++g_hit_count;
  }
}

static int hit_test(int x, int y)
{
  for (int i = 0; i < g_hit_count; ++i) {
    if (x >= g_hits[i].x0 && x <= g_hits[i].x1 &&
        y >= g_hits[i].y0 && y <= g_hits[i].y1) {
      return g_hits[i].item;
    }
  }
  return -1;
}

// ---------------------------------------------------------------------------
// Label registry (for live refresh from g_state).
// Each screen registers labels it wants updated; screens_refresh() sets their
// text from the current state.
// ---------------------------------------------------------------------------
#define MAX_LABELS 24
typedef struct {
  lv_obj_t *obj;
  uint8_t kind;   // which state value to render (see LABEL_*)
  uint8_t bit;    // for LABEL_CAPABILITY: which capability bit to render
} LabelRef;

#define LABEL_BATTERY_PCT 1
#define LABEL_SPEED_MPS   2
#define LABEL_HOLE_DIST   3
#define LABEL_HOLE_REM    4
#define LABEL_IMU_ROLL    5
#define LABEL_IMU_PITCH   6
#define LABEL_OBSTACLE    7
#define LABEL_GPS_LAT     8
#define LABEL_GPS_LON     9
#define LABEL_GPS_SPEED   10
#define LABEL_GPS_SATS    11
#define LABEL_NAV_STATUS  12
#define LABEL_SLOPE_DEG   13
#define LABEL_GEOFENCE    14
#define LABEL_SAFETY      15
#define LABEL_MODE        16
#define LABEL_STEERING_ASSIST 17
#define LABEL_HILL_ASSIST 32
#define LABEL_RANGE_M     18
#define LABEL_RETURN_M    19
#define LABEL_RANGE_STATE 20
#define LABEL_SLIP        21
#define LABEL_CAPABILITY  22
#define LABEL_SEGMENTATION 23
#define LABEL_HOLES_REMAINING 24
#define LABEL_MAP_POS     25
#define LABEL_MAP_AVAIL   26
#define LABEL_ROUND_DIST  27
#define LABEL_ROUND_ENERGY 28
#define LABEL_ROUND_TIME  29
#define LABEL_ROUND_AVG   30
#define LABEL_SPEED_LIMIT 31

static LabelRef g_labels[MAX_LABELS];
static int g_label_count = 0;

static void clear_labels(void)
{
  g_label_count = 0;
}

static void add_label_bit(lv_obj_t *obj, uint8_t kind, uint8_t bit)
{
  if (g_label_count < MAX_LABELS) {
    g_labels[g_label_count].obj = obj;
    g_labels[g_label_count].kind = kind;
    g_labels[g_label_count].bit = bit;
    ++g_label_count;
  }
}

static void add_label(lv_obj_t *obj, uint8_t kind)
{
  add_label_bit(obj, kind, 0);
}

static void set_label_text(LabelRef *lr)
{
  char buf[32];
  switch (lr->kind) {
    case LABEL_BATTERY_PCT:
      snprintf(buf, sizeof(buf), "Battery  %d%%", g_state.battery_pct);
      break;
    case LABEL_SPEED_MPS:
      snprintf(buf, sizeof(buf), "Speed    %.2f m/s", g_state.speed_mps / 100.0);
      break;
    case LABEL_HOLE_DIST:
      snprintf(buf, sizeof(buf), "Dist: %d m", g_state.hole_distance);
      break;
    case LABEL_HOLE_REM:
      snprintf(buf, sizeof(buf), "Rem: %d m", g_state.hole_remaining);
      break;
    case LABEL_IMU_ROLL:
      snprintf(buf, sizeof(buf), "Roll   %+.2f rad", g_state.imu_roll / 1000.0);
      break;
    case LABEL_IMU_PITCH:
      snprintf(buf, sizeof(buf), "Pitch  %+.2f rad", g_state.imu_pitch / 1000.0);
      break;
    case LABEL_OBSTACLE:
      snprintf(buf, sizeof(buf), "Obstacle: %s",
               g_state.obstacle ? "OBSTACLE" : "NONE");
      break;
    case LABEL_GPS_LAT:
      snprintf(buf, sizeof(buf), "Lat  %d.%07d", g_state.gps_lat / 10000000,
               g_state.gps_lat % 10000000);
      break;
    case LABEL_GPS_LON:
      snprintf(buf, sizeof(buf), "Lon  %d.%07d", g_state.gps_lon / 10000000,
               g_state.gps_lon % 10000000);
      break;
    case LABEL_GPS_SPEED:
      snprintf(buf, sizeof(buf), "Speed %.2f m/s", g_state.gps_speed / 100.0);
      break;
    case LABEL_GPS_SATS:
      snprintf(buf, sizeof(buf), "Sats  %d", g_state.gps_sats);
      break;
    case LABEL_NAV_STATUS: {
      const char *names[] = {"IDLE", "PLANNING", "DRIVING", "PAUSED",
                             "ARRIVED", "ERROR"};
      uint8_t s = g_state.nav_status;
      snprintf(buf, sizeof(buf), "Status: %s",
               s < 6 ? names[s] : "UNKNOWN");
      break;
    }
    case LABEL_SLOPE_DEG:
      snprintf(buf, sizeof(buf), "Slope  %.2f deg", g_state.slope_deg / 100.0);
      break;
    case LABEL_GEOFENCE: {
      const char *names[] = {"OK", "NEAR", "CROSSED", "NO FIX"};
      uint8_t s = g_state.geofence;
      snprintf(buf, sizeof(buf), "Geofence: %s",
               s < 4 ? names[s] : "UNKNOWN");
      break;
    }
    case LABEL_SAFETY: {
      const char *names[] = {"STOPPED", "READY", "MOVING", "LIMITED", "FAULT"};
      uint8_t s = g_state.safety_state;
      snprintf(buf, sizeof(buf), "Safety: %s",
               s < 5 ? names[s] : "UNKNOWN");
      break;
    }
    case LABEL_MODE: {
      const char *names[] = {"MANUAL", "FOLLOW", "AUTO", "TELEOP"};
      uint8_t s = g_state.mode;
      snprintf(buf, sizeof(buf), "Mode: %s", s < 4 ? names[s] : "UNKNOWN");
      break;
    }
    case LABEL_STEERING_ASSIST:
      snprintf(buf, sizeof(buf), "%s",
               g_state.steering_assist_enabled ? "ON" : "OFF");
      break;
    case LABEL_HILL_ASSIST:
      snprintf(buf, sizeof(buf), "%s",
               g_state.hill_assist_enabled ? "ON" : "OFF");
      break;
    case LABEL_RANGE_M:
      snprintf(buf, sizeof(buf), "%d m", g_state.range_m);
      break;
    case LABEL_RETURN_M:
      snprintf(buf, sizeof(buf), "%d m", g_state.return_m);
      break;
    case LABEL_RANGE_STATE: {
      const char *names[] = {"OK", "CAUTION", "CRITICAL"};
      uint8_t s = g_state.range_state;
      snprintf(buf, sizeof(buf), "%s", s < 3 ? names[s] : "UNKNOWN");
      break;
    }
    case LABEL_SLIP:
      snprintf(buf, sizeof(buf), "%s", g_state.slip ? "SLIP" : "OK");
      break;
    case LABEL_CAPABILITY: {
      // Render one sensor's presence from the capability bitmask.
      const char *names[] = {"LiDAR H", "LiDAR T", "GPS", "IMU",
                             "Battery", "Camera", "Coral", "ODrive"};
      uint8_t b = lr->bit;
      const bool present = (g_state.capability >> b) & 1u;
      snprintf(buf, sizeof(buf), "%-8s %s",
               b < 8 ? names[b] : "?",
               present ? "OK" : "MISSING");
      break;
    }
    case LABEL_SEGMENTATION:
      snprintf(buf, sizeof(buf), "Segmentation: %s",
               g_state.segmentation ? "ON" : "OFF");
      break;
    case LABEL_HOLES_REMAINING:
      if (g_state.holes_remaining < 0)
        snprintf(buf, sizeof(buf), "--");
      else
        snprintf(buf, sizeof(buf), "%d", g_state.holes_remaining);
      break;
    case LABEL_MAP_POS:
      snprintf(buf, sizeof(buf), "x: %d  y: %d", g_state.map_x, g_state.map_y);
      break;
    case LABEL_MAP_AVAIL:
      snprintf(buf, sizeof(buf), "%s", g_state.map_available ? "MAP" : "No map");
      break;
    case LABEL_ROUND_DIST:
      snprintf(buf, sizeof(buf), "%d m", g_state.round_distance);
      break;
    case LABEL_ROUND_ENERGY:
      snprintf(buf, sizeof(buf), "%.1f Wh", g_state.round_energy / 10.0);
      break;
    case LABEL_ROUND_TIME: {
      uint16_t s = g_state.round_duration;
      snprintf(buf, sizeof(buf), "%d:%02d", s / 60, s % 60);
      break;
    }
    case LABEL_ROUND_AVG:
      snprintf(buf, sizeof(buf), "%.2f m/s", g_state.round_avg_speed / 100.0);
      break;
    case LABEL_SPEED_LIMIT:
      snprintf(buf, sizeof(buf), "%.1f m/s", g_state.speed_limit / 100.0);
      break;
    default:
      return;
  }
  lv_label_set_text(lr->obj, buf);
}

// ---------------------------------------------------------------------------
// Screen builders
// ---------------------------------------------------------------------------

// Main Menu (SCR_MENU): vertical list of items.
static void build_menu(void)
{
  scr = make_screen();
  make_header("Main Menu");
  const char *items[] = {"MAP", "DRIVE DIST", "MODE", "ASSIST", "SPEED",
                         "QUICK SEL", "ENERGY", "CHANGE HOLE", "SELECT COURSE",
                         "WIFI", "END ROUND", "DEBUG", "SHUTDOWN"};
  int y = 40;
  for (int i = 0; i < 13; ++i) {
    make_button(scr, items[i], 20, y, 280, 40, C_SURFACE2);
    add_hit(20, y, 300, y + 40, i);  // item id = index
    y += 48;
  }
}

// Course selection (SCR_COURSE).
static void build_course(void)
{
  scr = make_screen();
  make_header("Select Course");
  // Placeholder list; the Pi sends the actual course list via CONFIG.
  make_button(scr, "Course 1", 20, 44, 280, 40, C_SURFACE2);
  add_hit(20, 44, 300, 84, 0);
  make_button(scr, "Course 2", 20, 92, 280, 40, C_SURFACE2);
  add_hit(20, 92, 300, 132, 1);
  make_button(scr, "Main Menu", 20, 140, 280, 40, C_SURFACE2);
  add_hit(20, 140, 300, 180, 2);
}

// Tee selection (SCR_TEE).
static void build_tee(void)
{
  scr = make_screen();
  make_header("Select Tee");
  make_button(scr, "Red", 20, 44, 280, 40, C_SURFACE2);
  add_hit(20, 44, 300, 84, 0);
  make_button(scr, "Blue", 20, 92, 280, 40, C_SURFACE2);
  add_hit(20, 92, 300, 132, 1);
  make_button(scr, "Course Selection", 20, 140, 280, 40, C_SURFACE2);
  add_hit(20, 140, 300, 180, 2);
}

// Map / Hole view (SCR_HOLE).
static void render_map_bitmap(void);  // fwd decl (defined below)
static void render_wifi_qr(int x0, int y0, int size);  // fwd decl (defined below)

static void build_hole(void)
{
  scr = make_screen();
  make_header("Hole");
  // Distance + remaining.
  lv_obj_t *dist = make_label(scr, "Dist: -- m", 12, 52, 140, 24, C_TEXT);
  add_label(dist, LABEL_HOLE_DIST);
  lv_obj_t *rem = make_label(scr, "Rem: -- m", 168, 52, 140, 24, C_TEXT);
  add_label(rem, LABEL_HOLE_REM);
  // Map area (the Pi sends the hole layout via DL_MAP_FRAME; the cached
  // bitmap is blitted here).
  lv_obj_t *map = lv_obj_create(scr);
  lv_obj_set_pos(map, 12, 80);
  lv_obj_set_size(map, 296, 372);
  lv_obj_set_style_bg_color(map, C_SURFACE, 0);
  lv_obj_set_style_border_width(map, 2, 0);
  lv_obj_set_style_border_color(map, C_TEXT, 0);
  render_map_bitmap();
}

// Map view (SCR_MAP).
// Shows a top-down course map with the trolley position. The Pi renders the
// map bitmap (downsampled from CourseMap) and sends it via DL_MAP_FRAME; the
// trolley position/heading arrive as ST_MAP_* state values. When no map is
// loaded, show a "No map" placeholder.
static void build_map(void)
{
  scr = make_screen();
  make_header("Map");
  // Map area (the Pi blits the bitmap here via DL_MAP_FRAME).
  lv_obj_t *map = lv_obj_create(scr);
  lv_obj_set_pos(map, 12, 40);
  lv_obj_set_size(map, 296, 380);
  lv_obj_set_style_bg_color(map, C_SURFACE, 0);
  lv_obj_set_style_border_width(map, 2, 0);
  lv_obj_set_style_border_color(map, C_TEXT, 0);
  render_map_bitmap();
  // Trolley position label (bottom).
  lv_obj_t *pos = make_label(scr, "x: --  y: --", 12, 428, 200, 24, C_TEXT);
  add_label(pos, LABEL_MAP_POS);
  lv_obj_t *avail = make_label(scr, "No map", 220, 428, 88, 24, C_TEXT_DIM);
  add_label(avail, LABEL_MAP_AVAIL);
  make_button(scr, "Main Menu", 12, 460, 296, 20, C_SURFACE2);
  add_hit(12, 460, 308, 480, 0);
}

// Round summary (SCR_ROUND_SUMMARY).
// Shows the just-finished round's stats (distance, energy, time, avg speed).
// The Pi navigates here when a round ends (TripSummary active=false).
static void build_round_summary(void)
{
  scr = make_screen();
  make_header("Round Summary");
  make_label(scr, "Distance", 20, 60, 180, 24, C_TEXT_DIM);
  lv_obj_t *dist = make_label(scr, "-- m", 220, 60, 80, 24, C_TEXT);
  add_label(dist, LABEL_ROUND_DIST);
  make_label(scr, "Energy", 20, 108, 180, 24, C_TEXT_DIM);
  lv_obj_t *energy = make_label(scr, "-- Wh", 220, 108, 80, 24, C_TEXT);
  add_label(energy, LABEL_ROUND_ENERGY);
  make_label(scr, "Time", 20, 156, 180, 24, C_TEXT_DIM);
  lv_obj_t *time = make_label(scr, "--:--", 220, 156, 80, 24, C_TEXT);
  add_label(time, LABEL_ROUND_TIME);
  make_label(scr, "Avg Speed", 20, 204, 180, 24, C_TEXT_DIM);
  lv_obj_t *avg = make_label(scr, "-- m/s", 220, 204, 80, 24, C_TEXT);
  add_label(avg, LABEL_ROUND_AVG);
  make_button(scr, "Main Menu", 20, 260, 280, 40, C_SURFACE2);
  add_hit(20, 260, 300, 300, 0);
}

// Speed bar (SCR_SPEED).
// Lets the operator select the max speed in manual/push-assist mode. The
// current selection is shown; +/- buttons adjust it. The Pi applies the cap.
static void build_speed(void)
{
  scr = make_screen();
  make_header("Speed");
  // Current speed limit (big).
  lv_obj_t *lim = make_label(scr, "-- m/s", 20, 60, 280, 48, C_TEXT);
  add_label(lim, LABEL_SPEED_LIMIT);
  lv_obj_set_style_text_font(lim, &lv_font_montserrat_16, 0);
  // Minus / Plus buttons.
  make_button(scr, "-", 20, 140, 120, 60, C_SURFACE2);
  add_hit(20, 140, 140, 200, 0);
  make_button(scr, "+", 180, 140, 120, 60, C_SURFACE2);
  add_hit(180, 140, 300, 200, 1);
  // Preset buttons.
  make_button(scr, "Slow", 20, 220, 80, 40, C_SURFACE2);
  add_hit(20, 220, 100, 260, 2);
  make_button(scr, "Med", 110, 220, 80, 40, C_SURFACE2);
  add_hit(110, 220, 190, 260, 3);
  make_button(scr, "Fast", 200, 220, 80, 40, C_SURFACE2);
  add_hit(200, 220, 280, 260, 4);
  make_button(scr, "Main Menu", 20, 280, 280, 40, C_SURFACE2);
  add_hit(20, 280, 300, 320, 5);
}

// Quick select (SCR_QUICK_SELECT).
// A grid of hole numbers (1-18) to jump directly to a hole, plus a Main Menu
// button. The Pi calls /course/hole with the chosen hole number.
static void build_quick_select(void)
{
  scr = make_screen();
  make_header("Quick Select");
  // 6 columns x 3 rows of hole buttons.
  const int cols = 6;
  const int bw = 44, bh = 44, gap = 6;
  const int x0 = 12, y0 = 44;
  int idx = 0;
  for (int row = 0; row < 3; ++row) {
    for (int col = 0; col < cols; ++col) {
      int hole = idx + 1;
      int x = x0 + col * (bw + gap);
      int y = y0 + row * (bh + gap);
      char label[4];
      snprintf(label, sizeof(label), "%d", hole);
      make_button(scr, label, x, y, bw, bh, C_SURFACE2);
      add_hit(x, y, x + bw, y + bh, idx);
      ++idx;
    }
  }
  make_button(scr, "Main Menu", 12, 200, 296, 40, C_SURFACE2);
  add_hit(12, 200, 308, 240, 18);
}

// Mode selection (SCR_MODE).
static void build_mode(void)
{
  scr = make_screen();
  make_header("Mode");
  make_button(scr, "Manual/Push Ass.", 20, 44, 280, 40, C_SURFACE2);
  add_hit(20, 44, 300, 84, 0);
  make_button(scr, "Follow Me", 20, 92, 280, 40, C_SURFACE2);
  add_hit(20, 92, 300, 132, 1);
  make_button(scr, "Autonomous", 20, 140, 280, 40, C_SURFACE2);
  add_hit(20, 140, 300, 180, 2);
  make_button(scr, "Main Menu", 20, 188, 280, 40, C_SURFACE2);
  add_hit(20, 188, 300, 228, 3);
}

// Assist (SCR_ASSIST).
static void build_assist(void)
{
  scr = make_screen();
  make_header("Assist");
  make_label(scr, "Push Assist", 20, 44, 180, 24, C_TEXT);
  make_label(scr, "OFF", 220, 44, 80, 24, C_TEXT_DIM);
  add_hit(20, 44, 300, 68, 0);
  make_label(scr, "Assist Level", 20, 92, 180, 24, C_TEXT);
  make_label(scr, "3", 220, 92, 80, 24, C_TEXT);
  add_hit(20, 92, 300, 116, 1);
  make_label(scr, "Hill Assist", 20, 140, 180, 24, C_TEXT);
  lv_obj_t *ha = make_label(scr, "OFF", 220, 140, 80, 24, C_TEXT_DIM);
  add_label(ha, LABEL_HILL_ASSIST);
  add_hit(20, 140, 300, 164, 2);
  make_label(scr, "Steering Assist", 20, 188, 180, 24, C_TEXT);
  lv_obj_t *sa = make_label(scr, "ON", 220, 188, 80, 24, C_TEXT_DIM);
  add_label(sa, LABEL_STEERING_ASSIST);
  add_hit(20, 188, 300, 212, 3);
  make_button(scr, "Main Menu", 20, 236, 280, 40, C_SURFACE2);
  add_hit(20, 236, 300, 276, 4);
}

// Energy dashboard (SCR_ENERGY).
// Shows the battery range estimator's /range/status: remaining range, hole
// remaining, return distance, and the OK/CAUTION/CRITICAL state.
static void build_energy(void)
{
  scr = make_screen();
  make_header("Energy");

  // Range (big, colored by state).
  lv_obj_t *range = make_label(scr, "-- m", 20, 44, 280, 40, C_TEXT);
  add_label(range, LABEL_RANGE_M);
  lv_obj_set_style_text_font(range, &lv_font_montserrat_16, 0);

  // State badge.
  lv_obj_t *st = make_label(scr, "OK", 20, 92, 280, 24, C_OK);
  add_label(st, LABEL_RANGE_STATE);

  // Remaining hole distance.
  make_label(scr, "Remaining", 20, 140, 180, 24, C_TEXT_DIM);
  lv_obj_t *rem = make_label(scr, "-- m", 220, 140, 80, 24, C_TEXT);
  add_label(rem, LABEL_HOLE_REM);

  // Return distance.
  make_label(scr, "Return", 20, 188, 180, 24, C_TEXT_DIM);
  lv_obj_t *ret = make_label(scr, "-- m", 220, 188, 80, 24, C_TEXT);
  add_label(ret, LABEL_RETURN_M);

  // Battery.
  make_label(scr, "Battery", 20, 236, 180, 24, C_TEXT_DIM);
  lv_obj_t *bat = make_label(scr, "--%", 220, 236, 80, 24, C_TEXT);
  add_label(bat, LABEL_BATTERY_PCT);

  // Holes left (predictive range).
  make_label(scr, "Holes Left", 20, 284, 180, 24, C_TEXT_DIM);
  lv_obj_t *holes = make_label(scr, "--", 220, 284, 80, 24, C_TEXT);
  add_label(holes, LABEL_HOLES_REMAINING);

  make_button(scr, "Main Menu", 20, 332, 280, 40, C_SURFACE2);
  add_hit(20, 332, 300, 372, 0);
}

// Sensor status (SCR_SENSORS).
// Shows which hardware sensors are present vs. missing, from the capability
// bitmask (ST_CAPABILITY). Each row is a sensor name + OK/MISSING.
static void build_sensors(void)
{
  scr = make_screen();
  make_header("Sensors");
  const int y0 = 44;
  const int dy = 40;
  for (int b = 0; b < 8; ++b) {
    lv_obj_t *l = make_label(scr, "--", 20, y0 + b * dy, 280, 24, C_TEXT);
    add_label_bit(l, LABEL_CAPABILITY, (uint8_t)b);
  }
  make_button(scr, "Main Menu", 20, y0 + 8 * dy, 280, 40, C_SURFACE2);
  add_hit(20, y0 + 8 * dy, 300, y0 + 8 * dy + 40, 0);
}

// Drive Distance (SCR_DRIVE_DIST).
// Lets the operator drive a fixed distance in the current heading direction.
// Buttons: 10/20/30/40/50 m + Cancel.
static void build_drive_dist(void)
{
  scr = make_screen();
  make_header("Drive Distance");
  const int dists[] = {10, 20, 30, 40, 50};
  int y = 44;
  for (int i = 0; i < 5; ++i) {
    char label[16];
    snprintf(label, sizeof(label), "%d m", dists[i]);
    make_button(scr, label, 20, y, 280, 40, C_SURFACE2);
    add_hit(20, y, 300, y + 40, i);
    y += 48;
  }
  make_button(scr, "Cancel", 20, y, 280, 40, C_DANGER);
  add_hit(20, y, 300, y + 40, 5);
  y += 48;
  make_button(scr, "Main Menu", 20, y, 280, 40, C_SURFACE2);
  add_hit(20, y, 300, y + 40, 6);
}

// Change Hole (SCR_CHANGE_HOLE).
static void build_change_hole(void)
{
  scr = make_screen();
  make_header("Change Hole");
  make_button(scr, "Hole 1  [380 m]", 20, 44, 280, 40, C_SURFACE2);
  add_hit(20, 44, 300, 84, 0);
  make_button(scr, "Hole 2  [350 m]", 20, 92, 280, 40, C_SURFACE2);
  add_hit(20, 92, 300, 132, 1);
  make_button(scr, "Hole 3  [410 m]", 20, 140, 280, 40, C_SURFACE2);
  add_hit(20, 140, 300, 180, 2);
  make_button(scr, "Course Selection", 20, 188, 280, 40, C_SURFACE2);
  add_hit(20, 188, 300, 228, 3);
}

// WiFi (SCR_WIFI).
static void build_wifi(void)
{
  scr = make_screen();
  make_header("WiFi");
  char ssid_label[80];
  snprintf(ssid_label, sizeof(ssid_label), "SSID: %s", g_wifi_ssid);
  make_label(scr, ssid_label, 20, 44, 280, 24, C_TEXT);
  char pass_label[80];
  snprintf(pass_label, sizeof(pass_label), "Pass: %s", g_wifi_pass);
  make_label(scr, pass_label, 20, 76, 280, 24, C_TEXT);
  // QR code encoding WIFI:S:<ssid>;P:<pass>;; (scannable with a phone).
  lv_obj_t *qr = lv_obj_create(scr);
  lv_obj_set_pos(qr, 110, 120);
  lv_obj_set_size(qr, 100, 100);
  lv_obj_set_style_bg_color(qr, C_SURFACE2, 0);
  render_wifi_qr(110, 120, 100);
  make_button(scr, "Main Menu", 20, 240, 280, 40, C_SURFACE2);
  add_hit(20, 240, 300, 280, 0);
}

// Debug menu (SCR_DEBUG).
static void build_debug(void)
{
  scr = make_screen();
  make_header("Debug");
  const char *items[] = {"System", "GPS", "LiDAR", "Camera", "IMU", "Navigation", "Sensors"};
  int y = 40;
  for (int i = 0; i < 7; ++i) {
    make_button(scr, items[i], 20, y, 280, 40, C_SURFACE2);
    add_hit(20, y, 300, y + 40, i);
    y += 48;
  }
  make_button(scr, "Main Menu", 20, y, 280, 40, C_SURFACE2);
  add_hit(20, y, 300, y + 40, 7);
}

// System debug (SCR_DEBUG_SYSTEM).
static void build_debug_system(void)
{
  scr = make_screen();
  make_header("System");
  lv_obj_t *bat = make_label(scr, "Battery  --%", 20, 44, 280, 24, C_TEXT);
  add_label(bat, LABEL_BATTERY_PCT);
  lv_obj_t *slip = make_label(scr, "Slip     OK", 20, 76, 280, 24, C_OK);
  add_label(slip, LABEL_SLIP);
  make_label(scr, "CPU      --%", 20, 108, 280, 24, C_TEXT);
  make_label(scr, "Disk     --%", 20, 140, 280, 24, C_TEXT);
  make_label(scr, "Uptime   --:--", 20, 172, 280, 24, C_TEXT);
  make_label(scr, "Temp     -- C", 20, 204, 280, 24, C_TEXT);
  make_button(scr, "Main Menu", 20, 248, 280, 40, C_SURFACE2);
  add_hit(20, 248, 300, 288, 0);
}

// GPS debug (SCR_DEBUG_GPS).
static void build_debug_gps(void)
{
  scr = make_screen();
  make_header("GPS");
  lv_obj_t *lat = make_label(scr, "Lat  --.------", 20, 44, 280, 24, C_TEXT);
  add_label(lat, LABEL_GPS_LAT);
  lv_obj_t *lon = make_label(scr, "Lon  --.------", 20, 76, 280, 24, C_TEXT);
  add_label(lon, LABEL_GPS_LON);
  lv_obj_t *spd = make_label(scr, "Speed -- m/s", 20, 108, 280, 24, C_TEXT);
  add_label(spd, LABEL_GPS_SPEED);
  lv_obj_t *sats = make_label(scr, "Sats  --", 20, 140, 280, 24, C_TEXT);
  add_label(sats, LABEL_GPS_SATS);
  make_label(scr, "Status NO FIX", 20, 172, 280, 24, C_DANGER);
  make_button(scr, "Main Menu", 20, 220, 280, 40, C_SURFACE2);
  add_hit(20, 220, 300, 260, 0);
}

// LiDAR debug (SCR_DEBUG_LIDAR).
static void build_debug_lidar(void)
{
  scr = make_screen();
  make_header("LiDAR");
  lv_obj_t *scan = lv_obj_create(scr);
  lv_obj_set_pos(scan, 20, 44);
  lv_obj_set_size(scan, 280, 200);
  lv_obj_set_style_bg_color(scan, C_SURFACE, 0);
  lv_obj_t *obs = make_label(scr, "Obstacle: NONE", 20, 260, 280, 24, C_OK);
  add_label(obs, LABEL_OBSTACLE);
  make_label(scr, "Nearest: -- m", 20, 292, 280, 24, C_TEXT);
  make_button(scr, "Main Menu", 20, 340, 280, 40, C_SURFACE2);
  add_hit(20, 340, 300, 380, 0);
}

// Camera debug (SCR_DEBUG_CAMERA).
static void build_debug_camera(void)
{
  scr = make_screen();
  make_header("Camera");
  lv_obj_t *cam = lv_obj_create(scr);
  lv_obj_set_pos(cam, 20, 44);
  lv_obj_set_size(cam, 280, 200);
  lv_obj_set_style_bg_color(cam, C_SURFACE, 0);
  lv_obj_t *cap = make_label(scr, "Camera   --", 20, 256, 280, 24, C_TEXT);
  add_label_bit(cap, LABEL_CAPABILITY, 5);  // bit 5 = Camera
  lv_obj_t *seg = make_label(scr, "Segmentation: OFF", 20, 292, 280, 24, C_TEXT_DIM);
  add_label(seg, LABEL_SEGMENTATION);
  make_button(scr, "Main Menu", 20, 340, 280, 40, C_SURFACE2);
  add_hit(20, 340, 300, 380, 0);
}

// IMU debug (SCR_DEBUG_IMU).
static void build_debug_imu(void)
{
  scr = make_screen();
  make_header("IMU");
  lv_obj_t *roll = make_label(scr, "Roll   +0.00 rad", 20, 44, 280, 24, C_TEXT);
  add_label(roll, LABEL_IMU_ROLL);
  lv_obj_t *pitch = make_label(scr, "Pitch  +0.00 rad", 20, 76, 280, 24, C_TEXT);
  add_label(pitch, LABEL_IMU_PITCH);
  make_label(scr, "Accel z -9.81 m/s2", 20, 108, 280, 24, C_TEXT);
  make_label(scr, "Status VALID", 20, 140, 280, 24, C_OK);
  make_button(scr, "Main Menu", 20, 188, 280, 40, C_SURFACE2);
  add_hit(20, 188, 300, 228, 0);
}

// Navigation debug (SCR_DEBUG_NAV).
static void build_debug_nav(void)
{
  scr = make_screen();
  make_header("Navigation");
  lv_obj_t *costmap = lv_obj_create(scr);
  lv_obj_set_pos(costmap, 20, 44);
  lv_obj_set_size(costmap, 280, 200);
  lv_obj_set_style_bg_color(costmap, C_SURFACE, 0);
  lv_obj_t *status = make_label(scr, "Status: IDLE", 20, 260, 280, 24, C_TEXT);
  add_label(status, LABEL_NAV_STATUS);
  make_label(scr, "Path len: -- m", 20, 292, 280, 24, C_TEXT);
  make_button(scr, "Main Menu", 20, 340, 280, 40, C_SURFACE2);
  add_hit(20, 340, 300, 380, 0);
}

// ---------------------------------------------------------------------------
// Splash / boot screen (SCR_SPLASH).
// Shown immediately on power-up while the Pi boots. The Pi pushes progress
// via DL_BOOT_STATUS; the ESP32 animates a spinner in screens_tick().
// ---------------------------------------------------------------------------
static lv_obj_t *splash_status_label;
static lv_obj_t *splash_progress_label;
static uint8_t g_boot_progress = 0;
static char g_boot_text[32] = "Starting...";

static void build_splash(void)
{
  scr = make_screen();
  // Logo / title.
  lv_obj_t *title = make_label(scr, "GOLF CART", 0, 150, 320, 40, C_ACCENT);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 150);
  make_label(scr, "Autonomous Push Trolley", 0, 196, 320, 24, C_TEXT_DIM);

  // Spinner (drawn as an arc in screens_tick).
  lv_obj_t *spinner = lv_obj_create(scr);
  lv_obj_set_pos(spinner, 140, 240);
  lv_obj_set_size(spinner, 40, 40);
  lv_obj_set_style_bg_color(spinner, C_BG, 0);
  lv_obj_set_style_border_width(spinner, 0, 0);

  // Status + progress labels.
  splash_status_label = make_label(scr, "Starting...", 0, 300, 320, 24, C_TEXT);
  lv_obj_align(splash_status_label, LV_ALIGN_TOP_MID, 0, 300);
  splash_progress_label = make_label(scr, "0%", 0, 330, 320, 24, C_TEXT_DIM);
  lv_obj_align(splash_progress_label, LV_ALIGN_TOP_MID, 0, 330);
}

// ---------------------------------------------------------------------------
// Screen registry
// ---------------------------------------------------------------------------
typedef void (*ScreenBuilder)(void);

static ScreenBuilder screen_builders[23] = {
  build_splash,         // 0x00 SCR_SPLASH
  build_course,         // 0x01 SCR_COURSE
  build_tee,            // 0x02 SCR_TEE
  build_hole,           // 0x03 SCR_HOLE
  build_menu,           // 0x04 SCR_MENU
  build_mode,           // 0x05 SCR_MODE
  build_assist,         // 0x06 SCR_ASSIST
  build_change_hole,    // 0x07 SCR_CHANGE_HOLE
  build_wifi,           // 0x08 SCR_WIFI
  build_debug,          // 0x09 SCR_DEBUG
  build_debug_system,   // 0x0A SCR_DEBUG_SYSTEM
  build_debug_gps,      // 0x0B SCR_DEBUG_GPS
  build_debug_lidar,    // 0x0C SCR_DEBUG_LIDAR
  build_debug_camera,   // 0x0D SCR_DEBUG_CAMERA
  build_debug_imu,      // 0x0E SCR_DEBUG_IMU
  build_debug_nav,      // 0x0F SCR_DEBUG_NAV
  build_energy,         // 0x10 SCR_ENERGY
  build_sensors,        // 0x11 SCR_SENSORS
  build_drive_dist,     // 0x12 SCR_DRIVE_DIST
  build_map,            // 0x13 SCR_MAP
  build_round_summary,  // 0x14 SCR_ROUND_SUMMARY
  build_speed,          // 0x15 SCR_SPEED
  build_quick_select,   // 0x16 SCR_QUICK_SELECT
};

// ---------------------------------------------------------------------------
// LVGL touch input (FT6336U via sensors.cpp).
// ---------------------------------------------------------------------------
static lv_indev_drv_t indev_drv;
static lv_indev_t *g_touch_indev;

// LVGL pointer read callback: polls the FT6336U and reports the touch point
// in display coordinates. This lets LVGL widgets receive taps directly.
static void touch_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
  (void)indev_drv;
  int x = 0, y = 0, gesture = 0;
  if (sensors_poll_touch(&x, &y, &gesture)) {
    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 40];  // 40-row partial buffer (RGB565)

void screens_init(void)
{
  tft.init();
  tft.setRotation(1);  // portrait 320x480

  // Backlight: PWM-driven for controllable brightness.
  pinMode(BACKLIGHT_PIN, OUTPUT);
  analogWrite(BACKLIGHT_PIN, 255);  // full brightness at boot

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 320 * 40);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 320;
  disp_drv.ver_res = 480;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.flush_cb = disp_flush;
  disp = lv_disp_drv_register(&disp_drv);

  // Register the FT6336U as LVGL's pointer input device so LVGL widgets can
  // receive taps directly. The read callback polls sensors_poll_touch().
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read_cb;
  g_touch_indev = lv_indev_drv_register(&indev_drv);
}

void screens_show(uint8_t screen_id)
{
  g_current_screen = screen_id;
  if (screen_id < 23 && screen_builders[screen_id] != NULL) {
    screen_builders[screen_id]();
  }
}

void screens_refresh(void)
{
  // Update the active screen's registered labels from g_state.
  for (int i = 0; i < g_label_count; ++i) {
    set_label_text(&g_labels[i]);
  }
}

int screens_handle_tap(int x, int y)
{
  // Map views (SCR_HOLE) return -1 so the Pi gets raw coordinates.
  if (g_current_screen == SCR_HOLE) {
    return -1;
  }
  return hit_test(x, y);
}

// Spinner animation state (splash screen).
static uint32_t g_spinner_last_ms = 0;
static int g_spinner_angle = 0;

void screens_tick(void)
{
  lv_timer_handler();

  // Animate the splash spinner (~15 fps) while the Pi is booting.
  if (g_current_screen == SCR_SPLASH) {
    uint32_t now = millis();
    if (now - g_spinner_last_ms >= 66) {
      g_spinner_last_ms = now;
      g_spinner_angle = (g_spinner_angle + 30) % 360;
      // Draw a rotating arc (a 270-degree wedge) centered at (160, 260).
      // drawArc takes raw RGB565 colors (not lv_color_t).
      tft.drawArc(160, 260, 20, 14, g_spinner_angle,
                  g_spinner_angle + 270, 0x38BDF8, 0x0F172A, true);
    }
  }
}

// ---------------------------------------------------------------------------
// Boot status (called from main.ino on_frame when a DL_BOOT_STATUS arrives).
// ---------------------------------------------------------------------------
void screens_set_boot_status(uint8_t progress, const char *text)
{
  g_boot_progress = progress;
  if (text != NULL) {
    snprintf(g_boot_text, sizeof(g_boot_text), "%s", text);
  }
  if (g_current_screen == SCR_SPLASH && splash_status_label != NULL) {
    lv_label_set_text(splash_status_label, g_boot_text);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u%%", g_boot_progress);
    lv_label_set_text(splash_progress_label, buf);
  }
}

// ---------------------------------------------------------------------------
// Backlight (called from main.ino on_frame when a ST_BACKLIGHT arrives).
// ---------------------------------------------------------------------------
void screens_set_backlight(uint8_t brightness)
{
  analogWrite(BACKLIGHT_PIN, brightness);
}

// ---------------------------------------------------------------------------
// Map bitmap (called from main.ino on_frame when a DL_MAP_FRAME arrives).
// Caches the packed RGB565 bitmap and blits it into the map area of the
// current screen (SCR_MAP or SCR_HOLE).
// ---------------------------------------------------------------------------
void screens_set_map_bitmap(const uint8_t *data, size_t len,
                            uint16_t map_w, uint16_t map_h)
{
  if (data == NULL || len < (size_t)map_w * map_h * 2) {
    return;
  }
  if (map_w > MAP_CACHE_MAX_W || map_h > MAP_CACHE_MAX_H) {
    return;  // too large to cache; ignore
  }
  // Cache the bitmap so it can be re-rendered when the screen changes.
  memcpy(g_map_cache, data, (size_t)map_w * map_h * 2);
  g_map_w = map_w;
  g_map_h = map_h;
  // Render now if a map-bearing screen is visible.
  if (g_current_screen == SCR_MAP || g_current_screen == SCR_HOLE) {
    render_map_bitmap();
  }
}

// Blit the cached map bitmap into the current screen's map area.
static void render_map_bitmap(void)
{
  if (g_map_w == 0 || g_map_h == 0) {
    return;
  }
  // Map area differs per screen: SCR_MAP at (12,40) 296x380; SCR_HOLE at
  // (12,80) 296x372. Center the bitmap in the active screen's area.
  int area_x = 12;
  int area_y = (g_current_screen == SCR_HOLE) ? 80 : 40;
  int area_w = 296;
  int area_h = (g_current_screen == SCR_HOLE) ? 372 : 380;
  int x0 = area_x + (area_w - (int)g_map_w) / 2;
  int y0 = area_y + (area_h - (int)g_map_h) / 2;
  if (x0 < area_x) x0 = area_x;
  if (y0 < area_y) y0 = area_y;
  for (uint16_t y = 0; y < g_map_h; ++y) {
    for (uint16_t x = 0; x < g_map_w; ++x) {
      size_t i = ((size_t)y * g_map_w + x) * 2;
      uint16_t rgb565 = (uint16_t)(g_map_cache[i] | (g_map_cache[i + 1] << 8));
      tft.drawPixel(x0 + (int)x, y0 + (int)y, rgb565);
    }
  }
}

// ---------------------------------------------------------------------------
// WiFi config (called from main.ino on_frame when a DL_CONFIG arrives).
// Stores the SSID/password and re-renders the WiFi screen + QR code.
// ---------------------------------------------------------------------------
void screens_set_wifi_config(uint8_t config_id, const char *text)
{
  if (text == NULL) {
    return;
  }
  if (config_id == CFG_WIFI_SSID) {
    snprintf(g_wifi_ssid, sizeof(g_wifi_ssid), "%s", text);
  } else if (config_id == CFG_WIFI_PASS) {
    snprintf(g_wifi_pass, sizeof(g_wifi_pass), "%s", text);
  } else {
    return;
  }
  if (g_current_screen == SCR_WIFI) {
    build_wifi();
  }
}

// Render a scannable QR code encoding the WiFi credentials into the given
// area. Uses the ricmoo/QRCode library (pure C, stack-based).
static void render_wifi_qr(int x0, int y0, int size)
{
  // WIFI:S:<ssid>;P:<pass>;; (per docs/hmi-spec.md §6.9).
  char payload[160];
  snprintf(payload, sizeof(payload), "WIFI:S:%s;P:%s;;",
           g_wifi_ssid, g_wifi_pass);

  QRCode qrcode;
  uint8_t qrcode_data[qrcode_getBufferSize(4)];
  if (qrcode_initText(&qrcode, qrcode_data, 4, ECC_MEDIUM, payload) != 0) {
    return;
  }
  // Scale each module to fit the requested size.
  int module_px = size / qrcode.size;
  if (module_px < 1) module_px = 1;
  int qr_px = qrcode.size * module_px;
  int ox = x0 + (size - qr_px) / 2;
  int oy = y0 + (size - qr_px) / 2;
  for (uint8_t y = 0; y < qrcode.size; ++y) {
    for (uint8_t x = 0; x < qrcode.size; ++x) {
      if (qrcode_getModule(&qrcode, x, y)) {
        tft.fillRect(ox + (int)x * module_px, oy + (int)y * module_px,
                     module_px, module_px, TFT_BLACK);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Alert banner (called from main.ino on_frame when a ST_ALERT arrives).
// Draws a colored banner across the top of the current screen; alert 0 clears.
// ---------------------------------------------------------------------------
static lv_obj_t *g_alert_banner = NULL;

void screens_set_alert(uint8_t alert)
{
  g_state.alert = alert;
  if (g_alert_banner != NULL) {
    lv_obj_del(g_alert_banner);
    g_alert_banner = NULL;
  }
  if (alert == ALERT_NONE) {
    return;
  }
  const char *text = "ALERT";
  lv_color_t color = C_WARN;
  switch (alert) {
    case ALERT_BATTERY_LOW:      text = "BATTERY LOW"; color = C_WARN; break;
    case ALERT_BATTERY_CRITICAL: text = "BATTERY CRITICAL"; color = C_DANGER; break;
    case ALERT_GEOFENCE_NEAR:    text = "GEOFENCE NEAR"; color = C_WARN; break;
    case ALERT_GEOFENCE_CROSSED: text = "GEOFENCE CROSSED"; color = C_DANGER; break;
    case ALERT_OBSTACLE:         text = "OBSTACLE"; color = C_DANGER; break;
    case ALERT_SLIP:             text = "WHEEL SLIP"; color = C_WARN; break;
    case ALERT_RANGE_CAUTION:    text = "RANGE CAUTION"; color = C_WARN; break;
    case ALERT_RANGE_CRITICAL:   text = "RANGE CRITICAL"; color = C_DANGER; break;
    case ALERT_NAV_ERROR:        text = "NAV ERROR"; color = C_DANGER; break;
    default: break;
  }
  g_alert_banner = lv_label_create(lv_scr_act());
  lv_obj_set_pos(g_alert_banner, 0, 0);
  lv_obj_set_size(g_alert_banner, 320, 24);
  lv_obj_set_style_bg_color(g_alert_banner, color, 0);
  lv_obj_set_style_text_color(g_alert_banner, C_BG, 0);
  lv_obj_set_style_text_font(g_alert_banner, &lv_font_montserrat_16, 0);
  lv_label_set_text(g_alert_banner, text);
  lv_obj_align(g_alert_banner, LV_ALIGN_TOP_MID, 0, 0);
}

// ---------------------------------------------------------------------------
// State setter (called from main.ino on_frame when a STATE_UPDATE arrives).
// ---------------------------------------------------------------------------
void screens_set_state(uint8_t id, int32_t value)
{
  switch (id) {
    case ST_BATTERY_PCT: g_state.battery_pct = (int16_t)value; break;
    case ST_SPEED_MPS: g_state.speed_mps = (int16_t)value; break;
    case ST_SAFETY_STATE: g_state.safety_state = (uint8_t)value; break;
    case ST_MODE: g_state.mode = (uint8_t)value; break;
    case ST_GPS_LAT: g_state.gps_lat = (int32_t)value; break;
    case ST_GPS_LON: g_state.gps_lon = (int32_t)value; break;
    case ST_GPS_SPEED: g_state.gps_speed = (uint16_t)value; break;
    case ST_GPS_SATS: g_state.gps_sats = (uint8_t)value; break;
    case ST_IMU_ROLL: g_state.imu_roll = (int16_t)value; break;
    case ST_IMU_PITCH: g_state.imu_pitch = (int16_t)value; break;
    case ST_OBSTACLE: g_state.obstacle = (uint8_t)value; break;
    case ST_OBSTACLE_NEAREST_M: g_state.obstacle_nearest = (uint16_t)value; break;
    case ST_GEOFENCE: g_state.geofence = (uint8_t)value; break;
    case ST_SPEED_ZONE_LIMIT: g_state.speed_zone_limit = (int16_t)value; break;
    case ST_SLOPE_DEG: g_state.slope_deg = (int16_t)value; break;
    case ST_NAV_STATUS: g_state.nav_status = (uint8_t)value; break;
    case ST_HOLE_NUMBER: g_state.hole_number = (uint8_t)value; break;
    case ST_HOLE_DISTANCE_M: g_state.hole_distance = (uint16_t)value; break;
    case ST_HOLE_REMAINING_M: g_state.hole_remaining = (uint16_t)value; break;
    case ST_PUSH_FORCE_N: g_state.push_force = (int16_t)value; break;
    case ST_ASSIST_LEVEL: g_state.assist_level = (uint8_t)value; break;
    case ST_ASSIST_ENABLED: g_state.assist_enabled = (uint8_t)value; break;
    case ST_HILL_ASSIST_ENABLED: g_state.hill_assist_enabled = (uint8_t)value; break;
    case ST_STEERING_ASSIST: g_state.steering_assist_enabled = (uint8_t)value; break;
    case ST_TIME_HHMM: g_state.time_hhmm = (uint16_t)value; break;
    case ST_RANGE_M: g_state.range_m = (uint16_t)value; break;
    case ST_RETURN_M: g_state.return_m = (uint16_t)value; break;
    case ST_RANGE_STATE: g_state.range_state = (uint8_t)value; break;
    case ST_SLIP: g_state.slip = (uint8_t)value; break;
    case ST_CAPABILITY: g_state.capability = (uint16_t)value; break;
    case ST_SEGMENTATION: g_state.segmentation = (uint8_t)value; break;
    case ST_HOLES_REMAINING: g_state.holes_remaining = (int16_t)value; break;
    case ST_MAP_X: g_state.map_x = (uint16_t)value; break;
    case ST_MAP_Y: g_state.map_y = (uint16_t)value; break;
    case ST_MAP_HEADING: g_state.map_heading = (int16_t)value; break;
    case ST_MAP_AVAILABLE: g_state.map_available = (uint8_t)value; break;
    case ST_ALERT: screens_set_alert((uint8_t)value); break;
    case ST_ROUND_ACTIVE: g_state.round_active = (uint8_t)value; break;
    case ST_ROUND_DISTANCE: g_state.round_distance = (uint16_t)value; break;
    case ST_ROUND_ENERGY: g_state.round_energy = (uint16_t)value; break;
    case ST_ROUND_DURATION: g_state.round_duration = (uint16_t)value; break;
    case ST_ROUND_AVG_SPEED: g_state.round_avg_speed = (uint16_t)value; break;
    case ST_SPEED_LIMIT: g_state.speed_limit = (uint16_t)value; break;
    default: break;
  }
  screens_refresh();
}