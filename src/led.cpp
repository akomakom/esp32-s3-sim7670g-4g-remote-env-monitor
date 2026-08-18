// =============================================================================
//  led.cpp — WS2812 status LED driver (single onboard pixel via rgbLedWrite).
// =============================================================================
//  Colour + pattern legend:
//    BOOT       amber,  solid            starting up
//    PROVISION  purple, solid            RS485 addressing tool
//    OFFLINE    red,    slow blink       no uplink (link down / bench mode)
//    CONNECTING blue,   fast blink       modem up, dialing / MQTT connecting
//    ONLINE     green,  slow breathe     connected & idle ("heartbeat")
//    SENDING    cyan,   solid            publishing a batch right now
//    SEND_OK    green,  fast blink       last publish succeeded (~1.5 s)
//    SEND_FAIL  red,    fast blink       last publish failed (~2 s)
//    OTA        white,  blink            OTA update in progress
//    LOWBAT     red,    very fast blink  low battery, about to deep-sleep
//
//  All output goes through the configured master brightness (STATUS_LED_BRIGHTNESS)
//  because the raw WS2812 is uncomfortably bright at full scale.
// =============================================================================
#include <Arduino.h>       // must precede config.h (defines size_t, and the
#include "config.h"        // esp32s3 variant's default PIN_RGB_LED we override)
#include "led.h"
#include "log.h"

namespace led {

#if (PIN_RGB_LED >= 0) && STATUS_LED_ENABLED

static Status   s_base          = BOOT;
static Status   s_overlay       = BOOT;
static uint32_t s_overlay_until = 0;   // millis deadline; 0 = no overlay active

static inline uint8_t scale(uint8_t v) {
  return (uint8_t)((uint16_t)v * STATUS_LED_BRIGHTNESS / 255);
}
static inline void put(uint8_t r, uint8_t g, uint8_t b) {
#if STATUS_LED_SWAP_RG
  rgbLedWrite(PIN_RGB_LED, scale(g), scale(r), scale(b));  // this board swaps R/G
#else
  rgbLedWrite(PIN_RGB_LED, scale(r), scale(g), scale(b));
#endif
}

// Triangle wave 0..255..0 over `period` ms — smooth breathing.
static uint8_t breathe(uint32_t period) {
  uint32_t t = millis() % period, half = period / 2;
  uint32_t x = (t < half) ? t : (period - t);
  return (uint8_t)(x * 255 / half);
}
// Square wave; true during the "on" half of each `period`.
static bool blink(uint32_t period) { return (millis() % period) < (period / 2); }

static void render() {
  Status st = s_base;
  if (s_overlay_until) {
    if ((int32_t)(s_overlay_until - millis()) > 0) st = s_overlay;
    else s_overlay_until = 0;
  }
  switch (st) {
    case BOOT:       put(70, 45, 0);                                  break; // amber
    case PROVISION:  put(90, 0, 90);                                  break; // purple
    case OFFLINE:    put(blink(1400) ? 140 : 0, 0, 0);                break; // red slow
    case CONNECTING: put(0, 0, blink(400) ? 150 : 0);                 break; // blue fast
    case ONLINE:     put(0, 40 + (uint8_t)((uint16_t)breathe(3000) * 3 / 4), 0); break; // green breathe (never fully dark)
    case SENDING:    put(0, 80, 90);                                  break; // cyan
    case SEND_OK:    put(0, blink(240) ? 170 : 0, 0);                 break; // green blink
    case SEND_FAIL:  put(blink(240) ? 170 : 0, 0, 0);                 break; // red blink
    case OTA:        { uint8_t v = blink(300) ? 140 : 0; put(v, v, v); } break; // white
    case LOWBAT:     put(blink(150) ? 170 : 0, 0, 0);                 break; // red v.fast
  }
}

void begin() {
#if STATUS_LED_SELFTEST
  // Show each primary + white so the operator can confirm the colour mapping.
  // Values go through put()/STATUS_LED_SWAP_RG, so a correct mapping means the
  // logged name matches what you see.
  const struct { uint8_t r, g, b; const char* name; } test[] = {
    {200, 0, 0, "RED"}, {0, 200, 0, "GREEN"}, {0, 0, 200, "BLUE"}, {160, 160, 160, "WHITE"},
  };
  for (auto& t : test) { LOGI("LED self-test: %s", t.name); put(t.r, t.g, t.b); delay(500); }
#endif
  put(0, 0, 0);
}
void set(Status s) { s_base = s; render(); }
void event(Status s, uint32_t ms) {
  s_overlay = s;
  s_overlay_until = millis() + ms;
  if (!s_overlay_until) s_overlay_until = 1;  // guard the millis-wrap == 0 case
  render();
}
void loop() { render(); }

#else  // LED disabled or no pin -> no-ops

void begin() {}
void set(Status) {}
void event(Status, uint32_t) {}
void loop() {}

#endif

} // namespace led
