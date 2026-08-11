// =============================================================================
//  cellular.cpp — SIM7670G uplink over USB (esp_modem USB CDC-ACM DTE + PPP)
// =============================================================================
//
//  On the Waveshare ESP32-S3-SIM7670G-4G the modem is connected to the ESP32-S3
//  over USB, NOT a GPIO UART, and this modem does not do PPP over its UART. So we
//  drive it as a USB host (CDC-ACM) and run PPP over that link via ESP-IDF's
//  esp_modem (the USB DTE lives in the esp_modem_usb_dte component; see
//  platformio.ini custom_component_add + custom_sdkconfig). TLS still terminates
//  on the ESP32 (mbedTLS/NetworkClientSecure), so mutual-TLS MQTT is unaffected.
//
//  Prereqs on the board: the "4G" DIP powers the module, and the USB-routing DIP
//  must send the 4G module's USB to the ESP32-S3 (not the Type-C).
//
//  The public cellular:: interface is unchanged, so main.cpp / health.cpp don't
//  change.
// =============================================================================
#include "cellular.h"
#include "config.h"
#include "log.h"

#if CELLULAR_ENABLED

#include "esp_netif.h"
#include "esp_event.h"
#include "esp_modem_api.h"
#include "esp_modem_usb_c_api.h"
#include "esp_modem_usb_config.h"

namespace cellular {

static esp_modem_dce_t* s_dce        = nullptr;
static esp_netif_t*     s_ppp_netif  = nullptr;
static volatile bool    g_up         = false;   // set from the IP event task
static volatile bool    g_usb_gone   = false;   // set from the CDC-ACM error cb
static int              g_rsrp       = 0;
static char             g_oper[32]   = {0};

// PPP got/lost IP — runs in the system event task.
static void onIpEvent(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == IP_EVENT_PPP_GOT_IP) {
    ip_event_got_ip_t* e = (ip_event_got_ip_t*)data;
    esp_netif_set_default_netif(e->esp_netif);   // route lwip sockets via cellular
    g_up = true;
    LOGI("PPP up: " IPSTR, IP2STR(&e->ip_info.ip));
  } else if (id == IP_EVENT_PPP_LOST_IP) {
    g_up = false;
    LOGW("PPP down (lost IP)");
  }
}

// CDC-ACM terminal error (e.g. USB unplugged) — runs in the driver context.
static void onModemError(esp_modem_terminal_error_t err) {
  if (err == ESP_MODEM_TERMINAL_DEVICE_GONE) {
    g_usb_gone = true;
    g_up = false;
    LOGW("cellular: modem USB disconnected");
  }
}

// Create the PPP netif (once) and open the USB modem DCE. Returns false if the
// modem doesn't enumerate (bad DIP/cabling) — caller retries later.
static bool createModem() {
  if (!s_ppp_netif) {
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_PPP();
    s_ppp_netif = esp_netif_new(&cfg);
    if (!s_ppp_netif) { LOGE("cellular: esp_netif_new(PPP) failed"); return false; }
  }

  struct esp_modem_usb_term_config usb =
      ESP_MODEM_DEFAULT_USB_CONFIG(CELL_USB_VID, CELL_USB_PID, CELL_USB_ITF);
  usb.timeout_ms = 20000;   // wait up to 20s for USB enumeration (don't hang setup)

  esp_modem_dte_config_t dte = ESP_MODEM_DTE_DEFAULT_USB_CONFIG(usb);
  esp_modem_dce_config_t dce = ESP_MODEM_DCE_DEFAULT_CONFIG(CELL_APN);

  LOGI("cellular: opening USB modem %04X:%04X itf %d (APN %s)...",
       CELL_USB_VID, CELL_USB_PID, CELL_USB_ITF, CELL_APN);
  s_dce = esp_modem_new_dev_usb(ESP_MODEM_DCE_SIM7600, &dte, &dce, s_ppp_netif);
  if (!s_dce) {
    LOGE("cellular: modem not found on USB — check the USB DIP routes the 4G "
         "module to the ESP32-S3, and the 4G power DIP is on");
    return false;
  }
  esp_modem_set_error_cb(s_dce, onModemError);
  g_usb_gone = false;

#if CELL_MODEM_DIAG
  // One-shot command-mode diagnostics before dialing (best effort).
  esp_modem_sync(s_dce);
  int csq = 0, ber = 0;
  if (esp_modem_get_signal_quality(s_dce, &csq, &ber) == ESP_OK) {
    // esp_modem returns the raw AT+CSQ index (0..31, or 99 = unknown), NOT dBm.
    // Convert to dBm so it reads correctly (e.g. CSQ 13 -> -87 dBm) and matches
    // Home Assistant's signal_strength/dBm device class.
    g_rsrp = (csq >= 0 && csq <= 31) ? (-113 + 2 * csq) : 0;   // 0 = unknown
    LOGI("cellular: signal CSQ=%d (%d dBm) ber=%d", csq, g_rsrp, ber);
  }
  int act = 0;
  if (esp_modem_get_operator_name(s_dce, g_oper, &act) == ESP_OK)
    LOGI("cellular: operator '%s' (act %d)", g_oper, act);
#endif
  return true;
}

// Enter PPP (data) mode and wait for an IP.
static bool dial() {
  esp_err_t e = esp_modem_set_mode(s_dce, ESP_MODEM_MODE_DATA);
  if (e != ESP_OK) {
    LOGW("cellular: set_mode(DATA) failed: %s", esp_err_to_name(e));
    return false;
  }
  uint32_t t0 = millis();
  while (!g_up && (millis() - t0) < (uint32_t)CELL_DIAL_TIMEOUT_S * 1000) delay(200);
  return g_up;
}

void begin() {
  esp_netif_init();                    // idempotent; Arduino may have run these
  esp_event_loop_create_default();     // returns INVALID_STATE if already up — ok
  esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onIpEvent, nullptr);

  if (!createModem()) return;          // loop()/ensureConnected() will retry
  LOGI("cellular: dialing PPP over USB...");
  dial();
}

bool isConnected() { return g_up; }

bool ensureConnected() {
  if (g_up) return true;

  if (g_usb_gone || !s_dce) {          // USB dropped -> rebuild the modem object
    LOGW("cellular: (re)opening USB modem");
    if (s_dce) { esp_modem_destroy(s_dce); s_dce = nullptr; }
    if (!createModem()) return false;
  } else {
    LOGW("cellular: re-dialing PPP");
    esp_modem_set_mode(s_dce, ESP_MODEM_MODE_COMMAND);  // best-effort back to cmd
    delay(1000);
  }
  return dial();
}

void loop() {
  // Signal quality can't be polled while in PPP data mode without CMUX; g_rsrp
  // holds the value captured at connect time (see createModem diagnostics).
}

int         signalRSRP()   { return g_rsrp; }
const char* operatorName() { return g_oper; }

} // namespace cellular

#else  // !CELLULAR_ENABLED — bench mode: modem code compiled out entirely

namespace cellular {
void        begin()          {}
bool        isConnected()    { return false; }
bool        ensureConnected(){ return false; }
void        loop()           {}
int         signalRSRP()     { return 0; }
const char* operatorName()   { return ""; }
} // namespace cellular

#endif // CELLULAR_ENABLED
