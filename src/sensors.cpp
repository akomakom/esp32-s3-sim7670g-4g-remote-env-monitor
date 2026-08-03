#include "sensors.h"
#include "psychrometrics.h"
#include "config.h"
#include "log.h"
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
  LOGI("RS485 bus @%d baud ready", RS485_BAUD);
}

// Fill derived metrics + validity onto a partially-populated Reading.
static Reading finalize(Reading r, bool ok) {
  r.valid = ok;
  if (ok) {
    r.dew_c   = dewPointC(r.temp_c, r.rh);
    r.abs_hum = absoluteHumidity(r.temp_c, r.rh);
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

Reading read(uint8_t idx) {
  const SensorDef& s = SENSORS[idx];
  Reading r = s.is_i2c ? readI2C(idx) : readModbus(idx, s.addr);
  if (r.valid)
    LOGD("%s: T=%.2f RH=%.2f dew=%.2f AH=%.2f", s.key, r.temp_c, r.rh, r.dew_c, r.abs_hum);
  return r;
}

// -----------------------------------------------------------------------------
//  RS485 provisioning tool (spec §2) — assign unique slave addresses one by one.
// -----------------------------------------------------------------------------
static uint16_t readLine(uint8_t deflt) {
  while (!Serial.available()) delay(20);
  String s = Serial.readStringUntil('\n');
  s.trim();
  return s.length() ? (uint16_t)s.toInt() : deflt;
}

void runProvisioningTool() {
  Serial.println();
  Serial.println(F("=== RS485 SHT40 addressing tool ==="));
  Serial.println(F("Connect ONE sensor at a time. It ships as address 01."));
  Serial.printf ("Writing addr register 0x%04X, then verifying.\n", MODBUS_ADDR_REGISTER);

  while (true) {
    Serial.print(F("\nCurrent slave addr to talk to [1]: "));
    uint8_t cur = readLine(1);
    Serial.print(F("New unique addr to assign (1..247, 0=quit): "));
    uint8_t neu = readLine(0);
    if (neu == 0) { Serial.println(F("Done.")); return; }

    modbus.begin(cur, rs485);
    uint8_t rc = modbus.writeSingleRegister(MODBUS_ADDR_REGISTER, neu);
    if (rc != modbus.ku8MBSuccess) {
      Serial.printf("  write FAILED rc=0x%02X (check wiring/current addr)\n", rc);
      continue;
    }
    delay(200);
    // Verify by reading temperature at the NEW address.
    uint16_t t;
    if (readInputReg(neu, MODBUS_REG_TEMP, t)) {
      Serial.printf("  OK: sensor now answers at addr %u (T raw=%u). ", neu, t);
      Serial.println(F("Record this addr in config.h SENSORS[]."));
    } else {
      Serial.printf("  wrote addr %u but no reply — verify power/wiring.\n", neu);
    }
  }
}

} // namespace sensors
