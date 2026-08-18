// =============================================================================
//  led.h — onboard WS2812 status LED: at-a-glance device state.
// =============================================================================
//  set() picks the persistent state; event() overlays a transient one (e.g. a
//  publish result) for a few seconds and then reverts to the base state; loop()
//  renders and animates and must be called frequently (it is non-blocking).
//  The colour/pattern legend lives in led.cpp.
// =============================================================================
#pragma once
#include <stdint.h>

namespace led {

enum Status {
  BOOT,        // starting up
  PROVISION,   // RS485 addressing tool
  OFFLINE,     // no uplink (link down, or bench mode)
  CONNECTING,  // modem up, dialing / MQTT connecting
  ONLINE,      // connected & idle
  SENDING,     // publishing a batch right now
  SEND_OK,     // last publish succeeded
  SEND_FAIL,   // last publish failed
  OTA,         // OTA update in progress
  LOWBAT,      // low battery, about to deep-sleep
};

void begin();
void set(Status s);                  // persistent base state
void event(Status s, uint32_t ms);   // transient overlay for `ms`, then revert
void loop();                         // drive animation; call every loop iteration

} // namespace led
