#include "ring_buffer.h"
#include "config.h"
#include "log.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_heap_caps.h>

namespace ringbuf {

// -----------------------------------------------------------------------------
// RAM-primary buffer + occasional flash snapshot (see ring_buffer.h / config.h).
//
// The hot path (push / peek / popFront) is pure RAM — zero flash wear. Flash is
// written only by maintain(), and only when readings have been sitting UNSENT
// past BUFFER_FLASH_INTERVAL_S (an outage), at most once per interval; the
// snapshot is a single wholesale rewrite of the live records. When the buffer
// drains, the snapshot is deleted so a reboot doesn't resurrect already-sent
// data. On boot the snapshot is restored into RAM.
// -----------------------------------------------------------------------------

static const char*   SNAP_PATH = BUFFER_SNAPSHOT_PATH;
static const uint32_t MAGIC    = 0x524D4233;         // "RMB3"
static const size_t   CAP      = BUFFER_MAX_RECORDS; // RAM capacity in records
static const uint32_t FLUSH_MS = (uint32_t)BUFFER_FLASH_INTERVAL_S * 1000UL;

static Reading* g_ram   = nullptr;     // ring storage (heap; PSRAM if available)
static size_t   g_head  = 0;           // index of oldest record
static size_t   g_count = 0;           // live records in RAM
static bool     g_ready = false;

// Flash-snapshot bookkeeping (all millis()-based; wall clock not required).
static bool     g_flash_on_disk = false; // a non-empty snapshot currently exists
static bool     g_dirty         = false; // RAM changed since the last snapshot
static uint32_t g_fill_start_ms = 0;     // when the buffer last became non-empty
static uint32_t g_last_flush_ms = 0;     // when we last wrote a snapshot

struct SnapHdr { uint32_t magic; uint32_t count; };

// ---- flash snapshot I/O -----------------------------------------------------
static void writeSnapshot() {
  File f = LittleFS.open(SNAP_PATH, "w", true);
  if (!f) { LOGE("buffer: snapshot open failed"); return; }
  SnapHdr h = { MAGIC, (uint32_t)g_count };
  f.write((const uint8_t*)&h, sizeof(h));
  for (size_t i = 0; i < g_count; i++) {
    const Reading& r = g_ram[(g_head + i) % CAP];
    f.write((const uint8_t*)&r, sizeof(Reading));
    if ((i & 0x1FF) == 0) esp_task_wdt_reset();   // large snapshots must not trip WDT
  }
  f.close();
  g_flash_on_disk = (g_count > 0);
  LOGI("buffer: snapshot written (%u records)", (unsigned)g_count);
}

static void removeSnapshot() {
  if (LittleFS.exists(SNAP_PATH)) LittleFS.remove(SNAP_PATH);
  g_flash_on_disk = false;
}

static void restoreSnapshot() {
  File f = LittleFS.open(SNAP_PATH, "r");
  if (!f) return;
  SnapHdr h{};
  if (f.size() >= (int)sizeof(h) && f.read((uint8_t*)&h, sizeof(h)) == (int)sizeof(h) &&
      h.magic == MAGIC) {
    size_t n = h.count > CAP ? CAP : h.count;      // clamp to RAM capacity
    for (size_t i = 0; i < n; i++) {
      if (f.read((uint8_t*)&g_ram[i], sizeof(Reading)) != (int)sizeof(Reading)) { n = i; break; }
    }
    g_head = 0; g_count = n;
    g_flash_on_disk = (n > 0);
    if (n) g_fill_start_ms = millis();             // treat restored data as already-aged
  }
  f.close();
}

// ---- lifecycle --------------------------------------------------------------
bool begin() {
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    LOGE("LittleFS mount failed"); return false;
  }
  // Reclaim files from earlier buffer designs.
  if (LittleFS.exists(BUFFER_FILE_PATH)) LittleFS.remove(BUFFER_FILE_PATH);
  if (LittleFS.exists("/buffer.dat"))    LittleFS.remove("/buffer.dat");
  if (LittleFS.exists("/buffer.hdr"))    LittleFS.remove("/buffer.hdr");

  // Prefer PSRAM (keeps ~112 KB off the internal heap); fall back to internal.
  g_ram = (Reading*)heap_caps_malloc(CAP * sizeof(Reading), MALLOC_CAP_SPIRAM);
  if (!g_ram) g_ram = (Reading*)malloc(CAP * sizeof(Reading));
  if (!g_ram) { LOGE("buffer: alloc %u records failed", (unsigned)CAP); return false; }

  g_head = g_count = 0;
  restoreSnapshot();
  g_ready = true;
  LOGI("buffer ready: %u/%u records (RAM-primary)", (unsigned)g_count, (unsigned)CAP);
  return true;
}

bool push(const Reading& r) {
  if (!g_ready) return false;
  if (g_count >= CAP) {
    if (!BUFFER_DROP_OLDEST) { LOGW("buffer full, dropping new reading"); return false; }
    g_head = (g_head + 1) % CAP;                    // evict oldest
    g_count--;
  }
  g_ram[(g_head + g_count) % CAP] = r;
  g_count++;
  if (g_fill_start_ms == 0) g_fill_start_ms = millis();
  g_dirty = true;
  return true;
}

size_t count() { return g_ready ? g_count : 0; }

bool peek(size_t n, Reading& out) {
  if (!g_ready || n >= g_count) return false;
  out = g_ram[(g_head + n) % CAP];
  return true;
}

void popFront(size_t n) {
  if (!g_ready) return;
  if (n >= g_count) { g_head = 0; g_count = 0; }
  else { g_head = (g_head + n) % CAP; g_count -= n; }
  if (g_count == 0) g_fill_start_ms = 0;
  g_dirty = true;                                   // snapshot is now stale
}

void clear() {
  if (!g_ready) return;
  g_head = g_count = 0;
  g_fill_start_ms = 0;
  g_dirty = true;
}

// Rate-limited flash policy: snapshot only once data has been unsent for a full
// interval (an outage), then at most once per interval; drop the snapshot once
// the buffer drains so a reboot doesn't resurrect already-sent readings.
void maintain() {
  if (!g_ready) return;
  const uint32_t now = millis();

  if (g_count == 0) {
    if (g_flash_on_disk) { removeSnapshot(); g_dirty = false; } // one-time cleanup
    return;
  }
  const bool aged = (now - g_fill_start_ms) >= FLUSH_MS;        // unsent >= interval
  const bool due  = (g_last_flush_ms == 0) || (now - g_last_flush_ms) >= FLUSH_MS;
  if (aged && due && g_dirty) {
    writeSnapshot();
    g_last_flush_ms = now;
    g_dirty = false;
  }
}

void flushNow() {
  if (!g_ready) return;
  if (g_count > 0) { writeSnapshot(); g_last_flush_ms = millis(); g_dirty = false; }
  else if (g_flash_on_disk) { removeSnapshot(); }
}

} // namespace ringbuf
