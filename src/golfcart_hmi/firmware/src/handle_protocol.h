// Handle-unit serial protocol (ESP32 side).
// Mirrors golfcart_hmi/protocol.py — see docs/handle-protocol.md.
//
// Frame format:
//   +--------+--------+--------+--------+--------+------------------+--------+
//   | 0xAA   | 0x55   | TYPE   | LEN    | SEQ    | PAYLOAD[LEN]     | CRC16  |
//   +--------+--------+--------+--------+--------+------------------+--------+
//
// Byte stuffing: 0xAA / 0x55 / 0xDB in the payload are escaped with
// 0xDB + (byte ^ 0x20).

#ifndef HANDLE_PROTOCOL_H
#define HANDLE_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROTOCOL_VERSION 1

// Start-of-frame markers.
#define SOF1 0xAA
#define SOF2 0x55
#define ESC  0xDB

// Downlink (Pi -> ESP32).
#define DL_HELLO         0x01
#define DL_SCREEN_NAV    0x02
#define DL_STATE_UPDATE  0x03
#define DL_DEBUG_SUMMARY 0x04
#define DL_CONFIG        0x05
#define DL_ACK           0x06
#define DL_BOOT_STATUS   0x07
#define DL_MAP_FRAME     0x08

// Uplink (ESP32 -> Pi).
#define UL_HELLO       0x81
#define UL_JOYSTICK    0x82
#define UL_TOUCH       0x83
#define UL_MENU_SELECT 0x84
#define UL_FORCE       0x85
#define UL_ACK         0x86

// State value IDs.
#define ST_BATTERY_PCT        0x01
#define ST_SPEED_MPS          0x02
#define ST_SAFETY_STATE       0x03
#define ST_MODE               0x04
#define ST_GPS_LAT            0x05
#define ST_GPS_LON            0x06
#define ST_GPS_SPEED          0x07
#define ST_GPS_SATS           0x08
#define ST_IMU_ROLL           0x09
#define ST_IMU_PITCH          0x0A
#define ST_OBSTACLE           0x0B
#define ST_OBSTACLE_NEAREST_M 0x0C
#define ST_GEOFENCE           0x0D
#define ST_SPEED_ZONE_LIMIT   0x0E
#define ST_SLOPE_DEG          0x0F
#define ST_NAV_STATUS         0x10
#define ST_HOLE_NUMBER        0x11
#define ST_HOLE_DISTANCE_M    0x12
#define ST_HOLE_REMAINING_M   0x13
#define ST_PUSH_FORCE_N       0x14
#define ST_ASSIST_LEVEL       0x15
#define ST_ASSIST_ENABLED     0x16
#define ST_HILL_ASSIST_ENABLED 0x17
#define ST_TIME_HHMM          0x18
#define ST_BACKLIGHT          0x19
#define ST_STEERING_ASSIST    0x1A

// Energy dashboard state values (0x40-0x7F reserved).
#define ST_RANGE_M            0x40
#define ST_RETURN_M           0x41
#define ST_RANGE_STATE        0x42
#define ST_SLIP               0x43
#define ST_CAPABILITY         0x44
#define ST_SEGMENTATION       0x45
#define ST_HOLES_REMAINING    0x46
#define ST_MAP_X              0x47
#define ST_MAP_Y              0x48
#define ST_MAP_HEADING        0x49
#define ST_MAP_AVAILABLE      0x4A
#define ST_ALERT              0x4B
#define ST_ROUND_ACTIVE       0x4C
#define ST_ROUND_DISTANCE     0x4D
#define ST_ROUND_ENERGY       0x4E
#define ST_ROUND_DURATION     0x4F
#define ST_ROUND_AVG_SPEED    0x50
#define ST_SPEED_LIMIT        0x51

// Alert codes (ST_ALERT values).
#define ALERT_NONE            0
#define ALERT_BATTERY_LOW     1
#define ALERT_BATTERY_CRITICAL 2
#define ALERT_GEOFENCE_NEAR   3
#define ALERT_GEOFENCE_CROSSED 4
#define ALERT_OBSTACLE        5
#define ALERT_SLIP            6
#define ALERT_RANGE_CAUTION   7
#define ALERT_RANGE_CRITICAL  8
#define ALERT_NAV_ERROR       9
#define ST_HOLES_REMAINING    0x46

// Capabilities.
#define CAP_JOYSTICK 0x01
#define CAP_TOUCH    0x02
#define CAP_FORCE    0x04
#define CAP_DISPLAY  0x08

// Config IDs (DL_CONFIG payload: config id + value).
#define CFG_WIFI_SSID 0x01  // WiFi hotspot SSID (string)
#define CFG_WIFI_PASS 0x02  // WiFi hotspot password (string)

// Screen IDs.
#define SCREEN_SPLASH       0x00
#define SCREEN_COURSE       0x01
#define SCREEN_TEE          0x02
#define SCREEN_HOLE         0x03
#define SCREEN_MENU         0x04
#define SCREEN_MODE         0x05
#define SCREEN_ASSIST       0x06
#define SCREEN_CHANGE_HOLE  0x07
#define SCREEN_WIFI         0x08
#define SCREEN_DEBUG        0x09
#define SCREEN_DEBUG_SYSTEM 0x0A
#define SCREEN_DEBUG_GPS    0x0B
#define SCREEN_DEBUG_LIDAR  0x0C
#define SCREEN_DEBUG_CAMERA 0x0D
#define SCREEN_DEBUG_IMU    0x0E
#define SCREEN_DEBUG_NAV    0x0F
#define SCREEN_ENERGY       0x10
#define SCREEN_SENSORS      0x11
#define SCREEN_DRIVE_DIST   0x12
#define SCREEN_MAP          0x13
#define SCREEN_ROUND_SUMMARY 0x14
#define SCREEN_SPEED        0x15
#define SCREEN_QUICK_SELECT 0x16

// Max payload length (fits in a uint8 LEN field).
#define MAX_PAYLOAD 255

// CRC-16/CCITT (poly 0x1021, init 0xFFFF).
uint16_t handle_crc16(const uint8_t *data, size_t len);

// Encode a frame into out (must be >= len + 9). Returns total bytes written.
size_t handle_encode(uint8_t msg_type, const uint8_t *payload, size_t len,
                     uint8_t seq, uint8_t *out);

// Incremental decoder state.
typedef struct {
  uint8_t buf[512];
  size_t len;
} HandleDecoder;

// Feed bytes; on a complete frame, calls on_frame(type, payload, len, seq).
// Returns the number of frames decoded.
int handle_decoder_feed(HandleDecoder *dec, const uint8_t *data, size_t n,
                        void (*on_frame)(uint8_t type, const uint8_t *payload,
                                         size_t len, uint8_t seq));

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif // HANDLE_PROTOCOL_H