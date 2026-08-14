// Sensor bus bring-up and sampling for all 5 sensors (spec §2).
#pragma once
#include "reading.h"

namespace sensors {

// Initialise the I2C bus + local SHT40 and the RS485/Modbus bus.
void begin();

// Read one sensor by its index into SENSORS[]. Fills derived metrics.
// Returns a Reading with .valid=false on failure.
Reading read(uint8_t sensor_idx);

// RS485 sensor power switch (PIN_RS485_POWER). Call rs485PowerOn() before a batch
// of Modbus reads (it applies the warm-up delay) and rs485PowerOff() after, so the
// sensors are energised for the minimum time. Both are no-ops when not switched.
void rs485PowerOn();
void rs485PowerOff();

// --- One-time RS485 provisioning (spec §2) ---
// Interactive over USB serial: connect ONE sensor at a time, assign it a unique
// Modbus slave address, verify, repeat. Blocks; used only in PROVISIONING_MODE.
void runProvisioningTool();

} // namespace sensors
