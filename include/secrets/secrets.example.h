// Template for secrets/secrets.h — copy to secrets.h and fill in real values.
//   cp include/secrets/secrets.h.example  include/secrets/secrets.h   (or copy this file)
// secrets.h is git-ignored. It's included at the TOP of config.h, so anything
// defined here overrides the defaults there (via their #ifndef guards).
#pragma once

// MUST match the broker's password file (mosquitto/config/passwd) for this unit.
#define MQTT_PASSWORD   "change-me"

// Optional per-unit overrides — uncomment/edit as needed:
// #define MQTT_USERNAME   "rental-mon-01"
// #define DEVICE_ID       "rental-mon-01"
// #define CELL_APN        "hologram"
