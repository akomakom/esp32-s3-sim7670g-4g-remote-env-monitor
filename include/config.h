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
//  Local secret overrides (git-ignored). Copy secrets/secrets.example.h to
//  secrets/secrets.h and put per-unit secrets there (MQTT_PASSWORD, and
//  optionally MQTT_USERNAME / DEVICE_ID / CELL_APN). Because this is included
//  BEFORE the #ifndef defaults below, whatever it defines wins — and it never
//  gets committed. (build_flags -D... still works too, for CI.)
// -----------------------------------------------------------------------------
#if __has_include("secrets/secrets.h")
  #include "secrets/secrets.h"
#endif

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

// Bench / offline mode. When 0, the modem is NEVER powered on and no cellular,
// NTP or MQTT is attempted — the unit just samples its sensors and logs over USB
// serial. Use this to validate the I2C SHT40 and RS485 bus before a SIM is
// installed. (Not powering the modem also stops it presenting its own USB
// network interface to your laptop.) Leave 1 to deploy. Can be overridden from
// platformio.ini build_flags with -DCELLULAR_ENABLED=0.
#ifndef CELLULAR_ENABLED
  #define CELLULAR_ENABLED    1
#endif

// -----------------------------------------------------------------------------
//  1.  GPIO / PINOUT  —  RESOLVE against the Waveshare wiki/schematic (spec §1)
// -----------------------------------------------------------------------------
//  Board: Waveshare ESP32-S3-SIM7670G-4G.
//  These defaults follow the published Waveshare demo pin assignments. VERIFY
//  them against your board revision before trusting hardware. Strapping pins
//  (0,3,45,46) and USB pins (19,20) are intentionally avoided for sensors.
//
//  The SIM7670G modem UART on the ESP32-S3-SIM7670G-4G. Pin order verified
//  against a working community sketch for this board (Serial1 rx=17, tx=18):
//    https://gist.github.com/leighghunt/016f615ba5af4816482cd7d264b68411
//  i.e. ESP32 RX=17 (from modem TX), ESP32 TX=18 (to modem RX). NOTE: the modem
//  also exposes a USB interface (routed by the DIP switches); this firmware uses
//  the GPIO UART path. Ensure the "4G" DIP is ON (powers the module) — there is
//  no GPIO PWRKEY to pulse, so PIN_MODEM_PWRKEY is -1.
#define MODEM_UART_NUM        1
#define PIN_MODEM_TX          18   // ESP32 -> modem RX
#define PIN_MODEM_RX          17   // ESP32 <- modem TX
#define PIN_MODEM_PWRKEY      -1   // module is powered by the onboard DIP switch
#define PIN_MODEM_DTR         -1   // not wired to a usable GPIO on this board
#define MODEM_BAUD            115200

// I2C bus for the local (crawlspace) SHT40. Pick a free SDA/SCL pair.
#define PIN_I2C_SDA           5
#define PIN_I2C_SCL           4
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

// ---- Derived metrics ----
//  Dew point and absolute humidity are DERIVED on-device from temperature +
//  humidity. They add bytes to every reading on the metered uplink and can be
//  recomputed server-side (Home Assistant templates, etc.), so they default OFF.
//  Set to 1 to compute and transmit them. (Raw temp `c` and humidity `h` are
//  always sent; these only control the derived `p`/`a` fields — see payload.cpp.)
#define SEND_DEW_POINT        0   // dew point °C          (~7 B/reading)
#define SEND_ABS_HUMIDITY     0   // absolute humidity g/m³ (~7 B/reading)

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

// ---- Hot tub water temperature over ESP-NOW ----
//  The unit also subscribes to a separate ESP32 hot tub controller over ESP-NOW
//  and records its water temperature as another "sensor" (see the BUS_ESPNOW row
//  in SENSORS[] below). The controller's WiFi channel follows its home AP and is
//  unknown, so the client channel-hops to find it (like the CYD display client).
//  This is independent of the cellular uplink and fully failure-tolerant. Set to
//  0 to compile the feature out entirely.
#ifndef HOTTUB_ESPNOW_ENABLED
  #define HOTTUB_ESPNOW_ENABLED   1
#endif
// Our board_id in the ESP-NOW pairing protocol. Must be > 0 and, if you also run
// the CYD display client, distinct from it (the CYD uses 1) so both can pair.
#define HOTTUB_BOARD_ID           20
// Highest WiFi channel to probe: 11 in North America, 13 in Europe.
#define HOTTUB_MAX_CHANNEL        11
// How long to dwell on each channel waiting for a pairing reply before hopping.
#define HOTTUB_CHANNEL_DWELL_MS   250
// If no controller message arrives within this window, assume it moved/restarted
// and restart the channel search.
#define HOTTUB_MESSAGE_MAX_AGE_MS 60000UL
// A water-temp reading older than this is considered stale and is NOT buffered
// (so a dropped hot tub link looks like a skipped sample, not a frozen value).
// Should comfortably exceed the sample interval below.
#define HOTTUB_READING_MAX_AGE_MS 600000UL   // 10 min

// ---- Sensor inventory ----
//  index 0        = the local I2C SHT40.
//  index 1..N     = RS485 Modbus sensors, in this declared order.
//  index (ESPNOW) = the hot tub water temp received over ESP-NOW (§ HOT TUB).
//  `addr` is the Modbus slave address (ignored for the I2C / ESP-NOW sensors).
//  `enabled` can also be toggled at runtime via remote config (spec §4).
//  RESOLVE: fill the real addresses after running the provisioning tool (§2).
enum SensorBus {
  BUS_I2C,     // local I2C SHT40 (temp + humidity)
  BUS_RS485,   // RS485 Modbus SHT40-class transmitter (temp + humidity)
  BUS_ESPNOW,  // hot tub controller over ESP-NOW (temp only, water temperature)
};

struct SensorDef {
  const char* key;      // short id used in the compact payload (keep it short!)
  const char* location; // human-readable, for docs/HA only
  SensorBus   bus;      // which bus/transport this sensor lives on
  uint8_t     addr;     // Modbus slave address (1..247); 0 for I2C / ESP-NOW
  bool        enabled;
};

static const SensorDef SENSORS[] = {
  //  key       location                         bus         addr  enabled
  {  "crawl",  "Crawlspace (local I2C)",         BUS_I2C,    0,    true  },
  {  "return", "Return / indoor air",            BUS_RS485,  0x01, true  },
  {  "supply", "Supply duct",                    BUS_RS485,  0x02, true  },
  {  "outdoor","Outdoor reference",              BUS_RS485,  0x03, true  },
  {  "garage", "Attached garage (dehumidified)", BUS_RS485,  0x04, true  },
  {  "tub",    "Hot tub water (ESP-NOW)",        BUS_ESPNOW, 0,    HOTTUB_ESPNOW_ENABLED },
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
// Bring-up aid: in command mode, query signal/operator via esp_modem and log it.
#define CELL_MODEM_DIAG       1

// --- USB modem transport ---
//  This board wires the SIM7670G to the ESP32-S3 over USB (not the GPIO UART);
//  PPP over UART is unsupported by this modem. We drive it as a USB CDC-ACM host
//  (esp_modem USB DTE). Set the USB DIP so the 4G module routes to the ESP32-S3.
//  VID/PID from `lsusb` (Qualcomm composite). The AT-command CDC-ACM interface is
//  IF2 ("at"); if dialing there ever fails, IF6 ("ppp") is the alternative.
#define CELL_USB_VID          0x05C6
#define CELL_USB_PID          0x9330
#define CELL_USB_ITF          2

// -----------------------------------------------------------------------------
//  4.  MQTT / TLS TRANSPORT  (spec §3, §5, §7)
// -----------------------------------------------------------------------------
#ifndef MQTT_HOST
  #define MQTT_HOST           "broker.example.com"  // RESOLVE: your broker (set in secrets.h)
#endif
#ifndef MQTT_PORT
  #define MQTT_PORT           8883                  // TLS port (override in secrets.h)
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
#define DEFAULT_REPORT_INTERVAL_S   300   // publish a batch every 30 min
#define MAX_BATCH_READINGS          40     // cap per publish (fits MQTT_BUFFER)
#define MQTT_BUFFER_BYTES           2048   // must exceed a full MessagePack batch
// Cap batches drained per report cycle so a big backlog (post-outage) can't hog
// the loop long enough to trip the watchdog. 50*40 = 2000 readings/cycle; the
// rest drains on the next cycle. (The drain also feeds the WDT between batches.)
#define MAX_REPORT_BATCHES_PER_CYCLE 50

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
// blocks arbitrary-URL abuse of the cmd topic).  RESOLVE: your update host
// (override in secrets.h so your real host isn't committed).
#ifndef OTA_ALLOWED_PREFIX
  #define OTA_ALLOWED_PREFIX  "https://ota.example.com/"
#endif
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
#define LOG_LEVEL             4        // 0=off 1=err 2=warn 3=info 4=debug
