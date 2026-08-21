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
#include "espnow_tub.h"
#include "led.h"

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
  led::set(led::OTA);
  mqtt::publishString(TOPIC_ACK, "{\"ota\":\"starting\"}");
  ota::applyFromUrl(url); // reboots on success; returns on failure
  mqtt::publishString(TOPIC_ACK, "{\"ota\":\"failed\"}");
}

// "Report now" callback (HA button): sample every sensor and publish this loop,
// instead of waiting for the next scheduled sample/report. mqtt::loop() runs
// before the sample/report checks in loop(), so due-timers set here fire now.
static void onReportNow() {
  LOGI("cmd: report-now requested");
  g_next_sample_ms = millis();
  g_next_report_ms = millis();
}

// -----------------------------------------------------------------------------
void setup() {
  logInit();
  LOGI("=== Rental Monitor %s booting (%s) ===", FW_VERSION_FULL, DEVICE_ID);
  led::begin();
  led::set(led::BOOT);

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
    led::set(led::PROVISION);
    sensors::runProvisioningTool();
    LOGI("Provisioning done — reflash with PROVISIONING_MODE=false to deploy.");
    while (true) { delay(1000); }
  }

  // Hot tub water temp over ESP-NOW (uses the WiFi radio only; independent of the
  // cellular PPP uplink). Non-blocking; safe/no-op if the feature is disabled.
  espnow_tub::begin();

#if CELLULAR_ENABLED
  cellular::begin();
  timesync::begin();
  mqtt::begin(onOtaCommand, onReportNow);
#else
  LOGW("bench mode: CELLULAR_ENABLED=0 — modem/NTP/MQTT disabled, sensors only");
  (void)onOtaCommand;
  (void)onReportNow;
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
  const auto& cfg = rconfig::get();

  // Power the RS485 sensors on only for this read window (if power-switched and
  // at least one is enabled), so they can't self-heat from being always-on.
  bool any_rs485 = false;
  for (uint8_t i = 0; i < SENSOR_COUNT; i++)
    if (SENSORS[i].bus == BUS_RS485 && (i >= 16 || cfg.sensor_enabled[i])) { any_rs485 = true; break; }
  if (any_rs485) sensors::rs485PowerOn();   // includes the warm-up delay

  uint32_t ts = timesync::nowUtc();   // stamp at read time (after warm-up); 0 if unsynced
  int ok = 0;
  for (uint8_t i = 0; i < SENSOR_COUNT; i++) {
    if (i < 16 && !cfg.sensor_enabled[i]) continue;
    uint32_t t0 = millis();
    Reading r = sensors::read(i);
    uint32_t t_read = millis() - t0;
    uint32_t t_store = 0;
    if (r.valid) {
      r.ts = ts;                      // capture-time timestamp (backdated later)
      uint32_t t1 = millis();
      ringbuf::push(r);               // persisted BEFORE any network attempt
      t_store = millis() - t1;
      ok++;
    }
    // read vs store timing pinpoints where a slow cycle goes (Modbus vs LittleFS).
    LOGD("sensor %u: read=%lums store=%lums", i, (unsigned long)t_read, (unsigned long)t_store);
    feedWatchdog();                   // slow reads / flash writes must not starve the WDT
  }

  if (any_rs485) sensors::rs485PowerOff();
  LOGI("sampled %d readings, %u buffered", ok, (unsigned)ringbuf::count());
}

// -----------------------------------------------------------------------------
//  Reporting: drain the flash buffer in batches, backdated, over one TLS session.
//  Only pop records AFTER a confirmed publish (no data loss on failure).
// -----------------------------------------------------------------------------
static void doReport() {
  if (!mqtt::isConnected()) return;
  if (ringbuf::count() == 0) return;   // nothing buffered -> no send to signal
  static Reading batch[MAX_BATCH_READINGS];
  static uint8_t buf[MQTT_BUFFER_BYTES];
  led::event(led::SENDING, 5000);      // cyan while draining; result overwrites below
  bool failed = false;

  // Drain at most MAX_REPORT_BATCHES_PER_CYCLE per call. A large backlog (e.g.
  // after an outage) would otherwise publish dozens of TLS batches back-to-back,
  // blocking the loop past the watchdog timeout -> reset -> backlog never clears.
  // Feed the WDT and service the MQTT keepalive between batches; the remainder
  // drains on the next report cycle.
  for (uint16_t b = 0; b < MAX_REPORT_BATCHES_PER_CYCLE && ringbuf::count() > 0; b++) {
    size_t n = 0;
    size_t avail = ringbuf::count();
    for (size_t i = 0; i < avail && n < MAX_BATCH_READINGS; i++)
      if (ringbuf::peek(i, batch[n])) n++;

    size_t len = payload::encodeBatch(batch, n, buf, sizeof(buf));
    if (len == 0) { LOGE("report: encode failed"); break; }

    if (!mqtt::publish(TOPIC_DATA, buf, len, /*retained*/false)) {
      LOGW("report: publish failed, keeping %u records", (unsigned)n);
      failed = true;
      break;                          // keep data; retry next cycle
    }
    ringbuf::popFront(n);             // confirmed sent -> safe to drop
    LOGI("reported %u readings (%u B), %u left", (unsigned)n, (unsigned)len,
         (unsigned)ringbuf::count());
    feedWatchdog();                   // long drains must not starve the task WDT
    mqtt::loop();                     // keep the MQTT session serviced meanwhile
  }
  led::event(failed ? led::SEND_FAIL : led::SEND_OK, failed ? 2000 : 1500);
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
    ringbuf::flushNow();              // RAM is lost across deep sleep -> persist first
    led::set(led::LOWBAT);
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

  espnow_tub::loop();                 // advance hot tub channel search / pairing

  bool linkUp = serviceLink();
  if (linkUp) mqtt::loop();          // service keepalive + inbound config/cmd

  // Reflect link state on the status LED (base state; transient publish events
  // from doReport briefly overlay it). Bench mode has no uplink -> OFFLINE.
#if CELLULAR_ENABLED
  if (linkUp)                       led::set(led::ONLINE);
  else if (cellular::isConnected()) led::set(led::CONNECTING);
  else                              led::set(led::OFFLINE);
#else
  led::set(led::OFFLINE);
#endif

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

  ringbuf::maintain();                // rate-limited flash snapshot (outage insurance)

  delay(50);                          // yield; keeps CPU + data usage low
}
