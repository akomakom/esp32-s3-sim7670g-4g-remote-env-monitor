#include "sensors.h"
#include "psychrometrics.h"
#include "config.h"
#include "log.h"
#include "espnow_tub.h"
#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include <ModbusMaster.h>

namespace sensors {

static Adafruit_SHT4x sht4;          // local I2C sensor
static bool           sht4_ok = false;

static HardwareSerial rs485(RS485_UART_NUM);
static ModbusMaster   modbus;

// ---- RS485 direction control (only if the module isn't auto-direction) ----
static void rs485PreTx()  { if (PIN_RS485_DE_RE >= 0) digitalWrite(PIN_RS485_DE_RE, HIGH); }
static void rs485PostTx() { if (PIN_RS485_DE_RE >= 0) digitalWrite(PIN_RS485_DE_RE, LOW);  }

// ---- RS485 sensor power switch (MOSFET gate; steady on/off) ----
static void rs485PowerSet(bool on) {
  if (PIN_RS485_POWER < 0) return;
  bool level = RS485_POWER_ACTIVE_HIGH ? on : !on;
  digitalWrite(PIN_RS485_POWER, level ? HIGH : LOW);
}

void begin() {
  // --- I2C SHT40 ---
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
  sht4_ok = sht4.begin(&Wire);
  if (sht4_ok) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    LOGI("I2C SHT40 @0x%02X ready", SHT40_I2C_ADDRESS);
  } else {
    LOGE("I2C SHT40 not found @0x%02X", SHT40_I2C_ADDRESS);
  }

  // --- RS485 bus ---
  rs485.begin(RS485_BAUD, RS485_PARITY, PIN_RS485_RX, PIN_RS485_TX);
  if (PIN_RS485_DE_RE >= 0) {
    pinMode(PIN_RS485_DE_RE, OUTPUT);
    digitalWrite(PIN_RS485_DE_RE, LOW);
  }
  modbus.preTransmission(rs485PreTx);
  modbus.postTransmission(rs485PostTx);
  // RS485 sensor power switch: start OFF (energised only during reads).
  if (PIN_RS485_POWER >= 0) {
    pinMode(PIN_RS485_POWER, OUTPUT);
    rs485PowerSet(false);
    LOGI("RS485 sensors power-switched on GPIO%d (%s-active)", PIN_RS485_POWER,
         RS485_POWER_ACTIVE_HIGH ? "high" : "low");
  }
  LOGI("RS485 bus @%d baud ready", RS485_BAUD);
}

// Fill derived metrics + validity onto a partially-populated Reading.
static Reading finalize(Reading r, bool ok) {
  r.valid = ok;
  if (ok) {
    // Derived metrics are optional (config.h) — leave NaN when disabled so the
    // payload encoder omits them and no bandwidth is spent.
    r.dew_c   = SEND_DEW_POINT    ? dewPointC(r.temp_c, r.rh)        : NAN;
    r.abs_hum = SEND_ABS_HUMIDITY ? absoluteHumidity(r.temp_c, r.rh) : NAN;
  }
  return r;
}

static Reading readI2C(uint8_t idx) {
  Reading r{}; r.sensor_idx = idx;
  if (!sht4_ok) {
    // try a late re-init in case the bus glitched
    sht4_ok = sht4.begin(&Wire);
    if (!sht4_ok) return finalize(r, false);
  }
  sensors_event_t hum, temp;
  if (!sht4.getEvent(&hum, &temp)) return finalize(r, false);
  r.temp_c = temp.temperature;
  r.rh     = hum.relative_humidity;
  return finalize(r, true);
}

static bool readInputReg(uint8_t addr, uint16_t reg, uint16_t& out) {
  modbus.begin(addr, rs485);
  uint8_t rc = (MODBUS_FUNC_READ == 0x04)
                 ? modbus.readInputRegisters(reg, 1)
                 : modbus.readHoldingRegisters(reg, 1);
  if (rc != modbus.ku8MBSuccess) {
    LOGW("Modbus addr %u reg 0x%04X failed rc=0x%02X", addr, reg, rc);
    return false;
  }
  out = modbus.getResponseBuffer(0);
  return true;
}

static Reading readModbus(uint8_t idx, uint8_t addr) {
  Reading r{}; r.sensor_idx = idx;
  uint16_t rawT, rawH;
  bool ok = readInputReg(addr, MODBUS_REG_TEMP, rawT) &&
            readInputReg(addr, MODBUS_REG_HUMIDITY, rawH);
  if (!ok) return finalize(r, false);
  r.temp_c = (int16_t)rawT * MODBUS_TEMP_SCALE; // temp is signed
  r.rh     = rawH * MODBUS_HUM_SCALE;
  return finalize(r, true);
}

// The hot tub water temp arrives asynchronously over ESP-NOW and is cached by
// the espnow_tub module. A "read" just snapshots the latest fresh value; if the
// link is down or the value is stale we return invalid, exactly like a failed
// bus read, so nothing bogus is buffered. Water temp has no humidity, so the
// humidity-derived metrics are left NaN and omitted from the payload.
static Reading readEspNow(uint8_t idx) {
  Reading r{}; r.sensor_idx = idx;
  r.rh = r.dew_c = r.abs_hum = NAN;
  float temp_c;
  if (!espnow_tub::latestWaterTempC(temp_c, HOTTUB_READING_MAX_AGE_MS)) {
    r.valid = false;
    return r;
  }
  r.temp_c = temp_c;
  r.valid  = true;
  return r;
}

Reading read(uint8_t idx) {
  const SensorDef& s = SENSORS[idx];
  Reading r;
  switch (s.bus) {
    case BUS_I2C:    r = readI2C(idx);            break;
    case BUS_ESPNOW: r = readEspNow(idx);         break;
    case BUS_RS485:
    default:         r = readModbus(idx, s.addr); break;
  }
  if (r.valid)
    LOGD("%s: T=%.2f RH=%.2f dew=%.2f AH=%.2f", s.key, r.temp_c, r.rh, r.dew_c, r.abs_hum);
  return r;
}

void rs485PowerOn() {
  if (PIN_RS485_POWER < 0) return;
  rs485PowerSet(true);
  delay(RS485_POWER_WARMUP_MS);   // let the transmitters boot before we poll them
}

void rs485PowerOff() { rs485PowerSet(false); }

// -----------------------------------------------------------------------------
//  RS485 provisioning tool (spec §2) — assign unique slave addresses one by one.
// -----------------------------------------------------------------------------
// Read one line of input, terminated by CR or LF (minicom sends CR on Enter,
// others LF — accept either). Characters are echoed back since serial terminals
// usually have local echo off, so the operator can see what they type. An empty
// line returns `deflt`.
static uint16_t readLine(uint16_t deflt) {
  String s;
  for (;;) {
    while (!Serial.available()) delay(5);
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') break;             // Enter ends the line
    if (c == 8 || c == 127) {                       // backspace / delete
      if (s.length()) { s.remove(s.length() - 1); Serial.print(F("\b \b")); }
      continue;
    }
    if (c < 32) continue;                           // ignore stray control bytes
    s += c;
    Serial.write(c);                                // echo the keystroke
  }
  Serial.print(F("\r\n"));
  s.trim();
  return s.length() ? (uint16_t)s.toInt() : deflt;
}

// Read one HOLDING register (fn 0x03). Used to read the slave-address register
// back for persistence verification. Returns false if the sensor doesn't reply.
static bool readHoldingReg(uint8_t addr, uint16_t reg, uint16_t& out) {
  modbus.begin(addr, rs485);
  if (modbus.readHoldingRegisters(reg, 1) != modbus.ku8MBSuccess) return false;
  out = modbus.getResponseBuffer(0);
  return true;
}

// Probe a sensor at `addr`: read temperature + humidity and print them so the
// operator can confirm the bus is actually talking to a live sensor. Returns
// true on any reply.
static bool probeSensor(uint8_t addr) {
  uint16_t rawT = 0, rawH = 0;
  bool okT = readInputReg(addr, MODBUS_REG_TEMP, rawT);
  bool okH = readInputReg(addr, MODBUS_REG_HUMIDITY, rawH);
  if (okT && okH) {
    Serial.printf("  addr %u LIVE:  T=%.1f C   RH=%.1f %%\r\n",
                  addr, (int16_t)rawT * MODBUS_TEMP_SCALE, rawH * MODBUS_HUM_SCALE);
    return true;
  }
  if (okT || okH) {
    Serial.printf("  addr %u partial reply (T=%s, RH=%s) — check the register map\r\n",
                  addr, okT ? "ok" : "fail", okH ? "ok" : "fail");
    return true;
  }
  Serial.printf("  addr %u: NO REPLY — check wiring, power, and the current address\r\n", addr);
  return false;
}

// Write `neu` to the address register at `cur` using Modbus function `fn`
// (0x06 = write single, 0x10 = write multiple). Returns the Modbus rc.
static uint8_t writeAddrReg(uint8_t cur, uint16_t neu, uint8_t fn) {
  modbus.begin(cur, rs485);
  if (fn == 0x10) {
    modbus.setTransmitBuffer(0, neu);
    return modbus.writeMultipleRegisters(MODBUS_ADDR_REGISTER, 1);
  }
  return modbus.writeSingleRegister(MODBUS_ADDR_REGISTER, neu);
}

// Power-cycle the RS485 rail (if switchable) so a written address can latch, and
// warn if the rail doesn't actually remove the sensor's power.
static void powerCycleSensor(uint8_t cur) {
#if MODBUS_ADDR_APPLY_POWERCYCLE
  if (PIN_RS485_POWER >= 0) {
    rs485PowerOff();
    delay(400);
    uint16_t off;
    if (readInputReg(cur, MODBUS_REG_TEMP, off))
      Serial.println(F("    WARNING: sensor still responds with the rail OFF — it isn't losing power."));
    delay(1200);
    rs485PowerOn();          // includes the configured warm-up delay
  } else {
    Serial.println(F("    no RS485 power switch — cycle the sensor's power manually now."));
    delay(500);
  }
#else
  delay(300);
#endif
}

// One address-change attempt with a given function code: write, show the
// register read-back, power-cycle, then report whether the sensor now answers at
// the new address. Returns true on success.
// One address-change attempt: write `neu` to the address register — sent to
// `writeAt` (normally `cur`, or 0 for a broadcast write some clones require),
// wait for the EEPROM commit, power-cycle, and report whether the sensor now
// answers at the new address.
static bool tryAddrChange(uint8_t cur, uint16_t neu, uint8_t fn, uint8_t writeAt) {
  Serial.printf("  attempt: write addr %u, fn 0x%02X, to %s ...\r\n",
                neu, fn, writeAt == 0 ? "broadcast(0)" : "current addr");
  uint8_t rc = writeAddrReg(writeAt, neu, fn);
  if (writeAt != 0 && rc != modbus.ku8MBSuccess) {   // broadcast gets no reply -> don't require an ACK
    Serial.printf("    write FAILED rc=0x%02X\r\n", rc);
    return false;
  }
  // CRITICAL: give the sensor time to finish writing the address to EEPROM before
  // we remove power — cutting power mid-commit makes it revert.
  Serial.printf("    waiting %dms for EEPROM commit before power cycle...\r\n", MODBUS_ADDR_COMMIT_MS);
  delay(MODBUS_ADDR_COMMIT_MS);
  powerCycleSensor(cur);
  return probeSensor(neu);
}

void runProvisioningTool() {
  rs485PowerOn();   // keep the sensors energised for the whole interactive session
  Serial.println();
  Serial.println(F("=== RS485 SHT40 addressing tool ==="));
  Serial.println(F("Connect ONE sensor at a time. Sensors typically ship as address 1."));
  Serial.printf ("Address register 0x%04X. Enter 0 at the first prompt to quit.\r\n",
                 MODBUS_ADDR_REGISTER);

  while (true) {
    Serial.println();
    Serial.print(F("Current slave addr to talk to [1, 0=quit]: "));
    uint16_t cur = readLine(1);
    if (cur == 0) {
      Serial.println(F("Done — reflash with PROVISIONING_MODE=false to deploy."));
      return;
    }

    // 1) Confirm we can actually talk to this sensor BEFORE changing anything.
    Serial.printf("Probing addr %u ...\r\n", cur);
    if (!probeSensor((uint8_t)cur)) continue;       // no reply -> back to the top

    // 2) Ask for the new address (blank = leave as-is and re-probe next round).
    Serial.print(F("New unique addr to assign (1..247, blank=leave unchanged): "));
    uint16_t neu = readLine(cur);
    if (neu == cur)              { Serial.println(F("  unchanged.")); continue; }
    if (neu < 1 || neu > 247)    { Serial.println(F("  out of range (1..247) — skipping.")); continue; }

    // 3) Read the address register first so we know its value before the write.
    uint16_t before = 0;
    if (readHoldingReg((uint8_t)cur, MODBUS_ADDR_REGISTER, before))
      Serial.printf("  addr register 0x%04X currently reads %u\r\n", MODBUS_ADDR_REGISTER, before);
    else
      Serial.printf("  NOTE: addr register 0x%04X not readable at addr %u — it may be the wrong\r\n"
                    "  register for this model.\r\n", MODBUS_ADDR_REGISTER, cur);

    // 4) Try the configured write function; if the sensor doesn't adopt the new
    //    address, automatically fall back to the other. Common quirk on these
    //    clones: fn 0x06 writes RAM only and reverts on power loss, so the
    //    address only sticks when written with fn 0x10.
    uint8_t first  = MODBUS_ADDR_WRITE_MULTI ? 0x10 : 0x06;
    uint8_t second = MODBUS_ADDR_WRITE_MULTI ? 0x06 : 0x10;
    bool ok = tryAddrChange((uint8_t)cur, neu, first, (uint8_t)cur);
    if (!ok && probeSensor((uint8_t)cur)) {
      Serial.println(F("  that didn't take — retrying with the other write function..."));
      ok = tryAddrChange((uint8_t)cur, neu, second, (uint8_t)cur);
    }
    if (!ok && probeSensor((uint8_t)cur)) {
      Serial.println(F("  still no — last resort: broadcast write (some clones only accept addr 0)..."));
      ok = tryAddrChange((uint8_t)cur, neu, 0x06, 0);
    }

    // 5) Report the outcome.
    if (ok) {
      uint16_t stored = 0;
      if (readHoldingReg((uint8_t)neu, MODBUS_ADDR_REGISTER, stored) && stored == neu)
        Serial.printf("  persisted OK: address register reads %u at the new address.\r\n", stored);
      Serial.printf("  DONE: sensor is live at addr %u. Record it in config.h SENSORS[].\r\n", neu);
    } else if (probeSensor((uint8_t)cur)) {
      uint16_t regNow = 0;
      bool regOk = readHoldingReg((uint8_t)cur, MODBUS_ADDR_REGISTER, regNow);
      Serial.printf("  address did NOT change — still live at %u after fn 0x06, 0x10 and broadcast.\r\n", cur);
      if (regOk && regNow == neu)
        Serial.println(F("  The register holds the new value but the sensor won't act on it."));
      else if (regOk)
        Serial.printf("  The register reverted to %u — the write isn't being saved to NVM.\r\n", regNow);
      Serial.println(F("  Our frame matches the XY-MD02/SHT20 datasheet exactly, so this unit likely has\r\n"
                       "  a firmware quirk. Cross-check with a PC Modbus tool (mbpoll); meanwhile just\r\n"
                       "  leave it at addr 1 — the firmware supports per-sensor addresses in SENSORS[]."));
    } else {
      Serial.println(F("  no reply at new OR old address — check power/wiring, then re-probe."));
    }
  }
}

} // namespace sensors
