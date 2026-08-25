// Occupancy / ambient-light sensor (GL5528 photoresistor on PIN_LDR).
//
// A GL5528 is analog and the light/dark threshold is MQTT-tunable, so this can't
// be a plain GPIO ISR (fixed logic level, and analogRead can't run in an ISR).
// Instead a dedicated FreeRTOS task samples the ADC every LIGHT_POLL_MS —
// independent of the main loop (which blocks during sampling/TLS) — and fires an
// "occupancy event" when the level crosses the configurable threshold OR jumps by
// the configurable delta. Events are logged immediately; the main loop publishes
// the occupancy state to MQTT (PubSubClient isn't thread-safe, so the task never
// publishes directly).
#pragma once
#include <Arduino.h>

namespace light {

void begin();          // start the ADC sampling task (no-op if PIN_LDR < 0)
int  level();          // last light level (ADC counts 0..4095), -1 if disabled
bool occupied();       // true when the level is above the light threshold

// If a new occupancy event has occurred since the last call, returns true and
// fills the current state; call from the main loop to publish it. Clears the flag.
bool takeEvent(bool& occupied_out, int& level_out);

} // namespace light
