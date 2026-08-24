// =============================================================================
//  espnow_tub — ESP-NOW client for the hot tub controller (water temperature)
// =============================================================================
//
//  Ported from the CYD display client (esp32-spa-controller/display/ESPNowUtils),
//  but reworked to be fully non-blocking and failure-tolerant so it can run
//  alongside the cellular/MQTT pipeline without ever stalling the main loop or
//  tripping the watchdog:
//
//    * The controller's WiFi channel is unknown (it follows its home AP), so we
//      channel-hop 11..1 sending pairing requests until it answers, exactly like
//      the CYD client. The whole search is a millis()-driven state machine —
//      loop() returns immediately every call.
//    * Every ESP-IDF call is checked and tolerated (no ESP_ERROR_CHECK panics):
//      if WiFi/ESP-NOW ever fails we just keep retrying on the next loop().
//    * The device uplinks over cellular (PPP), not WiFi, so putting the radio in
//      STA mode purely for ESP-NOW does not disturb any other subsystem.
//
//  Only SERVER_STATUS (water temperature) is consumed for now; other message
//  types are received and ignored. See hot_tub_types.h for the wire format.
// =============================================================================
#pragma once
#include <Arduino.h>

namespace espnow_tub {

// Bring up WiFi (STA) + ESP-NOW and start the channel search. Safe to call even
// if HOTTUB_ESPNOW_ENABLED is off (it becomes a no-op). Non-blocking.
void begin();

// Advance the pairing/channel-search state machine. Call every main-loop pass;
// returns immediately. No-op if disabled or not yet begun.
void loop();

// Latest hot tub water temperature, already converted to °C. Returns false if we
// have never received a reading or the most recent one is older than max_age_ms
// (stale -> treated like a failed sensor read so nothing bogus is buffered).
bool latestWaterTempC(float& out_c, uint32_t max_age_ms);

// True once we have locked onto the controller's channel (paired). Informational
// (health/logging); receiving data does not strictly require this to stay true.
bool isPaired();

// RSSI (dBm, negative) of the most recently heard controller packet, or 0 if none
// within max_age_ms. Captured on any received packet — even pairing replies — so
// it's a useful link-quality metric even when two-way pairing keeps failing.
int  rssi(uint32_t max_age_ms);

} // namespace espnow_tub
