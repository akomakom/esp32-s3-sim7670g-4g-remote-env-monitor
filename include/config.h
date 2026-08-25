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
#define FW_VERSION            "1.0.3"          // version PREFIX; bump this by hand
// The build appends the current git short hash to the reported version. The
// pre-build script (git_rev.py, via platformio.ini extra_scripts) regenerates
// include/git_rev.h with GIT_REV each build; __has_include keeps non-PlatformIO
// builds (IDE indexers etc.) compiling with a "nogit" fallback.
#if __has_include("git_rev.h")
  #include "git_rev.h"
#endif
#ifndef GIT_REV
  #define GIT_REV             "nogit"
#endif
#define FW_VERSION_FULL       FW_VERSION "+" GIT_REV   // e.g. "1.0.3+9517564-dirty"
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

// Optional SOFTWARE power control for the SIM7670G. Per Waveshare's FAQ, with the
// "4G" DIP switch OFF a GPIO gates module power (HIGH = on, LOW = off). This lets
// the firmware POWER-CYCLE the modem to recover a wedged USB link (cdc_acm TX
// timeouts), and bypasses the mechanically flaky 4G DIP contact.
//   -1  = powered by the DIP switch, no software control (default; unchanged).
//   33  = Waveshare's documented pin for this board. VERIFY on your unit: set the
//         4G DIP OFF and confirm the modem powers up. CAVEATS: the FAQ's "GPIO22"
//         alternative does NOT exist on the ESP32-S3; and GPIO33 is the octal-PSRAM
//         data line, so this REQUIRES PSRAM to stay disabled (it is) and is
//         mutually exclusive with enabling SPIRAM.
#define PIN_MODEM_POWER          -1     // set to 33 to enable software power control
#define MODEM_POWER_ACTIVE_HIGH  true
#define MODEM_POWER_BOOT_MS      3000   // settle time after powering the module on
#define MODEM_RECOVER_AFTER_FAILS 3     // consecutive dial fails -> GPIO power-cycle
// Last-resort recovery that needs NO DIP access: if the link stays down this long,
// reboot to reset the ESP32-S3 USB-host stack (empirically what clears a wedged
// cdc_acm modem link). The RAM buffer is flushed to flash first. 0 = disabled.
#define MODEM_REBOOT_AFTER_S     3600   // reboot after this long continuously offline
// Shorter threshold until the FIRST successful connect since boot. A cold power-on
// boots the ESP32 and modem at once and can lose the USB enumeration race (a wedge
// a re-dial can't clear — but a flash clears it). Rebooting then acts like a flash:
// warm modem + fresh USB host -> it connects. Keep this comfortably longer than a
// healthy cold boot (modem registration ~80 s + PPP dial) so a slow-but-OK boot is
// never rebooted. 0 = use MODEM_REBOOT_AFTER_S even before the first connect.
#define MODEM_FIRST_CONNECT_REBOOT_S  300

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

// Optional power switch for the RS485 sensors: a single GPIO drives a MOSFET gate
// (steady digital on/off — NOT PWM). The sensors are powered ONLY during a Modbus
// read, so they can't self-heat from being always-on (they read true ambient
// right after power-up). -1 = permanently powered (no switching).
#define PIN_RS485_POWER       15
// Gate polarity: true = driving the pin HIGH turns the sensors ON (typical low-
// side N-MOSFET / HIGH-active driver module); false = active-low.
#define RS485_POWER_ACTIVE_HIGH  true

// 1-Wire bus for DS18B20 temperature probes (e.g. the water pipe). Needs a 4.7k
// pull-up from the data line to 3V3. -1 disables. (See the BUS_DS18B20 row in
// SENSORS[].)
#define PIN_DS18B20           16

// Onboard status RGB LED (WS2812). -1 disables LED status. (The esp32s3 Arduino
// variant defaults PIN_RGB_LED to 48; this board's addressable LED is on 38, so
// override the variant's value. config.h is always included after Arduino.h.)
#ifdef PIN_RGB_LED
  #undef PIN_RGB_LED
#endif
#define PIN_RGB_LED           38
// Status-LED behaviour (colour/pattern legend is documented in led.cpp).
#define STATUS_LED_ENABLED    1
#define STATUS_LED_BRIGHTNESS 40     // 0..255 master brightness (the WS2812 is bright)
// Some boards wire the addressable LED in a byte order different from the WS2812
// GRB that rgbLedWrite() assumes, which permutes colours (on this board green
// showed as red -> R and G swapped). Set 0 if your colours come out correct.
#define STATUS_LED_SWAP_RG    1
// Brief power-on colour self-test: shows RED/GREEN/BLUE/WHITE (~0.5 s each) and
// logs each colour name so you can confirm the mapping. Set 0 once it's correct.
#define STATUS_LED_SELFTEST   1

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

// Register numbering: this firmware speaks RELATIVE (0-based, on-the-wire)
// register offsets — what ModbusMaster expects. Datasheets frequently quote
// ABSOLUTE 3xxxx/4xxxx numbers instead; convert with the helpers below so you
// can paste datasheet values directly and keep BOTH conventions documented:
//   input   3xxxx (fn 0x04):    relative = 3xxxx - 30001  -> MB_INPUT(3xxxx)
//   holding 4xxxx (fn 0x03/0x06): relative = 4xxxx - 40001 -> MB_HOLDING(4xxxx)
#define MB_INPUT(abs)         ((uint16_t)((abs) - 30001))
#define MB_HOLDING(abs)       ((uint16_t)((abs) - 40001))

#define MODBUS_FUNC_READ      0x04              // read Input Registers (fn 0x04)
// Values below are for the tested SHT20 unit (XY-MD02 class); the generic SHT40
// transmitters on order should match, but VERIFY against their datasheet. Each
// line shows the relative offset the firmware uses and the absolute datasheet #.
#define MODBUS_REG_TEMP       MB_INPUT(30002)   // 0x0001  temperature (T  = raw/10)
#define MODBUS_REG_HUMIDITY   MB_INPUT(30003)   // 0x0002  humidity    (RH = raw/10)
#define MODBUS_TEMP_SCALE     0.1f              // raw * scale = °C   (signed int16)
#define MODBUS_HUM_SCALE      0.1f              // raw * scale = %RH
#define MODBUS_ADDR_REGISTER  MB_HOLDING(40258) // 0x0101  slave-address node reg
#define MODBUS_TIMEOUT_MS     500     // NOTE: informational only — ModbusMaster hardcodes
                                      // a 2000 ms response timeout (ku16MBResponseTimeout).
// Bus robustness: Modbus RTU requires a quiet gap between frames (>=3.5 char times,
// ~3.6 ms @ 9600), and auto-direction RS485 transceivers need a moment to turn
// around. Space each transaction and retry transient failures so back-to-back
// polling of several sensors doesn't drop frames.
#define MODBUS_INTERFRAME_MS  10      // quiet gap before each transaction
#define MODBUS_READ_RETRIES   2       // extra attempts on a failed read
// Many of these transmitters latch a newly-written Modbus address only after a
// power cycle. When a switchable RS485 rail exists (PIN_RS485_POWER), the
// addressing tool cycles it after writing and re-verifies at the new address.
// Set 0 if your sensor adopts the address immediately and you want to skip that.
#define MODBUS_ADDR_APPLY_POWERCYCLE  1
// Write function for the address register: 0 = writeSingleRegister (fn 0x06, the
// usual); 1 = writeMultipleRegisters (fn 0x10). Some modules only accept config
// writes via 0x10. The addressing tool reads the register back after writing to
// show whether the value actually stuck, so you can tell which one your unit needs.
#define MODBUS_ADDR_WRITE_MULTI       0
// After ACKing an address write, these sensors commit it to EEPROM asynchronously
// over a few hundred ms. Removing power before that finishes aborts the save and
// the address reverts — so wait this long after the write BEFORE any power cycle.
#define MODBUS_ADDR_COMMIT_MS         1500
// After switching the RS485 sensors on (PIN_RS485_POWER), wait this long for the
// transmitters to boot and be ready to answer Modbus before reading. Tune to
// your sensors (XY-MD02-class boot ~1s). Ignored if not power-switched.
#define RS485_POWER_WARMUP_MS 1500

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
// Diagnostics: at boot, scan for WiFi APs and log their RSSI. Compare the
// strongest AP's RSSI here to what a phone reads at the same spot — if the board
// is ~tens of dB weaker, its WiFi RX/antenna path is impaired (vs. an ESP-NOW-only
// or coexistence issue). Also, espnow loop() logs the live tub RSSI to serial so
// it's visible even with cellular (and thus MQTT health) disabled. Set 0 when done.
#define HOTTUB_WIFI_SCAN_DIAG     1
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
  BUS_DS18B20, // 1-Wire DS18B20 on PIN_DS18B20 (temp only)
};

struct SensorDef {
  const char* key;      // short id used in the compact payload (keep it short!)
  const char* location; // human-readable, for docs/HA only
  SensorBus   bus;      // which bus/transport this sensor lives on
  uint8_t     addr;     // Modbus slave address (1..247); 0 for I2C / ESP-NOW / DS18B20
  bool        enabled;
};

static const SensorDef SENSORS[] = {
  //  key       location                         bus          addr  enabled
  {  "crawl",  "Crawlspace (local I2C)",         BUS_I2C,     0,    true  },
  {  "return", "Return / indoor air",            BUS_RS485,   0x01, true  },
  {  "supply", "Supply duct",                    BUS_RS485,   0x02, true  },
  {  "outdoor","Outdoor reference",              BUS_RS485,   0x03, true  },
  {  "garage", "Attached garage (dehumidified)", BUS_RS485,   0x04, true  },
  {  "tub",    "Hot tub water (ESP-NOW)",        BUS_ESPNOW,  0,    HOTTUB_ESPNOW_ENABLED },
  {  "pipe",   "Water pipe (DS18B20)",           BUS_DS18B20, 0,    true  },
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

// NOTE: CMUX (AT + PPP multiplexed, which would allow LIVE signal polling) was
// tried and does NOT work on this SIM7670G-over-USB board — set_mode(CMUX)
// returns ESP_FAIL and hangs into a watchdog reboot loop, and it even left the
// modem stuck so plain DATA mode then failed until a modem power-cycle. Do not
// re-add it. Signal is therefore only read in the command-mode window at
// (re)connect (see refreshSignal), so rssi is a snapshot, not continuous.

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
#define MQTT_KEEPALIVE_S      600      // Adds a periodic ping that uses data
#define MQTT_QOS              1        // at-least-once; HA de-dups on timestamp
#define MQTT_CLEAN_SESSION    false    // persistent session -> fewer resubs (§5)

// Topic layout. Data is published under .../data, health under .../health,
// config is a *retained* topic the device subscribes to (§4), commands under
// .../cmd for OTA etc.
#define TOPIC_BASE            "rental/" DEVICE_ID
#define TOPIC_DATA            TOPIC_BASE "/data"
#define TOPIC_HEALTH          TOPIC_BASE "/health"
#define TOPIC_CONFIG          TOPIC_BASE "/config"   // retained, device subs (bulk JSON)
#define TOPIC_CMD             TOPIC_BASE "/cmd"       // device subs
#define TOPIC_CMD_NOW         TOPIC_BASE "/cmd/report_now" // device subs (HA button)
#define TOPIC_ACK             TOPIC_BASE "/ack"       // device publishes
// Per-key retained config topics for simple HA `number` controls. Separate
// topics so two settings never overwrite each other's retained value (matters
// when the device is offline and a change is applied on reconnect).
#define TOPIC_CFG_SAMPLE      TOPIC_BASE "/config/sample_s"  // retained, device subs
#define TOPIC_CFG_REPORT      TOPIC_BASE "/config/report_s"  // retained, device subs

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
//  6.  BUFFER  (spec §6 — survive outages)  — RAM-primary, flash as insurance
// -----------------------------------------------------------------------------
//  Readings live in a RAM ring buffer and are published from there; flash is
//  touched ONLY as outage insurance. While the link is healthy readings are sent
//  within a report cycle and never hit flash. Only if data stays UNSENT longer
//  than BUFFER_FLASH_INTERVAL_S (an outage) is the buffer snapshotted to flash,
//  and then at most once per interval. So a reboot mid-outage loses at most that
//  interval of readings, and flash writes drop from ~thousands/day to ~1 per
//  hour-of-outage (Home Assistant can't backfill history anyway — the authoritative
//  timestamped record is the server-side store, see docs).
#define BUFFER_SNAPSHOT_PATH  "/buffer.snap" // wholesale RAM snapshot (rare writes)
#define BUFFER_FILE_PATH      "/buffer.bin"  // legacy files removed on first boot
#define BUFFER_MAX_RECORDS    2000    // RAM capacity (×~28 B ≈ 56 KB internal heap; the
                                      // buffer prefers PSRAM, but SPIRAM is not enabled
                                      // in sdkconfig — enable it to raise this safely)
#define BUFFER_DROP_OLDEST    true    // when full, drop oldest (keep recent)
#define BUFFER_FLASH_INTERVAL_S  3600 // persist unsent data at most this often (outage)

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
// OTA TLS trust: 1 = validate the download host against the built-in Mozilla
// root bundle (public CAs, incl. Let's Encrypt) so you can serve firmware from
// an ordinary HTTPS site; 0 = pin your private CA (MQTT_CA_CERT) instead. Note
// this is INDEPENDENT of MQTT, which always uses the private CA.
#define OTA_USE_CERT_BUNDLE   1
// Stall timeout for the OTA download: abort only if NO data arrives for this many
// seconds. Cat-1 cellular is slow/jittery, so keep this generous — the transfer
// as a whole can take a while; we only give up on a true stall.
#define OTA_DOWNLOAD_TIMEOUT_S 30
// On a stall or dropped connection, reconnect and resume the download from where
// it left off (HTTP Range request) up to this many times before giving up. Lets
// a flaky cellular link finish the pull without re-fetching what already landed.
#define OTA_MAX_RESUMES        20

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
