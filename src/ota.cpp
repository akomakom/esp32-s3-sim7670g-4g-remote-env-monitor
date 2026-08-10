#include "ota.h"
#include "config.h"
#include "log.h"
#include "secrets/ca_cert.h"
#include <Update.h>
#include <NetworkClientSecure.h>
#include <HTTPClient.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>

// Built-in Mozilla root CA bundle (embedded by CONFIG_MBEDTLS_CERTIFICATE_BUNDLE).
// Used for the OTA HTTPS download so a normal public cert (Let's Encrypt) is
// accepted — see OTA_USE_CERT_BUNDLE. MQTT is unaffected (still pins the CA).
#if OTA_USE_CERT_BUNDLE
extern const uint8_t x509_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_bundle_end[]   asm("_binary_x509_crt_bundle_end");
#endif

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

  // Resumable download: unreliable cellular WILL stall or drop mid-transfer, so
  // instead of restarting we reconnect and ask for the rest via an HTTP Range
  // request, continuing where we left off (nothing re-downloaded). We only give
  // up after OTA_MAX_RESUMES failed segments. Update.write() is sequential, so
  // appending each resumed segment reconstructs the image correctly.
  //
  // NOTE: a FRESH NetworkClientSecure is created per attempt. Reusing one across
  // reconnects loses the cert-bundle attachment ("attach_ssl_certificate_bundle
  // was not called" / "No CA Chain is set"), so the TLS config must be applied on
  // a new client each time.
  static uint8_t buf[2048];
  int    total   = 0;      // full image size, learned from the first response
  size_t written = 0;

  for (int attempt = 0; attempt <= OTA_MAX_RESUMES; attempt++) {
    esp_task_wdt_reset();

    NetworkClientSecure client;
    if (MQTT_TLS_INSECURE) {
      client.setInsecure();
#if OTA_USE_CERT_BUNDLE
    } else {  // trust public CAs (Let's Encrypt etc.) via the Mozilla bundle
      client.setCACertBundle(x509_crt_bundle_start,
                             x509_crt_bundle_end - x509_crt_bundle_start);
#else
    } else {  // pin our private CA
      client.setCACert(MQTT_CA_CERT);
#endif
    }
    // Be patient with reads: Cat-1 cellular delivers the image in bursts with
    // multi-second gaps; a short read timeout makes a segment abort early.
    client.setTimeout(OTA_DOWNLOAD_TIMEOUT_S);

    HTTPClient http;
    if (!http.begin(client, url)) { LOGW("ota: begin failed (attempt %d)", attempt); delay(1500); continue; }
    http.setTimeout(OTA_DOWNLOAD_TIMEOUT_S * 1000);
    if (written > 0) http.addHeader("Range", "bytes=" + String((uint32_t)written) + "-");

    int code = http.GET();
    if (written == 0) {                       // first segment: expect 200 + full size
      if (code != HTTP_CODE_OK) { LOGW("ota: HTTP %d (attempt %d)", code, attempt); http.end(); delay(1500); continue; }
      total = http.getSize();
      if (total <= 0 || !Update.begin(total)) {
        LOGE("ota: Update.begin failed (len=%d)", total); http.end(); return false;
      }
      LOGI("ota: downloading %d bytes", total);
    } else {                                  // resume: server MUST honour Range
      if (code != 206) {
        LOGE("ota: resume got HTTP %d (expected 206) — server won't range, aborting", code);
        http.end(); Update.abort(); return false;
      }
    }

    Stream& stream = http.getStream();
    uint32_t last_data = millis();
    while (written < (size_t)total) {
      esp_task_wdt_reset();
      size_t want = (size_t)total - written;
      if (want > sizeof(buf)) want = sizeof(buf);
      size_t got = stream.readBytes(buf, want);   // waits up to the client timeout
      if (got == 0) {
        if (!client.connected() && stream.available() == 0) { LOGW("ota: connection dropped at %u/%d", (unsigned)written, total); break; }
        if (millis() - last_data > (uint32_t)OTA_DOWNLOAD_TIMEOUT_S * 1000) { LOGW("ota: stalled at %u/%d", (unsigned)written, total); break; }
        delay(10);
        continue;
      }
      if (Update.write(buf, got) != got) {
        LOGE("ota: flash write failed at %u: %s", (unsigned)written, Update.errorString());
        http.end(); Update.abort(); return false;
      }
      written += got;
      last_data = millis();
      if ((written & 0x3FFFF) < got) LOGI("ota: %u/%d bytes", (unsigned)written, total); // ~every 256KB
    }
    http.end();
    if (written >= (size_t)total) break;      // complete
    LOGW("ota: resuming from %u/%d (attempt %d/%d)", (unsigned)written, total, attempt + 1, OTA_MAX_RESUMES);
    delay(1000);                              // brief backoff before reconnecting
  }

  if (total <= 0 || written != (size_t)total || !Update.end() || !Update.isFinished()) {
    LOGE("ota: incomplete (%u/%d): %s", (unsigned)written, total, Update.errorString());
    Update.abort();
    return false;
  }
  LOGW("ota: image written OK, rebooting into trial image");
  delay(500);
  ESP.restart();
  return true; // not reached
}

} // namespace ota
