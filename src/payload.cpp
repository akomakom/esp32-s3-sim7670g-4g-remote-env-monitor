#include "payload.h"
#include "config.h"
#include <ArduinoJson.h>

namespace payload {

static float r2(float v) { return roundf(v * 100.0f) / 100.0f; }

// Wire schema (MessagePack), short keys to save bytes:
//   { "v":1, "d":"<device>", "r":[ {"i":idx,"t":ts,"c":temp,"h":rh,"p":dew,"a":ah}, ... ] }
// Humidity-derived fields (h/p/a) are OMITTED for temp-only sensors (e.g. the
// hot tub water temp over ESP-NOW), where they are stored as NaN.
size_t encodeBatch(const Reading* readings, size_t n, uint8_t* out, size_t out_cap) {
  JsonDocument doc;
  doc["v"] = 1;
  doc["d"] = DEVICE_ID;
  JsonArray arr = doc["r"].to<JsonArray>();
  for (size_t i = 0; i < n; i++) {
    const Reading& rd = readings[i];
    if (!rd.valid) continue;
    JsonObject o = arr.add<JsonObject>();
    o["i"] = rd.sensor_idx;
    o["t"] = rd.ts;
    o["c"] = r2(rd.temp_c);
    if (!isnan(rd.rh))      o["h"] = r2(rd.rh);
    if (!isnan(rd.dew_c))   o["p"] = r2(rd.dew_c);
    if (!isnan(rd.abs_hum)) o["a"] = r2(rd.abs_hum);
  }
  return serializeMsgPack(doc, out, out_cap);
}

} // namespace payload
