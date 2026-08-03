// One environmental sample from one sensor, with derived moisture metrics.
#pragma once
#include <Arduino.h>

struct Reading {
  uint32_t ts;         // capture time, UTC epoch seconds (0 = time not yet set)
  uint8_t  sensor_idx; // index into SENSORS[] (see config.h)
  float    temp_c;     // temperature °C
  float    rh;         // relative humidity %
  float    dew_c;      // derived: dew point °C
  float    abs_hum;    // derived: absolute humidity g/m^3
  bool     valid;      // false if the read failed (kept out of the buffer)
};
