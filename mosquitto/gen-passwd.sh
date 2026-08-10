#!/usr/bin/env bash
# =============================================================================
#  gen-passwd.sh — create/update the mosquitto password file for the TLS
#  listener. Run on the BROKER host (needs docker + the eclipse-mosquitto image).
#
#  Usage:
#     ./gen-passwd.sh [username] [password]
#  Defaults to  rental-mon-01 / change-me  (matches config.h defaults so it
#  works out of the box — CHANGE IT, and set the same value in MQTT_PASSWORD).
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

USER="${1:-rental-mon-01}"
PASS="${2:-change-me}"
IMAGE="${MOSQUITTO_IMAGE:-eclipse-mosquitto:2}"

mkdir -p config
# -b: batch (password on cmdline); -c: create fresh file (single device user).
docker run --rm -v "$PWD/config:/cfg" "$IMAGE" \
  mosquitto_passwd -b -c /cfg/passwd "$USER" "$PASS"

# The broker runs as uid 1883 inside the container; let it read the file.
if command -v sudo >/dev/null 2>&1; then
  sudo chown 1883:1883 config/passwd || true
fi
chmod 600 config/passwd || true

echo "wrote config/passwd for user '$USER'"
[[ "$PASS" == "change-me" ]] && echo "WARNING: using the default password — change it!"
