#!/usr/bin/env bash
# =============================================================================
#  test-publish.sh — publish a test value to the broker over mutual TLS.
#
#  Verifies the whole TLS path end to end: CA trust, client certificate, and
#  username/password. Publishes a retained per-sensor JSON reading to
#  `rental/<device>/sensor/<key>` — the same topic ha-bridge.py writes — so the
#  value also shows up in Home Assistant immediately.
#
#  Usage:
#     ./test-publish.sh [key] [temp_c]
#     ./test-publish.sh tub 38.5
#     ./test-publish.sh crawl 12.3
#     ./test-publish.sh health        # publish a retained "online" health msg so
#                                     # HA entities (gated on it) become available
#
#  Connection (env overrides):
#     MQTT_HOST (default broker.example.com)  MQTT_PORT (default 8883)
#     MQTT_USER (default rental-mon-01)      MQTT_PASS (default change-me)
#
#  Testing locally on the broker host (WAN not routable from inside)? The server
#  cert SAN includes localhost, so use the internal TLS port:
#     MQTT_HOST=localhost MQTT_PORT=8883 ./test-publish.sh tub 40
#
#  Requires: mosquitto-clients (mosquitto_pub) and the certs/ from gen-certs.sh.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

HOST="${MQTT_HOST:-broker.example.com}"
PORT="${MQTT_PORT:-8883}"
USER="${MQTT_USER:-rental-mon-01}"
PASS="${MQTT_PASS:-change-me}"
DEVICE="${DEVICE_ID:-rental-mon-01}"

KEY="${1:-tub}"
TEMP="${2:-38.5}"
TS="$(date -u +%s)"

if [[ "$KEY" == "health" ]]; then
  # Publish a retained "online" health message. HA entities are gated on this
  # topic for availability, so without it they stay Unavailable until the real
  # device (or this) publishes here. Handy for testing before the device is live.
  TOPIC="rental/${DEVICE}/health"
  PAYLOAD='{"online":true,"fw":"test","ts":'"$TS"',"up":42,"heap":120000,"rssi":-70,"op":"test","vbat":0,"mains":true,"rst":"poweron","buf":0,"tx":0,"rx":0}'
else
  TOPIC="rental/${DEVICE}/sensor/${KEY}"
  PAYLOAD="$(printf '{"t":%s,"c":%s}' "$TS" "$TEMP")"
fi

for f in certs/ca.crt certs/client.crt certs/client.key; do
  [[ -f "$f" ]] || { echo "missing $f — run ./gen-certs.sh first" >&2; exit 1; }
done

echo "Publishing to ${HOST}:${PORT}  topic=${TOPIC}"
echo "  payload: ${PAYLOAD}"
mosquitto_pub \
  -h "$HOST" -p "$PORT" \
  --cafile certs/ca.crt --cert certs/client.crt --key certs/client.key \
  -u "$USER" -P "$PASS" \
  -t "$TOPIC" -m "$PAYLOAD" \
  -q 1 -r -d

echo "OK — published (retained). Verify with:"
echo "  mosquitto_sub -h $HOST -p $PORT --cafile certs/ca.crt \\"
echo "    --cert certs/client.crt --key certs/client.key \\"
echo "    -u $USER -P '****' -t 'rental/#' -v"
