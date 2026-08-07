// =============================================================================
//  Rental Unit Environmental Monitor — main firmware entry (spec §4, §6, §11)
// =============================================================================
//
//  Orchestrates: sample -> buffer (backdated) -> batched TLS/MQTT publish, with
//  a hardware watchdog, an exponential-backoff link state machine, remote config
//  over a retained topic, self-health telemetry, and explicit OTA w/ rollback.
//
//  Reliability is the top priority (node is inaccessible): every reading is
//  persisted to flash BEFORE any network attempt, so nothing is lost across
//  reboots, outages, or brownouts. Time is stamped at capture and backdated on
//  send so Home Assistant ingests the true capture time.
// =============================================================================
#include <Arduino.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "log.h"
#include "reading.h"
#include "sensors.h"
#include "ring_buffer.h"
#include "payload.h"
#include "runtime_config.h"
#include "cellular.h"
#include "timesync.h"
#include "mqtt_transport.h"
#include "health.h"
#include "ota.h"

// ---- scheduling state ----
static uint32_t g_next_sample_ms = 0;
static uint32_t g_next_report_ms = 0;
static uint32_t g_next_health_ms = 0;

// ---- link backoff state (spec §6) ----
static uint32_t g_backoff_s   = RECONNECT_BACKOFF_MIN_S;
static uint32_t g_retry_at_ms = 0;

// Whether the BOOT button is held at reset -> force provisioning mode.
static bool bootButtonHeld() {
  pinMode(0, INPUT_PULLUP);   // GPIO0 = BOOT
  delay(20);
  return digitalRead(0) == LOW;
}

static void feedWatchdog() { esp_task_wdt_reset(); }

// OTA command callback (invoked from the MQTT layer on an explicit command).
static void onOtaCommand(const char* url) {
  mqtt::publishString(TOPIC_ACK, "{\"ota\":\"starting\"}");
  ota::applyFromUrl(url); // reboots on success; returns on failure
  mqtt::publishString(TOPIC_ACK, "{\"ota\":\"failed\"}");
}

// -----------------------------------------------------------------------------
void setup() {
  logInit();
  LOGI("=== Rental Monitor %s booting (%s) ===", FW_VERSION, DEVICE_ID);

  // Hardware watchdog: auto-reboot if the main loop ever hangs (spec §6).
  // The Arduino core already inits the Task WDT at startup, so a plain
  // esp_task_wdt_init() here returns ESP_ERR_INVALID_STATE and our (longer)
  // timeout would be silently ignored — reconfigure in that case.
  esp_task_wdt_config_t wdt = {
    .timeout_ms = (uint32_t)WATCHDOG_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true,
  };
  if (esp_task_wdt_init(&wdt) == ESP_ERR_INVALID_STATE) esp_task_wdt_reconfigure(&wdt);
  esp_task_wdt_add(NULL);

  ota::begin();                 // confirm-or-rollback a freshly flashed image
  ringbuf::begin();             // MUST come up before we sample (no data loss)
  rconfig::begin();             // load persisted interval/enable overrides
  sensors::begin();

  // One-time RS485 provisioning path (spec §2): config flag or BOOT held.
  if (PROVISIONING_MODE || bootButtonHeld()) {
    esp_task_wdt_delete(NULL);  // interactive tool blocks; don't trip the WDT
    sensors::runProvisioningTool();
    LOGI("Provisioning done — reflash with PROVISIONING_MODE=false to deploy.");
    while (true) { delay(1000); }
  }

#if CELLULAR_ENABLED
  cellular::begin();
  timesync::begin();
  mqtt::begin(onOtaCommand);
#else
  LOGW("bench mode: CELLULAR_ENABLED=0 — modem/NTP/MQTT disabled, sensors only");
  (void)onOtaCommand;
#endif

  uint32_t now = millis();
  g_next_sample_ms = now;                                    // sample immediately
  g_next_report_ms = now + rconfig::get().report_interval_s * 1000UL;
  g_next_health_ms = now + 15000;                            // first health soon
  LOGI("setup complete");
}

// -----------------------------------------------------------------------------
//  Sampling: read every enabled sensor, stamp UTC, persist to flash buffer.
// -----------------------------------------------------------------------------
static void doSample() {
  uint32_t ts = timesync::nowUtc();   // 0 if clock not yet synced
  const auto& cfg = rconfig::get();
  int ok = 0;
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    if (i < 16 && !cfg.sensor_enabled[i]) continue;
    Reading r = sensors::read(i);
    if (!r.valid) continue;
    r.ts = ts;                        // capture-time timestamp (backdated later)
    ringbuf::push(r);                 // persisted BEFORE any network attempt
    ok++;
  }
  LOGI("sampled %d readings, %u buffered", ok, (unsigned)ringbuf::count());
}

// -----------------------------------------------------------------------------
//  Reporting: drain the flash buffer in batches, backdated, over one TLS session.
//  Only pop records AFTER a confirmed publish (no data loss on failure).
// -----------------------------------------------------------------------------
static void doReport() {
  if (!mqtt::isConnected()) return;
  static Reading batch[MAX_BATCH_READINGS];
  static uint8_t buf[MQTT_BUFFER_BYTES];

  while (ringbuf::count() > 0) {
    size_t n = 0;
    size_t avail = ringbuf::count();
    for (size_t i = 0; i < avail && n < MAX_BATCH_READINGS; i++)
      if (ringbuf::peek(i, batch[n])) n++;

    size_t len = payload::encodeBatch(batch, n, buf, sizeof(buf));
    if (len == 0) { LOGE("report: encode failed"); break; }

    if (!mqtt::publish(TOPIC_DATA, buf, len, /*retained*/false)) {
      LOGW("report: publish failed, keeping %u records", (unsigned)n);
      break;                          // keep data; retry next cycle
    }
    ringbuf::popFront(n);             // confirmed sent -> safe to drop
    LOGI("reported %u readings (%u B), %u left", (unsigned)n, (unsigned)len,
         (unsigned)ringbuf::count());
  }
}

static void doHealth() {
  if (!mqtt::isConnected()) return;
  char js[320];
  size_t n = health::buildJson(js, sizeof(js));
  mqtt::publish(TOPIC_HEALTH, (const uint8_t*)js, n, /*retained*/false);
}

// -----------------------------------------------------------------------------
//  Link state machine with exponential backoff (spec §6). Never busy-loops the
//  modem; on repeated failure it backs off to protect data + battery.
// -----------------------------------------------------------------------------
static bool serviceLink() {
#if !CELLULAR_ENABLED
  return false;                      // bench mode: never touch the modem
#else
  cellular::loop();

  // Low-battery brownout guard (spec §6): sleep instead of crash-looping.
  float vbat = health::batteryVoltage();
  if (BATTERY_INSTALLED && vbat > 0.1f && vbat < LOW_BATTERY_SLEEP_V) {
    LOGW("low battery %.2fV -> deep sleep %ds", vbat, LOW_BATTERY_SLEEP_S);
    esp_sleep_enable_timer_wakeup((uint64_t)LOW_BATTERY_SLEEP_S * 1000000ULL);
    esp_deep_sleep_start();
  }

  if (cellular::isConnected() && mqtt::isConnected()) {
    g_backoff_s = RECONNECT_BACKOFF_MIN_S; // healthy -> reset backoff
    return true;
  }
  if (millis() < g_retry_at_ms) return false; // still backing off

  bool up = cellular::ensureConnected() && mqtt::ensureConnected();
  if (up) {
    g_backoff_s = RECONNECT_BACKOFF_MIN_S;
  } else {
    g_retry_at_ms = millis() + g_backoff_s * 1000UL;
    LOGW("link down, backoff %us", g_backoff_s);
    g_backoff_s = min<uint32_t>(g_backoff_s * 2, RECONNECT_BACKOFF_MAX_S);
  }
  return up;
#endif
}

// -----------------------------------------------------------------------------
void loop() {
  feedWatchdog();

  bool linkUp = serviceLink();
  if (linkUp) mqtt::loop();          // service keepalive + inbound config/cmd

  ota::markHealthyIfDue();           // confirm a trial image once stable

  uint32_t now = millis();
  const auto& cfg = rconfig::get();

  if ((int32_t)(now - g_next_sample_ms) >= 0) {
    doSample();
    g_next_sample_ms = now + cfg.sample_interval_s * 1000UL;
  }
  if ((int32_t)(now - g_next_report_ms) >= 0) {
    if (linkUp) doReport();
    g_next_report_ms = now + cfg.report_interval_s * 1000UL;
  }
  if ((int32_t)(now - g_next_health_ms) >= 0) {
    if (linkUp) doHealth();
    g_next_health_ms = now + cfg.report_interval_s * 1000UL;
  }

  delay(50);                          // yield; keeps CPU + data usage low
}
