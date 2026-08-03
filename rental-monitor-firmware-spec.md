# Rental Unit Environmental Monitor — Firmware Requirements Spec

**Purpose:** Unattended, cellular-connected environmental monitor for a rental unit. Two jobs: (1) verify proper environmental conditions are being maintained, and (2) produce a credible, timestamped record to defend against a false mold claim. The node lives in a remote/inaccessible location, so **reliability and failure recovery are the top priority**.

**Firmware style:** Arduino (ESP32 Arduino core **3.x** — required for the native `PPP` class and modern TLS `NetworkClientSecure`).

---

## 1. Hardware

- **Board:** Waveshare ESP32-S3-SIM7670G-4G (ESP32-S3, 2MB PSRAM, 16MB flash, SIM7670G LTE Cat-1 modem).
- **Cellular antenna:** External LTE antenna via the board's IPEX/u.FL main-antenna connector → IPEX-to-SMA pigtail → low-loss coax (LMR195/240 class, keep run short) → antenna mounted **outside the masonry / above grade**. Crawlspace reception is expected to be marginal without this.
- **Not used:** GPS/GNSS, onboard camera. (Frees the camera GPIO for other use.)
- **RS485 transceiver:** Waveshare TTL-to-RS485 module on a spare UART (auto-direction or DE/RE control pin — TBD from module variant).
- **SIM:** Hologram multi-carrier (~$0.03/MB) → bandwidth-sensitive design is a hard requirement.
- **Power:** Mains USB — board supports 18650 + solar + mains. 18650 may be installed so report voltage.  Solar will not be used.

### Open hardware items to resolve first
- **GPIO / pinout map.** Determine which ESP32-S3 pins are free vs. consumed by the SIM7670G UART, SD slot, RGB LED, battery-gauge IC, etc. Need: I2C SDA/SCL pair, RS485 UART TX/RX (+ DE/RE if not auto), and confirmation the modem UART pins aren't reused. Avoid ESP32-S3 strapping pins (0, 3, 45, 46) and USB pins (19/20) for sensors. Source: Waveshare wiki/schematic.
- **RS485 sensor Modbus register map** for the specific SHT40 Modbus units (address register, temp/humidity holding registers, baud/parity). Likely XY-MD02-class.
- **Hologram egress IP / CIDR range** for the server-side firewall filter (see §7).

---

## 2. Sensors

| # | Sensor | Bus | Location | Notes |
|---|--------|-----|----------|-------|
| 1 | SHT40 ×1 | I2C, addr **0x44** (fixed) | Crawlspace (local, short wire) | Direct to I2C pins |
| 2 | SHT40-Modbus ×4 | RS485 (Modbus RTU) over CAT6 | Return/indoor air; Supply duct; Outdoor; Attached garage (dehumidified) | Ship as address **01** — must be re-addressed to unique IDs |

**RS485 addressing procedure (one-time provisioning):** connect each sensor **individually**, write a new unique Modbus slave address to its address register, verify, then move to the next. Include a small provisioning sketch/mode for this. Record the final address→location mapping in config.

**Derived metrics (computed on-device, free):** for every sensor, compute **dew point** and **absolute humidity** in addition to T/RH. These carry the moisture-attribution story and cost nothing.

_Note:_ BLE sensor ingest from the earlier plan is **dropped** — all sensors are now wired.

---

## 3. Connectivity

- **Cellular data:** PPPoS over the SIM7670G.
  - Recommended lib path: **ESP32 Arduino core 3.x `PPP` class** (wraps esp_modem) *or* **lewisxhe's TinyGSM-fork** (`TinyGsmClientSIM7672` — SIM7670G is the Qualcomm SIM7672 family; the upstream vshymanskyy/TinyGSM does **not** compile cleanly for it). Pick one and standardize.
  - APN/user/pass configurable (Hologram APN) without recompiling.
- **Husarnet: dropped.** Device makes an **outbound-only** TLS MQTT connection to the broker; no inbound reachability needed, so no overlay VPN. Simpler and lighter on the per-MB plan.
- **Transport:** MQTT over **TLS** to Mosquitto/Home Assistant on a dedicated port.
- **Time sync:** get accurate UTC early (NTP over the cellular link, or modem network time via `AT+CCLK`). Required for correct timestamps and backdated batches.

---

## 4. Core functional requirements

1. **Sample** all 5 sensors on a schedule; timestamp each reading (UTC).
2. **Batch** readings and report periodically over MQTT to minimize connections/overhead.
3. **Backdate** buffered readings — each payload carries its original capture timestamp so Home Assistant ingests the true time, not send time.
4. **Remote config** via MQTT (retained topic the device subscribes to). Minimum: **reporting/sampling interval**. Design the config schema to extend (per-sensor enable, thresholds, etc.).
5. **Remote OTA firmware update** (nice-to-have): dual OTA partitions with **automatic rollback** if the new image fails a health check. Trigger via MQTT command → HTTPS pull. Gate explicitly (never automatic) to protect the data budget.

---

## 5. Bandwidth minimization (hard requirement — $0.03/MB)

- Batch multiple readings per publish; report on interval, not per-sample.
- **Compact payload encoding** (CBOR or tight binary; avoid verbose JSON). Short keys if JSON is unavoidable.
- Keep **one long-lived TLS/MQTT session** where practical; enable **TLS session resumption** to avoid re-handshakes.
- Tune MQTT **QoS** to avoid retransmit storms; use retained config topic so the device pulls config only on change.
- OTA is opt-in/explicit; consider compressed/delta images.
- Budget and log actual bytes/day via self-health telemetry so cost is observable.

---

## 6. Reliability & failure recovery (top priority — node is inaccessible)

- **Hardware watchdog** → auto-reboot on hang.
- **Modem/link state machine:** detect carrier loss, tear down and re-dial PPP, **exponential backoff** on repeated failures (protect battery + data).
- **Persistent buffering to flash** (LittleFS/NVS ring buffer) for unsent readings; survive multi-hour/day outages and **flush backdated** when the link returns. No data loss on reboot.
- **Brownout / low-battery handling** (if battery/solar): sleep/back off instead of crash-looping; use the onboard battery-gauge IC.
- **OTA rollback** as above.
- **Boot resilience:** safe defaults if config/flash is corrupt; never brick into an unrecoverable state.

---

## 7. Data integrity & security (this is what makes it *evidence*)

- Continuous, **timestamped** logging **pushed to the server** (not just on-device); rely on server-side retention/backups so the record is credible and tamper-resistant.
- MQTT over **TLS** to the broker on a dedicated port.
- Server firewall: **source CIDR filter** restricting the port to Hologram's egress range (resolve the range, then lock it down).
- Consider per-device MQTT credentials / client cert.

---

## 8. Self-health telemetry (report alongside sensor data)

Battery voltage, solar/charge state, cellular **RSRP/signal**, uptime, free heap, reboot cause, mains-power presence (if applicable), and **bytes sent/day**. Both keeps the system trustworthy and gives incidental context (e.g. a power outage explains a heating gap that isn't the tenant's fault).

---

## 9. Optional / roadmap datapoints (not in v1, but design to allow)

Discussed as high-value for the mold-attribution case if you expand later: **CO2** (SCD41, I2C — strong ventilation-behavior proxy), a **surface-temperature** probe for condensation risk (surface T vs. dew point), a **crawlspace water-leak** sensor (distinguishes building intrusion from lifestyle humidity), and **HVAC runtime** sensing (confirms heat is actually on). The RS485 outdoor sensor already covers the indoor-vs-outdoor attribution reference.

**Privacy guardrail:** environmental sensors only, in structural/common areas. **No** motion/occupancy/door sensors, camera, or audio — covert tenant surveillance can violate privacy/wiretap law and would badly undermine the very dispute this system exists to win. Disclose the monitoring in the lease with written acknowledgment; disclosure also strengthens the evidence.

---

## 10. Suggested Arduino libraries (starting set)

- **Cellular/PPP:** ESP32 core 3.x `PPP` **or** lewisxhe/TinyGSM-fork (SIM7672 class).
- **MQTT over TLS:** PubSubClient or 256dpi/arduino-mqtt over `NetworkClientSecure`.
- **I2C SHT40:** Adafruit SHT4x (or Sensirion driver).
- **RS485 Modbus RTU:** eModbus or ModbusMaster.
- **Payload:** ArduinoJson (with CBOR) or a compact binary packer.
- **OTA:** `Update` + esp_ota rollback, triggered via MQTT → HTTPS.
- **Storage/config:** NVS (Preferences) + LittleFS for the buffer.

---

## 11. First tasks for the new session

1. Pull the Waveshare pinout/schematic; lock in I2C pins, RS485 UART pins (+DE/RE), and confirm no conflict with the SIM7670G UART.
2. Bring up cellular PPP + a raw TLS MQTT publish to the broker (validate Hologram APN + firewall path).
3. Read the single I2C SHT40; then bring up the RS485 bus and the per-sensor addressing/provisioning routine.
4. Layer in batching + flash buffer + backdated send.
5. Add remote config (interval) over retained MQTT topic.
6. Add self-health telemetry.
7. Add watchdog + reconnect/backoff state machine.
8. (Nice-to-have) OTA with rollback.
