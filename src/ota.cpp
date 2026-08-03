#include "ota.h"
#include "config.h"
#include "log.h"
#include "secrets/ca_cert.h"
#include <Update.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>

namespace ota {

static uint32_t g_boot_ms = 0;
static bool     g_marked  = false;

void begin() {
  g_boot_ms = millis();
  // If we booted a PENDING_VERIFY image, it stays on trial until markHealthy.
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t st;
  if (esp_ota_get_state_partition(running, &st) == ESP_OK &&
      st == ESP_OTA_IMG_PENDING_VERIFY) {
    LOGW("ota: running a NEW image on trial — will confirm after %ds", OTA_HEALTHCHECK_S);
  } else {
    g_marked = true; // already-good image, nothing to confirm
  }
}

void markHealthyIfDue() {
  if (!OTA_ENABLED || g_marked) return;
  if (millis() - g_boot_ms < (uint32_t)OTA_HEALTHCHECK_S * 1000) return;
  if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
    LOGI("ota: new image confirmed healthy, rollback cancelled");
    g_marked = true;
  }
}

bool applyFromUrl(const char* url) {
  if (!OTA_ENABLED) { LOGW("ota: disabled"); return false; }
  if (strncmp(url, OTA_ALLOWED_PREFIX, strlen(OTA_ALLOWED_PREFIX)) != 0) {
    LOGE("ota: url not on allow-listed host, refusing: %s", url);
    return false;
  }
  LOGI("ota: pulling %s", url);

  NetworkClientSecure client;
  if (MQTT_TLS_INSECURE) client.setInsecure(); else client.setCACert(MQTT_CA_CERT);

  HTTPClient http;
  if (!http.begin(client, url)) { LOGE("ota: begin failed"); return false; }
  int code = http.GET();
  if (code != HTTP_CODE_OK) { LOGE("ota: HTTP %d", code); http.end(); return false; }

  int len = http.getSize();
  if (len <= 0 || !Update.begin(len)) { LOGE("ota: Update.begin failed"); http.end(); return false; }

  size_t written = Update.writeStream(http.getStream());
  http.end();
  if (written != (size_t)len || !Update.end() || !Update.isFinished()) {
    LOGE("ota: write/verify failed (%u/%d)", (unsigned)written, len);
    Update.abort();
    return false;
  }
  LOGW("ota: image written OK, rebooting into trial image");
  delay(500);
  ESP.restart();
  return true; // not reached
}

} // namespace ota
