#include "cellular.h"
#include "config.h"
#include "log.h"
#include <PPP.h>

namespace cellular {

static bool     g_up = false;
static int      g_rsrp = 0;
static char     g_oper[32] = {0};

static void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_PPP_GOT_IP:
      g_up = true;
      LOGI("PPP up: %s", PPP.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_PPP_LOST_IP:
    case ARDUINO_EVENT_PPP_DISCONNECTED:
      g_up = false;
      LOGW("PPP down");
      break;
    default: break;
  }
}

static void powerOnModem() {
  // SIM7670G power-on: pulse PWRKEY low. Timing per Waveshare/SIMCom.
  pinMode(PIN_MODEM_PWRKEY, OUTPUT);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  delay(100);
  digitalWrite(PIN_MODEM_PWRKEY, HIGH);
  delay(1000);
  digitalWrite(PIN_MODEM_PWRKEY, LOW);
  delay(3000); // let the modem boot before AT
}

void begin() {
  Network.onEvent(onEvent);
  powerOnModem();

  PPP.setApn(CELL_APN);
  if (strlen(CELL_PIN)) PPP.setPin(CELL_PIN);
  PPP.setResetPin(-1); // handled via PWRKEY above
  PPP.setPins(PIN_MODEM_TX, PIN_MODEM_RX, /*rts*/-1, /*cts*/-1, /*flow*/ESP_MODEM_FLOW_CONTROL_NONE);

  LOGI("Starting modem (SIM7670G) on UART%d @%d...", MODEM_UART_NUM, MODEM_BAUD);
  // SIM7670G is the SIM7672 (Qualcomm) family — use the generic command mode.
  PPP.begin(PPP_MODEM_GENERIC, MODEM_UART_NUM, MODEM_BAUD);

  // Capture operator + signal once attached (best-effort).
  if (PPP.attached()) {
    g_rsrp = PPP.RSSI();
    String op = PPP.operatorName();
    strncpy(g_oper, op.c_str(), sizeof(g_oper) - 1);
  }
}

bool isConnected() { return g_up; }

bool ensureConnected() {
  if (g_up) return true;
  LOGW("cellular: re-dialing PPP");
  PPP.end();                       // tear down before re-dial (spec §6)
  delay(500);
  PPP.begin(PPP_MODEM_GENERIC, MODEM_UART_NUM, MODEM_BAUD);
  uint32_t t0 = millis();
  while (!g_up && millis() - t0 < (uint32_t)CELL_DIAL_TIMEOUT_S * 1000) delay(200);
  return g_up;
}

void loop() {
  static uint32_t last = 0;
  if (g_up && millis() - last > 30000) {   // refresh signal periodically
    last = millis();
    g_rsrp = PPP.RSSI();
  }
}

int signalRSRP() { return g_rsrp; }
const char* operatorName() { return g_oper; }

} // namespace cellular
