// LVGL screen definitions for the handle-unit HMI.
//
// Implements the full screen set from docs/hmi-spec.md on the 320x480 ST7796
// display. Screens are built with LVGL widgets; the Pi drives navigation via
// SCREEN_NAV and state via STATE_UPDATE (see handle_protocol.h).
//
// The ESP32 maps taps to menu items locally and sends MENU_SELECT back to the
// Pi (see handle_protocol.h UL_MENU_SELECT).

#ifndef HANDLE_SCREENS_H
#define HANDLE_SCREENS_H

#include <stdint.h>
#include <stddef.h>

// Screen IDs (must match handle_protocol.h SCREEN_*).
#define SCR_SPLASH       0x00
#define SCR_COURSE       0x01
#define SCR_TEE          0x02
#define SCR_HOLE         0x03
#define SCR_MENU         0x04
#define SCR_MODE         0x05
#define SCR_ASSIST       0x06
#define SCR_CHANGE_HOLE  0x07
#define SCR_WIFI         0x08
#define SCR_DEBUG        0x09
#define SCR_DEBUG_SYSTEM 0x0A
#define SCR_DEBUG_GPS    0x0B
#define SCR_DEBUG_LIDAR  0x0C
#define SCR_DEBUG_CAMERA 0x0D
#define SCR_DEBUG_IMU    0x0E
#define SCR_DEBUG_NAV    0x0F
#define SCR_ENERGY       0x10
#define SCR_SENSORS      0x11
#define SCR_DRIVE_DIST   0x12
#define SCR_MAP          0x13
#define SCR_ROUND_SUMMARY 0x14
#define SCR_SPEED        0x15
#define SCR_QUICK_SELECT 0x16

// Initialize the LVGL screen system (called once from setup()).
void screens_init(void);

// Show the given screen (by ID). Rebuilds the LVGL widget tree.
void screens_show(uint8_t screen_id);

// Refresh the active screen with the latest cached state values.
void screens_refresh(void);

// Update a cached state value (from a STATE_UPDATE frame). id is a ST_* id
// from handle_protocol.h; value is the decoded payload value.
void screens_set_state(uint8_t id, int32_t value);

// Set the map bitmap (from a DL_MAP_FRAME frame). data is a packed RGB565
// bitmap of map_w x map_h pixels (little-endian). The Pi downsamples the
// course map to this size; the ESP32 blits it into the map area.
void screens_set_map_bitmap(const uint8_t *data, size_t len,
                            uint16_t map_w, uint16_t map_h);

// Set the active alert (from a ST_ALERT state value). Draws a banner overlay
// on the current screen; alert 0 clears it.
void screens_set_alert(uint8_t alert);

// Set a WiFi credential (from a DL_CONFIG frame). config_id is a CFG_* id
// from handle_protocol.h; text is the SSID or password. The WiFi screen
// renders these and a scannable QR code encoding WIFI:S:<ssid>;P:<pass>;;.
void screens_set_wifi_config(uint8_t config_id, const char *text);

// Update the boot-status line shown on the splash screen (from a
// DL_BOOT_STATUS frame). progress is 0-100; text is a short status string.
void screens_set_boot_status(uint8_t progress, const char *text);

// Set the display backlight brightness (0-255). 0 = off, 255 = full.
// The backlight is a dedicated pin on the Elecrow 14-pin header (pin 8, LED).
void screens_set_backlight(uint8_t brightness);

// Handle a tap at (x, y) in display coordinates. Returns the menu item id to
// send to the Pi, or -1 if the tap was not on a menu item (e.g. a map tap).
int screens_handle_tap(int x, int y);

// LVGL tick (call from loop()).
void screens_tick(void);

#endif // HANDLE_SCREENS_H