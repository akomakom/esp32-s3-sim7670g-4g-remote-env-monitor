// Cellular PPP link over the SIM7670G (spec §3). Uses the ESP32 Arduino core
// 3.x native PPP class (wraps esp_modem) — the path the spec recommends.
#pragma once
#include <Arduino.h>

namespace cellular {

void begin();          // power the modem, start PPP dial
bool isConnected();    // IP is up
bool ensureConnected();// (re)dial if needed; returns connected state
void loop();           // service link-state events

int  signalRSRP();     // last known signal in dBm (0 if unknown) — for health
const char* operatorName();
const char* localIP();  // PPP-assigned local IPv4 as a string ("" if link down)

} // namespace cellular
