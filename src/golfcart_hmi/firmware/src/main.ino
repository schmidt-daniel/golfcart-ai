// Handle-unit firmware for the ESP32-S3 (with PSRAM).
//
// Renders the HMI on the Elecrow 3.5" ST7796 (320x480) via LVGL, reads the
// FT6336U capacitive touch (I2C), the analog joystick (ADC), and the HX711
// load cell, and talks to the Raspberry Pi over USB serial using the protocol
// in handle_protocol.{h,c}.
//
// The ESP32 is the HMI + physical sensor interface. It renders what the Pi
// sends and reports input back. All control logic lives on the Pi.
//
// NOTE: This is the firmware skeleton. It compiles standalone (the protocol
// layer is complete and testable); the LVGL screen definitions and the
// FT6336U/HX711 drivers are stubbed and filled in as the hardware is wired.

#include <Arduino.h>
#include <HardwareSerial.h>

#include "handle_protocol.h"
#include "screens.h"
#include "sensors.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define SERIAL_BAUD 460800UL

// Joystick ADC pins (ESP32-S3 analog inputs).
#define JOY_X_PIN 4
#define JOY_Y_PIN 5
#define JOY_BTN_PIN 6

// HX711 load cell pins.
#define HX711_DOUT_PIN 7
#define HX711_SCK_PIN 8

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static HandleDecoder g_decoder;
static uint8_t g_seq = 0;
static uint8_t g_screen = SCREEN_COURSE;

// ---------------------------------------------------------------------------
// Serial send helpers
// ---------------------------------------------------------------------------
static void send_frame(uint8_t type, const uint8_t *payload, size_t len)
{
  uint8_t frame[512];
  size_t n = handle_encode(type, payload, len, g_seq++, frame);
  Serial.write(frame, n);
}

static void send_hello(void)
{
  uint8_t payload[2] = { PROTOCOL_VERSION, CAP_JOYSTICK | CAP_TOUCH | CAP_FORCE | CAP_DISPLAY };
  send_frame(UL_HELLO, payload, 2);
}

static void send_joystick(int16_t x, int16_t y, uint8_t btn)
{
  uint8_t payload[5];
  payload[0] = (uint8_t)(x & 0xFF);
  payload[1] = (uint8_t)((x >> 8) & 0xFF);
  payload[2] = (uint8_t)(y & 0xFF);
  payload[3] = (uint8_t)((y >> 8) & 0xFF);
  payload[4] = btn;
  send_frame(UL_JOYSTICK, payload, 5);
}

static void send_force(int16_t force)
{
  uint8_t payload[2];
  payload[0] = (uint8_t)(force & 0xFF);
  payload[1] = (uint8_t)((force >> 8) & 0xFF);
  send_frame(UL_FORCE, payload, 2);
}

static void send_menu_select(uint8_t item)
{
  uint8_t payload[1] = { item };
  send_frame(UL_MENU_SELECT, payload, 1);
}

// ---------------------------------------------------------------------------
// Downlink handling
// ---------------------------------------------------------------------------
static void on_frame(uint8_t type, const uint8_t *payload, size_t len, uint8_t seq)
{
  switch (type) {
    case DL_HELLO:
      // Pi acknowledged; nothing more to do.
      break;
    case DL_SCREEN_NAV:
      if (len >= 1) {
        g_screen = payload[0];
        screens_show(g_screen);
      }
      break;
    case DL_STATE_UPDATE:
      if (len >= 2) {
        uint8_t id = payload[0];
        // Values are little-endian; widths per the spec.
        switch (id) {
          case ST_BATTERY_PCT: screens_set_state(id, payload[1]); break;
          case ST_SPEED_MPS: screens_set_state(id, (int16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_SAFETY_STATE: screens_set_state(id, payload[1]); break;
          case ST_MODE: screens_set_state(id, payload[1]); break;
          case ST_GPS_LAT: screens_set_state(id, (int32_t)(payload[1] | (payload[2] << 8) | (payload[3] << 16) | (payload[4] << 24))); break;
          case ST_GPS_LON: screens_set_state(id, (int32_t)(payload[1] | (payload[2] << 8) | (payload[3] << 16) | (payload[4] << 24))); break;
          case ST_GPS_SPEED: screens_set_state(id, (uint16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_GPS_SATS: screens_set_state(id, payload[1]); break;
          case ST_IMU_ROLL: screens_set_state(id, (int16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_IMU_PITCH: screens_set_state(id, (int16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_OBSTACLE: screens_set_state(id, payload[1]); break;
          case ST_OBSTACLE_NEAREST_M: screens_set_state(id, (uint16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_GEOFENCE: screens_set_state(id, payload[1]); break;
          case ST_SPEED_ZONE_LIMIT: screens_set_state(id, (int16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_SLOPE_DEG: screens_set_state(id, (int16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_NAV_STATUS: screens_set_state(id, payload[1]); break;
          case ST_HOLE_NUMBER: screens_set_state(id, payload[1]); break;
          case ST_HOLE_DISTANCE_M: screens_set_state(id, (uint16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_HOLE_REMAINING_M: screens_set_state(id, (uint16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_PUSH_FORCE_N: screens_set_state(id, (int16_t)(payload[1] | (payload[2] << 8))); break;
          case ST_ASSIST_LEVEL: screens_set_state(id, payload[1]); break;
          case ST_ASSIST_ENABLED: screens_set_state(id, payload[1]); break;
          case ST_HILL_ASSIST_ENABLED: screens_set_state(id, payload[1]); break;
          case ST_TIME_HHMM: screens_set_state(id, (uint16_t)(payload[1] | (payload[2] << 8))); break;
          default: break;  // unknown id: ignore
        }
      }
      break;
    case DL_DEBUG_SUMMARY:
      // TODO: render debug summary.
      break;
    case DL_CONFIG:
      // TODO: apply config (e.g. assist level).
      break;
    case DL_ACK:
      // Heartbeat from the Pi.
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// Input sampling
// ---------------------------------------------------------------------------
static void sample_joystick(void)
{
  // Read the analog joystick (ADC) and send at 50 Hz.
  // Map the ADC range (0..4095 on ESP32-S3) to -32768..32767.
  int raw_x = analogRead(JOY_X_PIN);
  int raw_y = analogRead(JOY_Y_PIN);
  int16_t x = (int16_t)((raw_x - 2048) * 16);  // center 2048 -> 0
  int16_t y = (int16_t)((raw_y - 2048) * 16);
  uint8_t btn = (digitalRead(JOY_BTN_PIN) == LOW) ? 1 : 0;
  send_joystick(x, y, btn);
}

static void sample_force(void)
{
  // Read the HX711 and send raw force at 50-100 Hz.
  int32_t raw = sensors_read_force();
  send_force((int16_t)raw);
}

static void poll_touch(void)
{
  int x, y, gesture;
  if (!sensors_poll_touch(&x, &y, &gesture)) {
    return;
  }
  // Resolve the tap to a menu item locally; for map views send raw touch.
  int item = screens_handle_tap(x, y);
  if (item >= 0) {
    send_menu_select((uint8_t)item);
  } else {
    // Raw touch frame for map views.
    uint8_t payload[5];
    payload[0] = (uint8_t)(x & 0xFF);
    payload[1] = (uint8_t)((x >> 8) & 0xFF);
    payload[2] = (uint8_t)(y & 0xFF);
    payload[3] = (uint8_t)((y >> 8) & 0xFF);
    payload[4] = (uint8_t)gesture;
    send_frame(UL_TOUCH, payload, 5);
  }
}

// ---------------------------------------------------------------------------
// Setup / loop
// ---------------------------------------------------------------------------
void setup()
{
  Serial.begin(SERIAL_BAUD);
  pinMode(JOY_BTN_PIN, INPUT_PULLUP);
  screens_init();
  sensors_init();

  send_hello();
}

void loop()
{
  // Read any downlink bytes from the Pi.
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    handle_decoder_feed(&g_decoder, &b, 1, on_frame);
  }

  sample_joystick();
  sample_force();
  poll_touch();

  screens_tick();
  delay(10);  // ~100 Hz loop
}