#include "runtime_config.h"
#include "config.h"
#include "log.h"
#include <Preferences.h>
#include <ArduinoJson.h>

namespace rconfig {

static Settings   s;
static Preferences prefs;

static void loadDefaults() {
  s.sample_interval_s = DEFAULT_SAMPLE_INTERVAL_S;
  s.report_interval_s = DEFAULT_REPORT_INTERVAL_S;
  s.light_threshold   = DEFAULT_LIGHT_THRESHOLD;
  s.light_delta       = DEFAULT_LIGHT_DELTA;
  for (size_t i = 0; i < SENSOR_COUNT && i < 16; i++)
    s.sensor_enabled[i] = SENSORS[i].enabled;
}

static void persist() {
  prefs.begin("rcfg", false);
  prefs.putUInt("sample", s.sample_interval_s);
  prefs.putUInt("report", s.report_interval_s);
  uint16_t mask = 0;
  for (size_t i = 0; i < SENSOR_COUNT && i < 16; i++)
    if (s.sensor_enabled[i]) mask |= (1u << i);
  prefs.putUShort("enmask", mask);
  // Remember how many sensors the mask covers, so a mask saved before a new
  // sensor was added doesn't silently disable that (higher-index) sensor.
  prefs.putUShort("encnt", (uint16_t)(SENSOR_COUNT < 16 ? SENSOR_COUNT : 16));
  prefs.putUShort("lthr", s.light_threshold);
  prefs.putUShort("ldelta", s.light_delta);
  prefs.end();
}

void begin() {
  loadDefaults();
  prefs.begin("rcfg", true);
  if (prefs.isKey("sample")) s.sample_interval_s = prefs.getUInt("sample", s.sample_interval_s);
  if (prefs.isKey("report")) s.report_interval_s = prefs.getUInt("report", s.report_interval_s);
  if (prefs.isKey("enmask")) {
    uint16_t mask = prefs.getUShort("enmask", 0xFFFF);
    // Only honour the mask for sensors that existed when it was saved; any sensor
    // added since (higher index, and pre-"encnt" masks -> saved=0) keeps its
    // compiled SENSORS[].enabled default rather than being forced off.
    size_t saved = prefs.isKey("encnt") ? prefs.getUShort("encnt", 0) : 0;
    for (size_t i = 0; i < SENSOR_COUNT && i < 16 && i < saved; i++)
      s.sensor_enabled[i] = mask & (1u << i);
  }
  if (prefs.isKey("lthr"))   s.light_threshold = prefs.getUShort("lthr", s.light_threshold);
  if (prefs.isKey("ldelta")) s.light_delta     = prefs.getUShort("ldelta", s.light_delta);
  prefs.end();
  LOGI("config: sample=%us report=%us", s.sample_interval_s, s.report_interval_s);
}

const Settings& get() { return s; }

static uint32_t clampInterval(uint32_t v) {
  if (v < 10)     v = 10;      // never hammer the modem
  if (v > 86400)  v = 86400;   // at least once/day
  return v;
}

bool setSampleIntervalS(uint32_t v) {
  v = clampInterval(v);
  if (v == s.sample_interval_s) return false;
  s.sample_interval_s = v;
  persist();
  LOGI("config: sample=%us (set)", v);
  return true;
}

bool setReportIntervalS(uint32_t v) {
  v = clampInterval(v);
  if (v == s.report_interval_s) return false;
  s.report_interval_s = v;
  persist();
  LOGI("config: report=%us (set)", v);
  return true;
}

bool setLightThreshold(uint16_t v) {
  if (v > 4095) v = 4095;
  if (v == s.light_threshold) return false;
  s.light_threshold = v;
  persist();
  LOGI("config: light_threshold=%u (set)", v);
  return true;
}

bool applyJson(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len)) { LOGW("config: bad JSON ignored"); return false; }
  bool changed = false;

  if (doc["sample_s"].is<uint32_t>()) {
    uint32_t v = clampInterval(doc["sample_s"]);
    if (v != s.sample_interval_s) { s.sample_interval_s = v; changed = true; }
  }
  if (doc["report_s"].is<uint32_t>()) {
    uint32_t v = clampInterval(doc["report_s"]);
    if (v != s.report_interval_s) { s.report_interval_s = v; changed = true; }
  }
  // Occupancy thresholds: {"light_threshold":1800,"light_delta":600} (ADC counts).
  if (doc["light_threshold"].is<uint16_t>()) {
    uint16_t v = doc["light_threshold"];
    if (v != s.light_threshold) { s.light_threshold = v; changed = true; }
  }
  if (doc["light_delta"].is<uint16_t>()) {
    uint16_t v = doc["light_delta"];
    if (v != s.light_delta) { s.light_delta = v; changed = true; }
  }

  // Per-sensor enable by key: {"en":{"garage":false,"supply":true}}
  JsonObject en = doc["en"].as<JsonObject>();
  if (!en.isNull()) {
    for (size_t i = 0; i < SENSOR_COUNT && i < 16; i++) {
      JsonVariant v = en[SENSORS[i].key];
      if (v.is<bool>() && v.as<bool>() != s.sensor_enabled[i]) {
        s.sensor_enabled[i] = v.as<bool>();
        changed = true;
      }
    }
  }

  if (changed) { persist(); LOGI("config: updated sample=%us report=%us",
                                 s.sample_interval_s, s.report_interval_s); }
  return changed;
}

} // namespace rconfig
