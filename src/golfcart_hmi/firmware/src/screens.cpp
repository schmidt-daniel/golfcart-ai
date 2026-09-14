// LVGL screen implementations for the handle-unit HMI.
// See screens.h for the public API.

#include "screens.h"
#include "handle_protocol.h"

// ---------------------------------------------------------------------------
// LVGL includes (TFT_eSPI backend for the ST7796).
// ---------------------------------------------------------------------------
#include <lvgl.h>
#include <TFT_eSPI.h>

// ---------------------------------------------------------------------------
// Display + LVGL setup
// ---------------------------------------------------------------------------
static TFT_eSPI tft = TFT_eSPI();
static lv_disp_drv_t disp_drv;
static lv_disp_t *disp;

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
  uint16_t time_hhmm;
} HandleState;

static HandleState g_state;
static uint8_t g_current_screen = SCR_COURSE;

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
#define MAX_LABELS 16
typedef struct {
  lv_obj_t *obj;
  uint8_t kind;   // which state value to render (see LABEL_*)
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

static LabelRef g_labels[MAX_LABELS];
static int g_label_count = 0;

static void clear_labels(void)
{
  g_label_count = 0;
}

static void add_label(lv_obj_t *obj, uint8_t kind)
{
  if (g_label_count < MAX_LABELS) {
    g_labels[g_label_count].obj = obj;
    g_labels[g_label_count].kind = kind;
    ++g_label_count;
  }
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
  const char *items[] = {"MAP", "MODE", "ASSIST", "CHANGE HOLE",
                         "SELECT COURSE", "WIFI", "DEBUG", "SHUTDOWN"};
  int y = 40;
  for (int i = 0; i < 8; ++i) {
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
static void build_hole(void)
{
  scr = make_screen();
  make_header("Hole");
  // Distance + remaining.
  lv_obj_t *dist = make_label(scr, "Dist: -- m", 12, 52, 140, 24, C_TEXT);
  add_label(dist, LABEL_HOLE_DIST);
  lv_obj_t *rem = make_label(scr, "Rem: -- m", 168, 52, 140, 24, C_TEXT);
  add_label(rem, LABEL_HOLE_REM);
  // Map area (placeholder; the Pi sends the hole layout via DEBUG_SUMMARY).
  lv_obj_t *map = lv_obj_create(scr);
  lv_obj_set_pos(map, 12, 80);
  lv_obj_set_size(map, 296, 372);
  lv_obj_set_style_bg_color(map, C_SURFACE, 0);
  lv_obj_set_style_border_width(map, 2, 0);
  lv_obj_set_style_border_color(map, C_TEXT, 0);
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
  make_label(scr, "OFF", 220, 140, 80, 24, C_TEXT_DIM);
  add_hit(20, 140, 300, 164, 2);
  make_button(scr, "Main Menu", 20, 188, 280, 40, C_SURFACE2);
  add_hit(20, 188, 300, 228, 3);
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
  make_label(scr, "SSID: golfcart-xxxx", 20, 44, 280, 24, C_TEXT);
  make_label(scr, "Pass: 12345678", 20, 76, 280, 24, C_TEXT);
  // QR placeholder.
  lv_obj_t *qr = lv_obj_create(scr);
  lv_obj_set_pos(qr, 110, 120);
  lv_obj_set_size(qr, 100, 100);
  lv_obj_set_style_bg_color(qr, C_SURFACE2, 0);
  make_button(scr, "Main Menu", 20, 240, 280, 40, C_SURFACE2);
  add_hit(20, 240, 300, 280, 0);
}

// Debug menu (SCR_DEBUG).
static void build_debug(void)
{
  scr = make_screen();
  make_header("Debug");
  const char *items[] = {"System", "GPS", "LiDAR", "Camera", "IMU", "Navigation"};
  int y = 40;
  for (int i = 0; i < 6; ++i) {
    make_button(scr, items[i], 20, y, 280, 40, C_SURFACE2);
    add_hit(20, y, 300, y + 40, i);
    y += 48;
  }
  make_button(scr, "Main Menu", 20, y, 280, 40, C_SURFACE2);
  add_hit(20, y, 300, y + 40, 6);
}

// System debug (SCR_DEBUG_SYSTEM).
static void build_debug_system(void)
{
  scr = make_screen();
  make_header("System");
  lv_obj_t *bat = make_label(scr, "Battery  --%", 20, 44, 280, 24, C_TEXT);
  add_label(bat, LABEL_BATTERY_PCT);
  make_label(scr, "CPU      --%", 20, 76, 280, 24, C_TEXT);
  make_label(scr, "Disk     --%", 20, 108, 280, 24, C_TEXT);
  make_label(scr, "Uptime   --:--", 20, 140, 280, 24, C_TEXT);
  make_label(scr, "Temp     -- C", 20, 172, 280, 24, C_TEXT);
  make_button(scr, "Main Menu", 20, 220, 280, 40, C_SURFACE2);
  add_hit(20, 220, 300, 260, 0);
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
  make_label(scr, "Segmentation: OFF", 20, 260, 280, 24, C_TEXT_DIM);
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

static ScreenBuilder screen_builders[16] = {
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
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[320 * 40];  // 40-row partial buffer (RGB565)

void screens_init(void)
{
  tft.init();
  tft.setRotation(1);  // portrait 320x480

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, NULL, 320 * 40);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 320;
  disp_drv.ver_res = 480;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.flush_cb = disp_flush;
  disp = lv_disp_drv_register(&disp_drv);

  // TODO: register FT6336U touch driver with lv_indev.
}

void screens_show(uint8_t screen_id)
{
  g_current_screen = screen_id;
  if (screen_id < 16 && screen_builders[screen_id] != NULL) {
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
    case ST_TIME_HHMM: g_state.time_hhmm = (uint16_t)value; break;
    default: break;
  }
  screens_refresh();
}