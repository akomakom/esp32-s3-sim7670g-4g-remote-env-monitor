// =============================================================================
//  config.h  —  ALL tunables, secrets, and "open items" for the Rental Monitor
// =============================================================================
//
//  This is the ONE file you edit to commission a unit. Everything the firmware
//  spec left "TBD" lives here with a sensible, clearly-labelled default so the
//  build always compiles and runs. Search this file for the word  RESOLVE  to
//  find the items the spec flagged as "resolve first" (§1, §7).
//
//  Nothing in here requires a code change to re-provision a unit; secrets can
//  also be overridden at build time (see platformio.ini build_flags) so they
//  need not be committed to git.
//
//  Units: temperatures °C, humidity %RH, time seconds unless noted.
// =============================================================================
#pragma once

// -----------------------------------------------------------------------------
//  0.  BUILD / IDENTITY
// -----------------------------------------------------------------------------
#define FW_VERSION            "1.0.0"          // reported in health telemetry
#ifndef DEVICE_ID
  #define DEVICE_ID           "rental-mon-01"  // unique per unit; used in topics
#endif

// If true, boot straight into the interactive RS485 addressing tool over USB
// serial instead of normal operation (see §2 "one-time provisioning").
// Leave false for deployed units. Can also be forced by holding BOOT at reset.
#define PROVISIONING_MODE     false

// -----------------------------------------------------------------------------
//  1.  GPIO / PINOUT  —  RESOLVE against the Waveshare wiki/schematic (spec §1)
// -----------------------------------------------------------------------------
//  Board: Waveshare ESP32-S3-SIM7670G-4G.
//  These defaults follow the published Waveshare demo pin assignments. VERIFY
//  them against your board revision before trusting hardware. Strapping pins
//  (0,3,45,46) and USB pins (19,20) are intentionally avoided for sensors.
//
//  The SIM7670G modem UART is fixed by the board and must NOT be reused.
#define MODEM_UART_NUM        1
#define PIN_MODEM_TX          17   // ESP32 -> modem RX   (Waveshare default)
#define PIN_MODEM_RX          18   // ESP32 <- modem TX
#define PIN_MODEM_PWRKEY      4    // pulse to power the SIM7670G on
#define PIN_MODEM_DTR         5    // optional sleep/wake control (-1 if unused)
#define MODEM_BAUD            115200

// I2C bus for the local (crawlspace) SHT40. Pick a free SDA/SCL pair.
#define PIN_I2C_SDA           15
#define PIN_I2C_SCL           16
#define I2C_FREQ_HZ           100000

// RS485 UART for the Modbus SHT40 sensors, via Waveshare TTL-to-RS485 module.
#define RS485_UART_NUM        2
#define PIN_RS485_TX          13
#define PIN_RS485_RX          14
// DE/RE direction-control pin. Set to -1 if your RS485 module is auto-direction
// (many Waveshare modules are). Otherwise wire DE+RE together to this pin.
#define PIN_RS485_DE_RE       -1

// Onboard status RGB LED (WS2812). -1 disables LED status.
#define PIN_RGB_LED           38

// Battery gauge: the board ships an 18650 holder + gauge IC. If a cell is
// installed we report its voltage (spec §1, §8). Set to false if no cell.
#define BATTERY_INSTALLED     false
// ADC fallback if the fuel-gauge IC is not populated/used. -1 = no ADC reading.
#define PIN_BATTERY_ADC       -1
#define BATTERY_ADC_DIVIDER   2.0f   // on-board resistor divider ratio

// -----------------------------------------------------------------------------
//  2.  SENSORS
// -----------------------------------------------------------------------------
#define SHT40_I2C_ADDRESS     0x44   // fixed on the SHT40 (spec §2)

// ---- RS485 Modbus SHT40 register map ----  RESOLVE for your exact units.
//  Defaults below are for XY-MD02 / SHT20-Modbus-class transmitters, which the
//  spec names as the likely part. VERIFY against your sensor's datasheet.
#define RS485_BAUD            9600
#define RS485_PARITY          SERIAL_8N1
#define MODBUS_FUNC_READ      0x04   // read Input Registers (XY-MD02 style)
#define MODBUS_REG_TEMP       0x0001 // input register holding temperature
#define MODBUS_REG_HUMIDITY   0x0002 // input register holding humidity
#define MODBUS_TEMP_SCALE     0.1f   // raw * scale = °C   (signed int16)
#define MODBUS_HUM_SCALE      0.1f   // raw * scale = %RH
#define MODBUS_ADDR_REGISTER  0x0101 // holding register storing the slave addr
#define MODBUS_TIMEOUT_MS     500

// ---- Sensor inventory ----
//  index 0        = the local I2C SHT40.
//  index 1..N     = RS485 Modbus sensors, in this declared order.
//  `addr` is the Modbus slave address (ignored for the I2C sensor).
//  `enabled` can also be toggled at runtime via remote config (spec §4).
//  RESOLVE: fill the real addresses after running the provisioning tool (§2).
struct SensorDef {
  const char* key;      // short id used in the compact payload (keep it short!)
  const char* location; // human-readable, for docs/HA only
  bool        is_i2c;   // true = local I2C SHT40, false = RS485 Modbus
  uint8_t     addr;     // Modbus slave address (1..247); 0 for the I2C sensor
  bool        enabled;
};

static const SensorDef SENSORS[] = {
  //  key       location                         i2c    addr  enabled
  {  "crawl",  "Crawlspace (local I2C)",         true,   0,    true  },
  {  "return", "Return / indoor air",            false,  0x01, true  },
  {  "supply", "Supply duct",                    false,  0x02, true  },
  {  "outdoor","Outdoor reference",              false,  0x03, true  },
  {  "garage", "Attached garage (dehumidified)", false,  0x04, true  },
};
static const size_t SENSOR_COUNT = sizeof(SENSORS) / sizeof(SENSORS[0]);

// -----------------------------------------------------------------------------
//  3.  CELLULAR  (spec §3)  —  RESOLVE Hologram APN specifics if non-default
// -----------------------------------------------------------------------------
#ifndef CELL_APN
  #define CELL_APN            "hologram"   // Hologram default APN
#endif
#define CELL_USER             ""           // usually blank for Hologram
#define CELL_PASS             ""
#define CELL_PIN              ""           // SIM PIN, "" if none
#define CELL_DIAL_TIMEOUT_S   90           // give the modem time to attach

// -----------------------------------------------------------------------------
//  4.  MQTT / TLS TRANSPORT  (spec §3, §5, §7)
// -----------------------------------------------------------------------------
#ifndef MQTT_HOST
  #define MQTT_HOST           "broker.example.com"   // RESOLVE: your broker
#endif
#ifndef MQTT_PORT
  #define MQTT_PORT           8883                    // dedicated TLS port
#endif
#ifndef MQTT_USERNAME
  #define MQTT_USERNAME       "rental-mon-01"         // per-device creds (§7)
#endif
#ifndef MQTT_PASSWORD
  #define MQTT_PASSWORD       "change-me"
#endif
#define MQTT_KEEPALIVE_S      60
#define MQTT_QOS              1        // at-least-once; HA de-dups on timestamp
#define MQTT_CLEAN_SESSION    false    // persistent session -> fewer resubs (§5)

// Topic layout. Data is published under .../data, health under .../health,
// config is a *retained* topic the device subscribes to (§4), commands under
// .../cmd for OTA etc.
#define TOPIC_BASE            "rental/" DEVICE_ID
#define TOPIC_DATA            TOPIC_BASE "/data"
#define TOPIC_HEALTH          TOPIC_BASE "/health"
#define TOPIC_CONFIG          TOPIC_BASE "/config"   // retained, device subs
#define TOPIC_CMD             TOPIC_BASE "/cmd"       // device subs
#define TOPIC_ACK             TOPIC_BASE "/ack"       // device publishes

// TLS: paste the broker's CA chain (PEM) into secrets/ca_cert.h as
// `MQTT_CA_CERT`. If you skip it and set MQTT_TLS_INSECURE the device will NOT
// validate the server cert — acceptable ONLY for bring-up, never for evidence.
#define MQTT_TLS_INSECURE     false
// Optional mutual-TLS client cert (spec §7 "client cert"): define
// MQTT_CLIENT_CERT / MQTT_CLIENT_KEY in secrets/ca_cert.h to enable.

// -----------------------------------------------------------------------------
//  5.  SCHEDULING & BATCHING  (spec §4, §5)  — remotely overridable
// -----------------------------------------------------------------------------
//  These are DEFAULTS. The retained config topic can override them live without
//  reflashing (see remote_config). Keep the send interval large to save data.
#define DEFAULT_SAMPLE_INTERVAL_S   300    // read all sensors every 5 min
#define DEFAULT_REPORT_INTERVAL_S   1800   // publish a batch every 30 min
#define MAX_BATCH_READINGS          40     // cap per publish (fits MQTT_BUFFER)
#define MQTT_BUFFER_BYTES           2048   // must exceed a full MessagePack batch

// -----------------------------------------------------------------------------
//  6.  FLASH RING BUFFER  (spec §6 — survive multi-day outages)
// -----------------------------------------------------------------------------
#define BUFFER_FILE_PATH      "/buffer.bin"
#define BUFFER_MAX_RECORDS    20000   // ~ several days of 5 sensors @ 5 min
#define BUFFER_DROP_OLDEST    true    // when full, drop oldest (keep recent)

// -----------------------------------------------------------------------------
//  7.  RELIABILITY  (spec §6)
// -----------------------------------------------------------------------------
#define WATCHDOG_TIMEOUT_S    120     // hardware task watchdog auto-reboots hang
#define RECONNECT_BACKOFF_MIN_S   10  // exponential backoff floor
#define RECONNECT_BACKOFF_MAX_S   1800 // ...and ceiling (protect data + battery)
#define LOW_BATTERY_SLEEP_V   3.30f   // below this, deep-sleep+backoff not crash
#define LOW_BATTERY_SLEEP_S   3600    // sleep this long when browning out

// -----------------------------------------------------------------------------
//  8.  OTA  (spec §4.5, §6)  —  explicit, never automatic
// -----------------------------------------------------------------------------
#define OTA_ENABLED           true
// Only URLs on this HTTPS host prefix are accepted (defends the data budget and
// blocks arbitrary-URL abuse of the cmd topic).  RESOLVE: your update host.
#define OTA_ALLOWED_PREFIX    "https://ota.example.com/"
#define OTA_HEALTHCHECK_S     60      // must stay healthy this long or rollback

// -----------------------------------------------------------------------------
//  9.  SERVER-SIDE (informational only — enforced on the server, not here)
// -----------------------------------------------------------------------------
//  RESOLVE (spec §1, §7): Hologram egress CIDR range to allow on the broker's
//  firewall for MQTT_PORT. Recorded here so it lives with the unit config.
//  Example (VERIFY current range with Hologram):
//      HOLOGRAM_EGRESS_CIDR = "10.176.0.0/13"
#define HOLOGRAM_EGRESS_CIDR  "RESOLVE-with-Hologram"

// -----------------------------------------------------------------------------
//  10. LOGGING
// -----------------------------------------------------------------------------
#define LOG_BAUD              115200
#define LOG_LEVEL             3        // 0=off 1=err 2=warn 3=info 4=debug
