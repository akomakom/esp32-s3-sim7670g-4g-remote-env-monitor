#include "health.h"
#include "config.h"
#include "cellular.h"
#include "mqtt_transport.h"
#include "timesync.h"
#include "ring_buffer.h"
#include "runtime_config.h"
#include <esp_system.h>

namespace health {

float batteryVoltage() {
  if (!BATTERY_INSTALLED) return 0.0f;
  if (PIN_BATTERY_ADC < 0) return 0.0f;  // gauge-IC path could be added here
  // Simple ADC read (mV) * divider. analogReadMilliVolts is calibrated on S3.
  uint32_t mv = analogReadMilliVolts(PIN_BATTERY_ADC);
  return (mv / 1000.0f) * BATTERY_ADC_DIVIDER;
}

bool mainsPresent() {
  // On mains USB the board is always powered; if a battery is installed a high
  // voltage (>4.0V under load) implies charging/mains. Without a dedicated
  // sense line we report true when no battery is configured (mains-only unit).
  if (!BATTERY_INSTALLED) return true;
  return batteryVoltage() > 4.0f;
}

const char* resetReason() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_SW:       return "sw";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_INT_WDT:  return "int_wdt";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_WDT:      return "wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_DEEPSLEEP:return "deepsleep";
    default:               return "other";
  }
}

size_t buildJson(char* out, size_t cap) {
  float vbat = batteryVoltage();
  return snprintf(out, cap,
    "{\"online\":true,\"fw\":\"%s\",\"ts\":%u,\"up\":%u,"
    "\"heap\":%u,\"rssi\":%d,\"op\":\"%s\",\"vbat\":%.2f,"
    "\"mains\":%s,\"rst\":\"%s\",\"buf\":%u,\"tx\":%u,\"rx\":%u,"
    "\"sample_s\":%u,\"report_s\":%u}",
    FW_VERSION_FULL,
    timesync::nowUtc(),
    (uint32_t)(millis() / 1000),
    (uint32_t)ESP.getFreeHeap(),
    cellular::signalRSRP(),
    cellular::operatorName(),
    vbat,
    mainsPresent() ? "true" : "false",
    resetReason(),
    (uint32_t)ringbuf::count(),
    mqtt::bytesSentToday(),
    mqtt::bytesRecvToday(),
    rconfig::get().sample_interval_s,
    rconfig::get().report_interval_s);
}

} // namespace health
