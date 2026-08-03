// MQTT-over-TLS transport (spec §3, §5, §7).
//  - one long-lived TLS/MQTT session, TLS session resumption enabled
//  - subscribes to the retained config topic and the cmd topic
//  - counts bytes on the wire for the data-budget health metric (§5, §8)
#pragma once
#include <Arduino.h>
#include <stddef.h>

namespace mqtt {

// Called when a firmware-update command arrives on the cmd topic.
typedef void (*OtaCmdHandler)(const char* url);

void   begin(OtaCmdHandler ota_cb);
bool   ensureConnected();          // (re)connect if the session dropped
void   loop();                     // service keepalive + inbound messages
bool   isConnected();

// Publish a raw payload. Returns true on success; tallies bytes sent.
bool   publish(const char* topic, const uint8_t* data, size_t len, bool retained = false);
bool   publishString(const char* topic, const char* s, bool retained = false);

uint32_t bytesSentToday();         // resets at UTC midnight (best-effort)
uint32_t bytesRecvToday();

} // namespace mqtt
