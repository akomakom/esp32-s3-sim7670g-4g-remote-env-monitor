#include "espnow_tub.h"
#include "config.h"
#include "log.h"
#include "hot_tub_types.h"

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace espnow_tub {

#if !HOTTUB_ESPNOW_ENABLED
// ---- Compiled out: everything is a no-op ------------------------------------
void begin() {}
void loop() {}
bool latestWaterTempC(float&, uint32_t) { return false; }
bool isPaired() { return false; }
#else

// ---- shared state (written from the ESP-NOW recv task, read from loop) -------
static portMUX_TYPE   s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile float s_water_temp_f   = NAN;   // last value, as received (°F)
static volatile uint32_t s_last_recv_ms = 0;    // millis() of last SERVER_STATUS
static volatile bool  s_have_reading   = false;

// ---- pairing / channel-search state (main-loop owned, except where noted) ----
static bool           s_enabled_runtime = false; // begin() succeeded at least once
static bool           s_esp_now_ready   = false;
static PairingStatus  s_status          = NOT_PAIRED;
static int            s_channel         = HOTTUB_MAX_CHANNEL; // 11 is most common
static uint32_t       s_last_channel_ms = 0;    // when we last (re)sent a request
static uint32_t       s_retry_begin_ms  = 0;    // backoff for re-init after failure

static const uint8_t  BROADCAST_MAC[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// -----------------------------------------------------------------------------
// Ensure a peer exists for `mac` on `chan`. Failure-tolerant: logs and returns
// false instead of aborting. `chan` 0 means "use the current radio channel".
static bool ensurePeer(const uint8_t* mac, uint8_t chan) {
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = chan;
  peer.encrypt = false;
  esp_now_del_peer(mac);                // ignore result; may not exist yet
  esp_err_t e = esp_now_add_peer(&peer);
  if (e != ESP_OK) { LOGW("espnow: add_peer failed (0x%x)", e); return false; }
  return true;
}

// -----------------------------------------------------------------------------
// ESP-NOW receive callback. Runs in the WiFi task context, NOT the main loop —
// keep it short and guard shared state. We only care about SERVER_STATUS (water
// temp) and the PAIRING reply that tells us the controller's channel.
static void onDataRecv(const esp_now_recv_info* info, const uint8_t* data, int len) {
  if (len < 1) return;
  const uint8_t type = data[0];

  switch (type) {
    case SERVER_STATUS: {
      if (len < (int)sizeof(struct_status_server)) return;   // truncated -> drop
      struct_status_server st;
      memcpy(&st, data, sizeof(st));
      portENTER_CRITICAL(&s_mux);
      s_water_temp_f = st.water_temp;
      s_last_recv_ms = millis();
      s_have_reading = true;
      portEXIT_CRITICAL(&s_mux);
      break;
    }
    case PAIRING: {
      if (len < (int)sizeof(struct_pairing)) return;
      struct_pairing pd;
      memcpy(&pd, data, sizeof(pd));
      if (pd.board_id == 0) {            // 0 == reply from the controller/server
        ensurePeer(pd.macAddr, pd.channel);
        portENTER_CRITICAL(&s_mux);
        s_channel      = pd.channel;     // lock onto the controller's channel
        s_status       = PAIR_PAIRED;
        s_last_recv_ms = millis();       // treat the reply as recent activity
        portEXIT_CRITICAL(&s_mux);
        LOGI("espnow: paired with hot tub on channel %d", pd.channel);
      }
      break;
    }
    default:
      break;                             // CONTROL_STATUS / METRICS_STATUS: ignore
  }
}

// -----------------------------------------------------------------------------
// Send a pairing request on the current channel (broadcast).
static void sendPairingRequest() {
  struct_pairing pd = {};
  pd.msgType  = PAIRING;
  pd.board_id = HOTTUB_BOARD_ID;
  pd.channel  = (uint8_t)s_channel;
  esp_err_t e = esp_now_send(BROADCAST_MAC, (const uint8_t*)&pd, sizeof(pd));
  if (e != ESP_OK) LOGD("espnow: pairing send failed (0x%x)", e);
}

// -----------------------------------------------------------------------------
// One-time (re)initialisation of WiFi STA + ESP-NOW. Returns true on success.
// Fully tolerant: on any failure it returns false and begin()/loop() will retry.
static bool initRadio() {
  // The uplink is cellular (PPP), so STA mode here is used ONLY for ESP-NOW and
  // never associates with an AP. Disable auto-reconnect + power save so nothing
  // hijacks our channel or sleeps through inbound packets.
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  if (esp_now_init() != ESP_OK) {
    LOGW("espnow: esp_now_init failed");
    return false;
  }
  esp_now_register_recv_cb(onDataRecv);

  // Broadcast peer used to fan pairing requests out on each candidate channel
  // (channel 0 => whatever channel the radio is currently set to).
  if (!ensurePeer(BROADCAST_MAC, 0)) {
    esp_now_deinit();
    return false;
  }

  s_channel = HOTTUB_MAX_CHANNEL;
  s_status  = PAIR_REQUEST;
  LOGI("espnow: hot tub client up (MAC %s), searching channels %d..1",
       WiFi.macAddress().c_str(), HOTTUB_MAX_CHANNEL);
  return true;
}

// -----------------------------------------------------------------------------
void begin() {
  s_enabled_runtime = true;
  s_esp_now_ready   = initRadio();
  if (!s_esp_now_ready) s_retry_begin_ms = millis() + 5000; // retry from loop()
}

// -----------------------------------------------------------------------------
void loop() {
  if (!s_enabled_runtime) return;

  // Radio/ESP-NOW not up yet -> retry init on a slow cadence, then bail.
  if (!s_esp_now_ready) {
    if ((int32_t)(millis() - s_retry_begin_ms) >= 0) {
      s_esp_now_ready = initRadio();
      if (!s_esp_now_ready) s_retry_begin_ms = millis() + 5000;
    }
    return;
  }

  // Read the small bits of state the recv callback may have changed.
  PairingStatus status;
  uint32_t last_recv;
  portENTER_CRITICAL(&s_mux);
  status    = s_status;
  last_recv = s_last_recv_ms;
  portEXIT_CRITICAL(&s_mux);

  const uint32_t now = millis();

  switch (status) {
    case NOT_PAIRED:                     // shouldn't happen once initRadio ran
    case PAIR_REQUEST:
      if (esp_wifi_set_channel((uint8_t)s_channel, WIFI_SECOND_CHAN_NONE) != ESP_OK)
        LOGD("espnow: set_channel %d failed", s_channel);
      sendPairingRequest();
      s_last_channel_ms = now;
      portENTER_CRITICAL(&s_mux);
      if (s_status == PAIR_REQUEST) s_status = PAIR_REQUESTED; // don't clobber a
      portEXIT_CRITICAL(&s_mux);                               // concurrent PAIRED
      break;

    case PAIR_REQUESTED:
      // Give the controller a moment to answer, then try the next channel down.
      // Only hop if still unpaired — a pairing reply may have landed (in the recv
      // task) since we snapshotted `status`, setting PAIR_PAIRED + the channel.
      if (now - s_last_channel_ms > HOTTUB_CHANNEL_DWELL_MS) {
        portENTER_CRITICAL(&s_mux);
        if (s_status == PAIR_REQUESTED) {
          if (--s_channel < 1) s_channel = HOTTUB_MAX_CHANNEL;
          s_status = PAIR_REQUEST;
        }
        portEXIT_CRITICAL(&s_mux);
      }
      break;

    case PAIR_PAIRED:
      // If the controller goes quiet (restart / channel change), rediscover.
      if (last_recv == 0 || (now - last_recv) > HOTTUB_MESSAGE_MAX_AGE_MS) {
        LOGW("espnow: hot tub silent >%lums, re-pairing",
             (unsigned long)HOTTUB_MESSAGE_MAX_AGE_MS);
        portENTER_CRITICAL(&s_mux);
        s_status = PAIR_REQUEST;
        portEXIT_CRITICAL(&s_mux);
      }
      break;
  }
}

// -----------------------------------------------------------------------------
bool latestWaterTempC(float& out_c, uint32_t max_age_ms) {
  if (!s_enabled_runtime) return false;

  float    f;
  uint32_t last;
  bool     have;
  portENTER_CRITICAL(&s_mux);
  f    = s_water_temp_f;
  last = s_last_recv_ms;
  have = s_have_reading;
  portEXIT_CRITICAL(&s_mux);

  if (!have || isnan(f)) return false;
  if ((millis() - last) > max_age_ms) return false;   // stale -> not a valid read

  out_c = (f - 32.0f) * (5.0f / 9.0f);                // controller sends °F
  return true;
}

// -----------------------------------------------------------------------------
bool isPaired() {
  if (!s_enabled_runtime) return false;
  portENTER_CRITICAL(&s_mux);
  bool paired = (s_status == PAIR_PAIRED);
  portEXIT_CRITICAL(&s_mux);
  return paired;
}

#endif // HOTTUB_ESPNOW_ENABLED

} // namespace espnow_tub
