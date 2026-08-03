// Self-health telemetry (spec §8) — published alongside sensor data.
#pragma once
#include <Arduino.h>

namespace health {

float    batteryVoltage();   // 0 if no cell installed
bool     mainsPresent();     // USB/mains power detected
const char* resetReason();   // human string for last reboot cause

// Build the health JSON (small, published to TOPIC_HEALTH). Returns length.
size_t buildJson(char* out, size_t cap);

} // namespace health
