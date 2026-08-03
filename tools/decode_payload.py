#!/usr/bin/env python3
"""Decode a MessagePack data payload from the rental monitor into JSON.

Usage:
    mosquitto_sub -h broker -p 8883 --cafile ca.pem -t 'rental/+/data' \
        | python3 tools/decode_payload.py            # reads base64/hex lines
    python3 tools/decode_payload.py < payload.bin    # reads raw bytes

Only dependency: `msgpack` (pip install msgpack).
"""
import sys, json, msgpack

# Sensor index -> location. Keep in sync with config.h SENSORS[].
LOCATIONS = ["crawl", "return", "supply", "outdoor", "garage"]
FIELDS = {"c": "temp_c", "h": "rh", "p": "dew_c", "a": "abs_hum", "t": "ts", "i": "sensor"}


def decode(raw: bytes) -> dict:
    doc = msgpack.unpackb(raw, raw=False)
    out = {"version": doc.get("v"), "device": doc.get("d"), "readings": []}
    for r in doc.get("r", []):
        rec = {FIELDS.get(k, k): v for k, v in r.items()}
        idx = rec.get("sensor")
        if isinstance(idx, int) and idx < len(LOCATIONS):
            rec["location"] = LOCATIONS[idx]
        out["readings"].append(rec)
    return out


if __name__ == "__main__":
    data = sys.stdin.buffer.read()
    print(json.dumps(decode(data), indent=2))
