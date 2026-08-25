#!/usr/bin/env python3
"""ha-bridge — decode the Rental Monitor's MessagePack `data` batches into
per-sensor JSON that Home Assistant's MQTT integration can read.

The device publishes compact MessagePack to `rental/<device>/data` (see
docs/payload-format.md). HA's Jinja templates can't decode MessagePack, so this
bridge subscribes to that topic over TLS, unpacks each reading, and re-publishes
it as **retained JSON** to:

    rental/<device>/sensor/<key>      e.g. rental/rental-mon-01/sensor/tub

    payload: {"t":<utc_epoch>,"c":<temp_c>[,"h":rh,"p":dew_c,"a":abs_hum]}

The matching HA config is in ha-mqtt.yaml. Health (`.../health`) is already JSON
and is consumed by HA directly — this bridge does not touch it.

Run it wherever it can reach the broker (e.g. on the broker host):

    pip install paho-mqtt msgpack
    ./ha-bridge.py                       # uses env / defaults below

Deps: paho-mqtt (v1 or v2), msgpack.
"""
import json
import os
import sys
import ssl
import time

import msgpack
import paho.mqtt.client as mqtt

# ---- config (env overrides) -------------------------------------------------
HOST = os.environ.get("MQTT_HOST", "localhost")     # bridge usually runs beside broker
PORT = int(os.environ.get("MQTT_PORT", "8883"))     # broker's internal TLS port
USER = os.environ.get("MQTT_USER", "rental-mon-01")
PASS = os.environ.get("MQTT_PASS", "change-me")
CA   = os.environ.get("MQTT_CA",   "certs/ca.crt")
CERT = os.environ.get("MQTT_CERT", "certs/client.crt")
KEY  = os.environ.get("MQTT_KEY",  "certs/client.key")
# TLS hostname to check against the server cert SAN. When running on the broker
# host you connect to localhost but the cert is for broker.example.com — the SAN
# includes localhost/127.0.0.1 too, so leave HOST=localhost and this matches.
DATA_SUB = os.environ.get("MQTT_DATA_SUB", "rental/+/data")

# sensor index -> key. Keep in sync with config.h SENSORS[] (and decode_payload.py).
KEYS = ["crawl", "return", "supply", "outdoor", "garage", "tub", "pipe"]
# short wire key -> which fields to forward (others are omitted when NaN/absent)
FIELDS = ("t", "c", "h", "p", "a")


def on_connect(client, userdata, flags, reason_code, properties=None):
    # paho v2 passes a ReasonCode (has .is_failure); v1 passes an int rc.
    failed = getattr(reason_code, "is_failure", None)
    if failed is None:
        failed = reason_code != 0
    if failed:
        print(f"connect failed: {reason_code}", file=sys.stderr)
        return
    client.subscribe(DATA_SUB, qos=1)
    print(f"connected; subscribed to {DATA_SUB}")


def on_message(client, userdata, msg):
    try:
        doc = msgpack.unpackb(msg.payload, raw=False)
    except Exception as e:
        print(f"skip undecodable msg on {msg.topic}: {e}", file=sys.stderr)
        return
    device = doc.get("d") or msg.topic.split("/")[1]
    for r in doc.get("r", []):
        idx = r.get("i")
        key = KEYS[idx] if isinstance(idx, int) and idx < len(KEYS) else f"idx{idx}"
        out = {k: r[k] for k in FIELDS if k in r}
        topic = f"rental/{device}/sensor/{key}"
        client.publish(topic, json.dumps(out, separators=(",", ":")),
                       qos=1, retain=True)
        print(f"-> {topic} {out}")


def main():
    # paho-mqtt v2 needs the callback-API version; fall back for v1.
    try:
        client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2)
    except AttributeError:
        client = mqtt.Client()
    client.username_pw_set(USER, PASS)
    client.tls_set(ca_certs=CA, certfile=CERT, keyfile=KEY,
                   tls_version=ssl.PROTOCOL_TLS_CLIENT)
    client.on_connect = on_connect
    client.on_message = on_message
    # Auto-reconnect on drops; also retry the FIRST connect so we tolerate the
    # broker not being up yet at container start.
    client.reconnect_delay_set(min_delay=1, max_delay=30)
    while True:
        try:
            client.connect(HOST, PORT, keepalive=60)
            break
        except Exception as e:
            print(f"connect to {HOST}:{PORT} failed ({e}); retrying in 5s",
                  file=sys.stderr)
            time.sleep(5)
    client.loop_forever()


if __name__ == "__main__":
    main()
