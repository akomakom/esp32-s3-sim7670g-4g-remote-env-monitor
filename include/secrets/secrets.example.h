// Template for secrets/secrets.h — copy to secrets.h and fill in real values:
//   cp include/secrets/secrets.example.h  include/secrets/secrets.h
// secrets.h is git-ignored. It's included at the TOP of config.h, so anything
// defined here overrides the generic defaults there (via their #ifndef guards),
// which keeps your deployment-specific values out of the committed source.
#pragma once

// --- Broker / transport (override config.h's generic placeholders) ---
#define MQTT_HOST       "broker.example.com"
#define MQTT_PORT       8883
#define MQTT_PASSWORD   "change-me"        // must match mosquitto/config/passwd

// --- OTA update host (only URLs under this prefix are accepted) ---
#define OTA_ALLOWED_PREFIX  "https://ota.example.com/"

// --- Optional per-unit overrides ---
// #define MQTT_USERNAME   "rental-mon-01"
// #define DEVICE_ID       "rental-mon-01"
// #define CELL_APN        "hologram"
