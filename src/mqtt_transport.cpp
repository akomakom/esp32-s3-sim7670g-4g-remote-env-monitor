#include "mqtt_transport.h"
#include "config.h"
#include "log.h"
#include "runtime_config.h"
#include "timesync.h"
#include "secrets/ca_cert.h"
#include <NetworkClientSecure.h>
#include <PubSubClient.h>

namespace mqtt {

static NetworkClientSecure tls;
static PubSubClient        client(tls);
static OtaCmdHandler       g_ota_cb = nullptr;

static uint32_t g_sent = 0, g_recv = 0;
static uint32_t g_day  = 0;   // UTC day number for the byte counters

static void rollDayIfNeeded() {
  uint32_t now = timesync::nowUtc();
  if (!now) return;
  uint32_t day = now / 86400UL;
  if (day != g_day) { g_day = day; g_sent = 0; g_recv = 0; }
}

static void onMessage(char* topic, uint8_t* payload, unsigned int len) {
  g_recv += len;
  LOGD("mqtt rx %s (%u B)", topic, len);

  if (strcmp(topic, TOPIC_CONFIG) == 0) {
    rconfig::applyJson((const char*)payload, len);
    return;
  }
  // Per-key config (HA number controls): payload is a plain integer of seconds.
  if (strcmp(topic, TOPIC_CFG_SAMPLE) == 0) {
    rconfig::setSampleIntervalS(strtoul(String((char*)payload, len).c_str(), nullptr, 10));
    return;
  }
  if (strcmp(topic, TOPIC_CFG_REPORT) == 0) {
    rconfig::setReportIntervalS(strtoul(String((char*)payload, len).c_str(), nullptr, 10));
    return;
  }
  if (strcmp(topic, TOPIC_CMD) == 0) {
    // Minimal command channel. OTA is explicit and host-gated (spec §4.5/§8).
    // Format: {"cmd":"ota","url":"https://ota.example.com/fw.bin"}
    if (len < 8) return;
    String s((char*)payload, len);
    int u = s.indexOf("\"url\"");
    if (s.indexOf("\"ota\"") >= 0 && u >= 0 && g_ota_cb) {
      int q1 = s.indexOf('"', s.indexOf(':', u) + 1);
      int q2 = s.indexOf('"', q1 + 1);
      if (q1 > 0 && q2 > q1) { String url = s.substring(q1 + 1, q2); g_ota_cb(url.c_str()); }
    }
    return;
  }
}

void begin(OtaCmdHandler ota_cb) {
  g_ota_cb = ota_cb;

  if (MQTT_TLS_INSECURE) {
    tls.setInsecure();
    LOGW("mqtt: TLS validation DISABLED (bring-up only!)");
  } else {
    tls.setCACert(MQTT_CA_CERT);
  }
#ifdef MQTT_CLIENT_CERT
  tls.setCertificate(MQTT_CLIENT_CERT);
  tls.setPrivateKey(MQTT_CLIENT_KEY);
#endif
  // Bandwidth (spec §5): we keep ONE long-lived TLS/MQTT session (see
  // ensureConnected only re-handshaking when it actually drops) and a
  // persistent MQTT session (MQTT_CLEAN_SESSION=false) so we avoid re-subscribe
  // and re-handshake churn on the metered link.

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setKeepAlive(MQTT_KEEPALIVE_S);
  client.setBufferSize(MQTT_BUFFER_BYTES); // must fit a full batch payload
  client.setCallback(onMessage);
}

bool isConnected() { return client.connected(); }

bool ensureConnected() {
  if (client.connected()) return true;

  LOGI("mqtt: connecting to %s:%d", MQTT_HOST, MQTT_PORT);
  bool ok = client.connect(DEVICE_ID, MQTT_USERNAME, MQTT_PASSWORD,
                           /*willTopic*/ TOPIC_HEALTH, /*willQos*/ 0,
                           /*willRetain*/ false, /*willMsg*/ "{\"online\":false}",
                           /*cleanSession*/ MQTT_CLEAN_SESSION);
  if (!ok) { LOGW("mqtt: connect failed rc=%d", client.state()); return false; }

  client.subscribe(TOPIC_CONFIG,     MQTT_QOS);  // retained -> we get current config
  client.subscribe(TOPIC_CMD,        MQTT_QOS);
  client.subscribe(TOPIC_CFG_SAMPLE, MQTT_QOS);  // retained per-key config (HA numbers)
  client.subscribe(TOPIC_CFG_REPORT, MQTT_QOS);
  LOGI("mqtt: connected, subscribed to config+cmd");
  return true;
}

void loop() { client.loop(); }

bool publish(const char* topic, const uint8_t* data, size_t len, bool retained) {
  if (!client.connected()) return false;
  rollDayIfNeeded();
  bool ok = client.publish(topic, data, len, retained);
  if (ok) g_sent += len;
  return ok;
}

bool publishString(const char* topic, const char* s, bool retained) {
  return publish(topic, (const uint8_t*)s, strlen(s), retained);
}

uint32_t bytesSentToday() { rollDayIfNeeded(); return g_sent; }
uint32_t bytesRecvToday() { rollDayIfNeeded(); return g_recv; }

} // namespace mqtt
