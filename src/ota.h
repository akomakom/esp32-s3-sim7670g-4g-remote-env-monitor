// OTA firmware update with automatic rollback (spec §4.5, §6).
//  - triggered ONLY by an explicit MQTT command (never automatic)
//  - image is pulled over HTTPS from an allow-listed host (protects data budget)
//  - dual OTA partitions; if the new image fails its post-boot health check the
//    bootloader rolls back to the last-known-good image.
#pragma once
#include <Arduino.h>

namespace ota {

// Call early in setup(): confirm-or-rollback the freshly-booted image.
void begin();

// Once the device has run healthily for OTA_HEALTHCHECK_S, mark the running
// image valid so it is not rolled back on next boot.
void markHealthyIfDue();

// Download + apply an image from `url` (must match OTA_ALLOWED_PREFIX), then
// reboot. Returns false (and stays on current image) on any failure.
bool applyFromUrl(const char* url);

} // namespace ota
