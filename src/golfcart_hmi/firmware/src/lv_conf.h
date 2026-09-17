/**
 * LVGL configuration for the handle-unit HMI.
 *
 * This file is picked up by LVGL via LV_CONF_INCLUDE_SIMPLE (set in
 * platformio.ini build_flags). It configures the ST7796 display through the
 * TFT_eSPI driver and enables the fonts used by screens.c.
 *
 * Display: Elecrow 3.5" IPS SPI LCD, 320x480 portrait, RGB565 (16-bit).
 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* Color depth: 16-bit RGB565 (ST7796). */
#define LV_COLOR_DEPTH 16

/* Use the TFT_eSPI display driver (ST7796 via SPI). */
#define LV_USE_TFT_ESPI 1

/* Fonts used by screens.c. */
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#define LV_FONT_MONTSERRAT_16 1

/* Touch input is handled by the FT6336U driver in sensors.cpp and registered
 * as LVGL's pointer input device in screens_init (see touch_read_cb). */

/* Memory: the ESP32-S3 has 320 KB RAM; give LVGL a generous heap. */
#define LV_MEM_SIZE (48 * 1024)

/* DPI for the 3.5" 320x480 panel (~165 DPI). */
#define LV_DPI_DEF 165

#endif /* LV_CONF_H */