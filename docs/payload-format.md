# Wire payload format

To honour the $0.03/MB budget (spec §5) the device publishes **MessagePack**
with 1-character keys instead of verbose JSON. MessagePack is self-describing,
~40–60% smaller than the equivalent JSON, and decodes in one line on the server.

## Data topic — `rental/<device>/data`

Logical shape (shown as JSON for readability; on the wire it is MessagePack):

```json
{
  "v": 1,
  "d": "rental-mon-01",
  "r": [
    { "i": 0, "t": 1722690000, "c": 12.34, "h": 78.1, "p": 8.6, "a": 8.9 }
  ]
}
```

| Key | Meaning                                   |
|-----|-------------------------------------------|
| `v` | payload schema version                    |
| `d` | device id                                 |
| `r` | array of readings (one batch)             |
| `i` | sensor index into `SENSORS[]` (config.h)  |
| `t` | capture time, **UTC epoch seconds** (backdated — this is the true sample time, not send time) |
| `c` | temperature °C                            |
| `h` | relative humidity % *(omitted for temp-only sensors)* |
| `p` | dew point °C (derived) *(omitted for temp-only sensors)* |
| `a` | absolute humidity g/m³ (derived) *(omitted for temp-only sensors)* |

**Temp-only sensors:** some sources report temperature but no humidity — e.g. the
hot tub water temperature received over ESP-NOW (`BUS_ESPNOW` in `SENSORS[]`).
For those, `c` is present and `h`/`p`/`a` are omitted entirely (not sent as `0`),
so the decoder must treat those keys as optional.

Readings are **backdated**: `t` is stamped when the sample was taken and buffered
to flash, so Home Assistant records the true capture time even after a multi-day
outage flush.

## Health topic — `rental/<device>/health`

Small JSON (sent rarely, size isn't critical). Fields: `fw`, `ts`, `up` (uptime
s), `heap`, `rssi` (dBm), `op` (operator), `vbat`, `mains`, `rst` (reset cause),
`buf` (buffered records), `tx`/`rx` (bytes today), and the **active** intervals
`sample_s`/`report_s` (so a dashboard/control can reflect the live config). See
spec §8.

## Config topics — `rental/<device>/config…` (retained, device subscribes)

Two ways to change the runtime config; both clamp values (10..86400 s) and
persist to NVS, and take effect on the next cycle:

1. **Bulk JSON** — `rental/<device>/config` (retained). Unknown fields ignored:
   ```json
   { "sample_s": 300, "report_s": 1800, "en": { "garage": false } }
   ```
2. **Per-key** — `rental/<device>/config/sample_s` and `.../config/report_s`
   (retained), each a plain integer of seconds. Separate topics so two settings
   never overwrite each other's retained value while the device is offline. These
   back the Home Assistant `number` controls (see `mosquitto/ha-mqtt.yaml`); the
   controls read the live value back from the `health` topic's `sample_s`/`report_s`.

## Command topics — `rental/<device>/cmd…` (device subscribes)

Explicit, host-gated OTA (spec §4.5) on `rental/<device>/cmd`:

```json
{ "cmd": "ota", "url": "https://ota.example.com/rental-mon-01/fw-1.1.0.bin" }
```

**Report now** — `rental/<device>/cmd/report_now`: any payload triggers an
immediate sample of all sensors + publish, without waiting for the report
interval. Backs the Home Assistant "Report Now" button in `mosquitto/ha-mqtt.yaml`.
