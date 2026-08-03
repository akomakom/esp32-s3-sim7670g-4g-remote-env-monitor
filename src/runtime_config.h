// Runtime-adjustable config (spec §4 remote config). Defaults come from
// config.h; overrides arrive on the retained MQTT config topic and persist in
// NVS so they survive reboot. Schema is designed to extend (per-sensor enable,
// thresholds) without firmware changes.
#pragma once
#include <Arduino.h>

namespace rconfig {

struct Settings {
  uint32_t sample_interval_s;
  uint32_t report_interval_s;
  bool     sensor_enabled[16];   // per-sensor override, indexed like SENSORS[]
};

void            begin();                 // load persisted overrides from NVS
const Settings& get();

// Apply a JSON config document (from the retained topic). Unknown fields are
// ignored; known fields are validated, clamped, persisted. Returns true if
// anything changed. Example: {"sample_s":300,"report_s":1800,"en":{"garage":false}}
bool applyJson(const char* json, size_t len);

} // namespace rconfig
