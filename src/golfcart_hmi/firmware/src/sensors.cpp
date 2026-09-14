// Handle-unit sensor drivers (ESP32 side).
// See sensors.h for the public API.

#include "sensors.h"

#include <Wire.h>
#include <HX711.h>

// ---------------------------------------------------------------------------
// FT6336U capacitive touch (I2C).
// The FT6336U is a FocalTech controller. It exposes touch points over I2C.
// Register map is controller-specific; these are the common FT6x36 registers.
// ---------------------------------------------------------------------------
#define FT6336_I2C_ADDR 0x38
#define FT6336_REG_STATUS 0x02
#define FT6336_REG_TOUCH1_XH 0x03
#define FT6336_REG_TOUCH1_XL 0x04
#define FT6336_REG_TOUCH1_YH 0x05
#define FT6336_REG_TOUCH1_YL 0x06

// HX711 load cell pins (ESP32-S3).
#define HX711_DOUT_PIN 7
#define HX711_SCK_PIN 8

static HX711 g_scale;
static bool g_touch_ok = false;
static bool g_scale_ok = false;

bool sensors_init(void)
{
  // Touch.
  Wire.begin();
  Wire.beginTransmission(FT6336_I2C_ADDR);
  g_touch_ok = (Wire.endTransmission() == 0);

  // Load cell.
  g_scale.begin(HX711_DOUT_PIN, HX711_SCK_PIN);
  g_scale_ok = g_scale.wait_ready_timeout(1000);

  return g_touch_ok || g_scale_ok;
}

bool sensors_poll_touch(int *x, int *y, int *gesture)
{
  if (!g_touch_ok) {
    return false;
  }
  // Read the status register; bit 0 = touch present.
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(FT6336_REG_STATUS);
  Wire.endTransmission();
  Wire.requestFrom((int)FT6336_I2C_ADDR, 1);
  if (Wire.available() < 1) {
    return false;
  }
  uint8_t status = (uint8_t)Wire.read();
  if ((status & 0x01) == 0) {
    return false;  // no touch
  }

  // Read touch point 1 (x: 12-bit, y: 12-bit).
  Wire.beginTransmission(FT6336_I2C_ADDR);
  Wire.write(FT6336_REG_TOUCH1_XH);
  Wire.endTransmission();
  Wire.requestFrom((int)FT6336_I2C_ADDR, 4);
  if (Wire.available() < 4) {
    return false;
  }
  uint8_t xh = (uint8_t)Wire.read();
  uint8_t xl = (uint8_t)Wire.read();
  uint8_t yh = (uint8_t)Wire.read();
  uint8_t yl = (uint8_t)Wire.read();

  int tx = ((int)(xh & 0x0F) << 8) | xl;
  int ty = ((int)(yh & 0x0F) << 8) | yl;

  // Map the panel's native resolution to the 320x480 display. The FT6336U on
  // this panel reports 0..1023 in each axis; scale to 320x480.
  *x = tx * 320 / 1024;
  *y = ty * 480 / 1024;
  *gesture = 0;  // tap
  return true;
}

int32_t sensors_read_force(void)
{
  if (!g_scale_ok) {
    return 0;
  }
  // Raw ADC counts (uncalibrated). The Pi applies zero offset + gain.
  return (int32_t)g_scale.get_units(1);
}