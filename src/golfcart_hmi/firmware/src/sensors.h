// Handle-unit sensor drivers (ESP32 side).
//
// Wraps the FT6336U capacitive touch (I2C) and the HX711 load cell. The ESP32
// samples these and forwards raw data to the Pi over the serial protocol.
//
// NOTE: The FT6336U register addresses and HX711 pin assignments should be
// confirmed against the actual hardware wiring.

#ifndef HANDLE_SENSORS_H
#define HANDLE_SENSORS_H

#include <stdint.h>

// Initialize the touch + load-cell drivers. Returns true on success.
bool sensors_init(void);

// Poll the FT6336U. If a tap is present, returns true and fills (x, y) in
// display coordinates (0..319, 0..479) and gesture (0=tap, 1=hold, 2=swipe).
bool sensors_poll_touch(int *x, int *y, int *gesture);

// Read the raw load-cell force (uncalibrated ADC counts). Returns 0 if no
// load cell is present.
int32_t sensors_read_force(void);

#endif // HANDLE_SENSORS_H