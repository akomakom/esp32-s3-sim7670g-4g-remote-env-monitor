# Rental Unit Environmental Monitor — Firmware

Unattended, cellular-connected environmental monitor for a rental unit, built on
the **Waveshare ESP32-S3-SIM7670G-4G** with the **ESP32 Arduino core 3.x**. It
verifies that proper conditions are maintained and produces a credible,
timestamped, server-pushed record to defend against a false mold claim.

Implements [`rental-monitor-firmware-spec.md`](rental-monitor-firmware-spec.md).
Because the node is inaccessible, **reliability and failure recovery are the top
priority** — every reading is persisted to flash *before* any network attempt,
timestamped at capture, and backdated on send.

## Everything you tune lives in one file

All pinout, sensor, cellular, broker, and reliability settings — including every
item the spec flagged "resolve first" — are in
**[`include/config.h`](include/config.h)**, each with a working default and a
comment. Search that file for `RESOLVE` for the items that need real values
before a unit is trustworthy:

| RESOLVE item (spec)          | Where in config.h            | Default shipped        |
|------------------------------|------------------------------|------------------------|
| GPIO / pinout map (§1)       | section 1                    | Waveshare demo pins    |
| RS485 Modbus register map (§1)| section 2                   | XY-MD02-class defaults |
| Modbus sensor addresses (§2) | `SENSORS[]`                  | 0x01–0x04 placeholders |
| Hologram APN (§3)            | section 3                    | `hologram`             |
| Broker host/port/creds (§7)  | section 4                    | `broker.example.com`   |
| Hologram egress CIDR (§7)    | section 9 (informational)    | `RESOLVE-with-Hologram`|
| OTA host allow-list (§4.5)   | section 8                    | `ota.example.com`      |

TLS material (broker CA chain, optional client cert) goes in
`include/secrets/ca_cert.h` (git-ignored; copy from `ca_cert.example.h`).

## Build & flash

Uses [PlatformIO](https://platformio.org/):

```bash
pio run                 # build
pio run -t upload       # flash over USB
pio device monitor      # serial log @115200
```

Host/secrets can be injected at build time via `build_flags` in
[`platformio.ini`](platformio.ini) instead of editing `config.h`.

## Provisioning the RS485 sensors (one-time, spec §2)

The four RS485 SHT40 sensors ship as address `01` and must get unique addresses.
Set `PROVISIONING_MODE true` in `config.h` (or hold **BOOT** at reset), then:

1. Connect **one** sensor to the RS485 bus.
2. Open the serial monitor; follow the prompts to assign a unique address.
3. Verify, disconnect, repeat for each sensor.
4. Record each final address in `SENSORS[]`, set `PROVISIONING_MODE false`, reflash.

## Hot tub water temperature (ESP-NOW)

Beyond the 5 wired sensors, the unit also subscribes to a separate ESP32 **hot
tub controller** over **ESP-NOW** and records its **water temperature** as a 6th
sensor (`tub`), flowing through the same buffer → batch → publish pipeline. The
uplink is cellular (PPP), so the WiFi radio is free — ESP-NOW runs on it without
disturbing anything else.

The controller's WiFi channel follows its home AP and is unknown, so the client
**channel-hops 11→1** sending pairing requests until the controller answers
(same protocol as the CYD display client in the companion project). The whole
search is a non-blocking, `millis()`-driven state machine and is fully
failure-tolerant: every ESP-IDF call is checked (no panics), and if the link
drops or a reading goes stale it is simply skipped — never buffered as a frozen
value. Water temp arrives in °F and is converted to °C to match everything else.

Configure it in the **HOT TUB** block of [`config.h`](include/config.h)
(`HOTTUB_ESPNOW_ENABLED`, `HOTTUB_BOARD_ID`, `HOTTUB_MAX_CHANNEL`, staleness
windows). The wire protocol lives in
[`include/hot_tub_types.h`](include/hot_tub_types.h) — a **verbatim** copy of the
controller's header (keep the two in sync). The client itself is
[`src/espnow_tub.cpp`](src/espnow_tub.cpp). Disable at runtime like any sensor:
`{"en":{"tub":false}}` on the retained config topic; or set
`HOTTUB_ESPNOW_ENABLED 0` to compile it out.

## How it maps to the spec

| Spec area | Implementation |
|-----------|----------------|
| §2 Sensors + derived metrics | [`sensors.cpp`](src/sensors.cpp), [`psychrometrics.cpp`](src/psychrometrics.cpp) (dew point, absolute humidity) |
| §3 Cellular PPP + time | [`cellular.cpp`](src/cellular.cpp) (core 3.x `PPP`), [`timesync.cpp`](src/timesync.cpp) |
| §4 Batching, backdating, remote config, OTA | [`main.cpp`](src/main.cpp), [`runtime_config.cpp`](src/runtime_config.cpp), [`ota.cpp`](src/ota.cpp) |
| §5 Bandwidth (MessagePack, long-lived TLS, byte accounting) | [`payload.cpp`](src/payload.cpp), [`mqtt_transport.cpp`](src/mqtt_transport.cpp) |
| §6 Reliability (watchdog, backoff, flash buffer, brownout, rollback) | [`main.cpp`](src/main.cpp), [`ring_buffer.cpp`](src/ring_buffer.cpp) |
| §7 Security (TLS, per-device creds, client cert hook) | [`mqtt_transport.cpp`](src/mqtt_transport.cpp), `secrets/` |
| §8 Self-health telemetry | [`health.cpp`](src/health.cpp) |

Wire formats and the retained-config / OTA-command JSON are documented in
[`docs/payload-format.md`](docs/payload-format.md). A server-side decoder is in
[`tools/decode_payload.py`](tools/decode_payload.py). The full **OTA update
walkthrough** (build → host over HTTPS → trigger via MQTT → verify/rollback) is
in [`docs/ota.md`](docs/ota.md).

## Data flow

```
sample all enabled sensors ─▶ stamp UTC ─▶ persist to LittleFS ring buffer
        (every sample_s)                         │ (survives reboot/outage)
                                                 ▼
   on report_s, if link up:  drain buffer ─▶ MessagePack batch ─▶ MQTT/TLS publish
                                                 │ pop only after confirmed send
                                                 ▼
                              Mosquitto / Home Assistant (backdated timestamps)
```

## Roadmap (spec §9, designed-for but not in v1)

CO₂ (SCD41), surface-temp condensation probe, crawlspace leak sensor, HVAC
runtime sensing. The config schema and `SENSORS[]` table are structured to add
these without a rewrite. **Privacy guardrail:** environmental sensors only — no
motion/occupancy/camera/audio (spec §9).
