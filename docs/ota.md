# OTA firmware update (MQTT-triggered, HTTPS pull)

The device never auto-updates. You publish an explicit command to its MQTT
`cmd` topic with an HTTPS URL; the device downloads the image, flashes it, and
reboots into a **trial** image that is **automatically rolled back** unless it
stays healthy for `OTA_HEALTHCHECK_S` (60 s). See [`src/ota.cpp`](../src/ota.cpp).

## How it works (spec §4.5 / §8)

```
you ──MQTT publish {"cmd":"ota","url":...}──▶ rental/<device>/cmd
device: verify url starts with OTA_ALLOWED_PREFIX
        HTTPS GET the .bin (server cert validated against public roots / Let's Encrypt)
        write to the idle OTA slot, reboot into it (trial)
        confirm after 60s healthy  ──or──  roll back to the previous image
```

Guardrails baked in:
- **Host allow-list:** the URL must begin with `OTA_ALLOWED_PREFIX` (set in
  `include/secrets/secrets.h`). Anything else is refused.
- **TLS:** the download validates the server against the built-in Mozilla root
  bundle (`OTA_USE_CERT_BUNDLE=1`), so **any publicly-valid cert (Let's Encrypt)
  works** — the OTA host just needs a normal HTTPS cert whose name matches the
  URL. (MQTT is independent and still pins your private CA.)
- **Dual-OTA + rollback:** `partitions_ota.csv` provides `app0`/`app1`; a bad
  image (crash/hang within 60 s) rolls back automatically on next boot.

## Prerequisites (one-time)

- Device already running firmware built from this repo (dual-OTA partitions).
- Any **HTTPS** site reachable from the device over cellular that serves the
  `.bin` under `OTA_ALLOWED_PREFIX`. With `OTA_USE_CERT_BUNDLE=1` you can just use
  your **existing Let's-Encrypt-secured site** — no private cert, no extra port,
  no new web server.
- The URL must start with `OTA_ALLOWED_PREFIX` (set in `secrets.h`), host and
  port included. `https://ota.example.com/` ⇒ your normal HTTPS site on 443.
  A different host/port means setting `OTA_ALLOWED_PREFIX` to match and flashing
  that build first — the *running* firmware's prefix is what's enforced.

## Step 1 — build the new image

Bump the version so you can see the change land, then build:

```bash
# edit include/config.h:  #define FW_VERSION  "1.0.1"
pio run
```

The app image to serve is:

```
.pio/build/rental_monitor/firmware.bin
```

Copy it to your OTA web root (e.g. next to the certs), optionally versioned:

```bash
cp .pio/build/rental_monitor/firmware.bin /path/to/otaroot/fw-1.0.1.bin
```

## Step 2 — put it on your HTTPS site

Copy `firmware.bin` into the web root your Let's Encrypt cert already serves,
under `OTA_ALLOWED_PREFIX`:

```bash
scp .pio/build/rental_monitor/firmware.bin  you@host:/var/www/html/fw-1.0.1.bin
```

Sanity-check from anywhere (public cert ⇒ no `--cacert` needed):

```bash
curl -I https://ota.example.com/fw-1.0.1.bin
```

You want `200 OK` **and** a `Content-Length` header — the device relies on it, so
serve a plain static file (not a chunked/dynamic response).

## Step 3 — trigger the update over MQTT

Publish the command to the `cmd` topic **while the device is online** and
**non-retained** (a retained OTA command would re-fire on every reconnect →
update loop):

```bash
mosquitto_pub -h broker.example.com -p 8883 \
  --cafile mosquitto/certs/ca.crt \
  --cert mosquitto/certs/client.crt --key mosquitto/certs/client.key \
  -u rental-mon-01 -P 'your-password' \
  -t 'rental/rental-mon-01/cmd' \
  -m '{"cmd":"ota","url":"https://ota.example.com/fw-1.0.1.bin"}'
```

(No `-r`.) The device acts on it the moment it arrives.

## Step 4 — watch it happen

In one terminal, subscribe to the device's ack + health:

```bash
mosquitto_sub -h broker.example.com -p 8883 \
  --cafile mosquitto/certs/ca.crt \
  --cert mosquitto/certs/client.crt --key mosquitto/certs/client.key \
  -u rental-mon-01 -P 'your-password' \
  -t 'rental/rental-mon-01/ack' -t 'rental/rental-mon-01/health' -v
```

Expected sequence:
- `.../ack  {"ota":"starting"}`
- (device downloads ~1.5 MB, then reboots — the MQTT session drops briefly)
- device comes back on the **new** image; after 60 s it self-confirms
- `.../health` now shows `"fw":"1.0.1"` (was `"1.0.0"`)

On the USB serial console you'll see `ota: pulling …`, `ota: image written OK,
rebooting into trial image`, then on next boot `ota: running a NEW image on
trial` and later `ota: new image confirmed healthy, rollback cancelled`.

If it fails you'll get `.../ack {"ota":"failed"}` and the device keeps running
the current image. Common causes: URL not under `OTA_ALLOWED_PREFIX`; the site's
cert not publicly valid or its name not matching the URL host; no `Content-Length`
(dynamic/chunked response); or the host unreachable from cellular.

## Rollback test (optional)

To prove rollback works, flash (or serve) an image that panics early — the
device boots it, fails to confirm within 60 s, and the bootloader reverts to the
previous good slot on the next reboot. Nothing to do manually.

## Notes

- **Data cost:** each OTA pulls the whole image (~1.5 MB) over cellular. At the
  spec's $0.03/MB that's ~$0.05; on pay-as-you-go metering it can be ~$0.60.
  Update deliberately, not on a schedule.
- The command JSON is parsed leniently (it just looks for `"ota"` and `"url"`).
  Keep the URL as a normal JSON string value.
- `MQTT_TLS_INSECURE` also disables OTA cert validation — leave it `false`.
