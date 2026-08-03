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
| `h` | relative humidity %                       |
| `p` | dew point °C (derived)                    |
| `a` | absolute humidity g/m³ (derived)          |

Readings are **backdated**: `t` is stamped when the sample was taken and buffered
to flash, so Home Assistant records the true capture time even after a multi-day
outage flush.

## Health topic — `rental/<device>/health`

Small JSON (sent rarely, size isn't critical). Fields: `fw`, `ts`, `up` (uptime
s), `heap`, `rssi` (dBm), `op` (operator), `vbat`, `mains`, `rst` (reset cause),
`buf` (buffered records), `tx`/`rx` (bytes today). See spec §8.

## Config topic — `rental/<device>/config` (retained, device subscribes)

Publish a small **JSON** doc *retained* so the device pulls it only on change
(spec §5). Unknown fields are ignored; known ones are validated and persisted:

```json
{ "sample_s": 300, "report_s": 1800, "en": { "garage": false } }
```

## Command topic — `rental/<device>/cmd` (device subscribes)

Explicit, host-gated OTA only (spec §4.5):

```json
{ "cmd": "ota", "url": "https://ota.example.com/rental-mon-01/fw-1.1.0.bin" }
```
